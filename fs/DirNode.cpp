/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2003-2004, Valient Gough
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "fs/DirNode.h"

#include "base/logging.h"
#include "base/optional.h"
#include "base/Error.h"
#include "base/Mutex.h"

#include "fs/CipherFileIO.h"
#include "fs/Context.h"
#include "fs/FileUtils.h"
#include "fs/MACFileIO.h"
#include "fs/fsconfig.pb.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <numeric>

#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>

using std::list;
using std::string;
using std::shared_ptr;

namespace encfs {

DirTraverse::DirTraverse() {}

DirTraverse::DirTraverse(Directory &&_dir_io, uint64_t _iv,
                         const shared_ptr<NameIO> &_naming)
    : dir_io(std::move(_dir_io)), iv(_iv), naming(_naming) {}

std::string DirTraverse::nextPlaintextName(FsFileType *fileType,
                                           fs_file_id_t *inode) {
  opt::optional<FsDirEnt> dirent;
  while ((dirent = dir_io->readdir())) {
    if (fileType && dirent->type) *fileType = *dirent->type;
    if (inode) *inode = dirent->file_id;
    try {
      uint64_t localIv = iv;
      return std::move(naming->decodePath({dirent->name}, &localIv).front());
    }
    catch (Error &ex) {
      // .. .problem decoding, ignore it and continue on to next name..
      LOG(INFO) << "error decoding filename " << dirent->name << " : "
                << ex.what();
    }
  }

  return string();
}

std::string DirTraverse::nextInvalid() {
  opt::optional<FsDirEnt> dirent;
  while ((dirent = dir_io->readdir())) {
    try {
      uint64_t localIv = iv;
      naming->decodePath({dirent->name}, &localIv);
      continue;
    }
    catch (Error &ex) {
      return dirent->name;
    }
  }

  return string();
}

struct RenameEl {
  // ciphertext names
  Path oldCName;
  Path newCName;  // intermediate name (not final cname)

  // plaintext names
  Path oldPName;
  Path newPName;

  bool isDirectory;

  ~RenameEl() {
    // clear out plaintext names from memory
    oldPName.zero();
    newPName.zero();
  }
};

class RenameOp {
 private:
  DirNode *dn;
  shared_ptr<list<RenameEl>> renameList;
  list<RenameEl>::const_iterator last;

 public:
  RenameOp(DirNode *_dn, const shared_ptr<list<RenameEl>> &_renameList)
      : dn(_dn), renameList(_renameList) {
    last = renameList->begin();
  }

  RenameOp(const RenameOp &src)
      : dn(src.dn), renameList(src.renameList), last(src.last) {}

  operator bool() const { return (bool)renameList; }

  bool apply();
  void undo();
};

// TODO: share this code with DirNode::rename()
bool RenameOp::apply() {
  try {
    while (last != renameList->end()) {
      // backing store rename.
      LOG(INFO) << "renaming " << last->oldCName << "-> " << last->newCName;
      auto oldCNamePath = last->oldCName;
      auto newCNamePath = last->newCName;

      fs_time_t old_mtime;
      bool preserve_mtime;
      try {
        old_mtime = encfs::get_attrs(dn->fs_io, oldCNamePath).mtime;
        preserve_mtime = true;
      }
      catch (...) {
        preserve_mtime = false;
      }

      // internal node rename..
      dn->renameNode(last->oldPName, last->newPName);

      // rename on disk..
      try {
        dn->fs_io->rename(oldCNamePath, newCNamePath);
      }
      catch (const std::system_error &err) {
        LOG(WARNING) << "Error renaming " << last->oldCName << ": "
                     << err.code().message();
        dn->renameNode(last->newPName, last->oldPName, false);
        return false;
      }

      if (preserve_mtime) {
        withExceptionCatcherNoRet((int)std::errc::io_error,
                                  bindMethod(dn->fs_io, &FsIO::set_times),
                                  newCNamePath, opt::nullopt, old_mtime);
      }

      ++last;
    }

    return true;
  }
  catch (Error &err) {
    LOG(WARNING) << "caught error in rename application: " << err.what();
    return false;
  }
}

void RenameOp::undo() {
  LOG(INFO) << "in undoRename";

  if (last == renameList->begin()) {
    LOG(INFO) << "nothing to undo";
    return;  // nothing to undo
  }

  // list has to be processed backwards, otherwise we may rename
  // directories and directory contents in the wrong order!
  int undoCount = 0;
  int errorCount = 0;
  list<RenameEl>::const_iterator it = last;

  while (it != renameList->begin()) {
    --it;

    LOG(INFO) << "undo: renaming " << it->newCName << " -> " << it->oldCName;

    auto newCNamePath = it->newCName;
    auto oldCNamePath = it->oldCName;

    try {
      dn->fs_io->rename(newCNamePath, oldCNamePath);
    }
    catch (const std::system_error &err) {
      // ignore system errors
      LOG(WARNING) << "error in rename und: " << err.code().message();
    }
    try {
      dn->renameNode(it->newPName, it->oldPName, false);
    }
    catch (Error &err) {
      if (++errorCount == 1)
        LOG(WARNING) << "error in rename und: " << err.what();
      // continue on anyway...
    }
    ++undoCount;
  };

  LOG(WARNING) << "Undo rename count: " << undoCount;
}

DirNode::DirNode(std::shared_ptr<EncFS_Context> _ctx, const string &sourceDir,
                 const FSConfigPtr &_config)
    : mutex(),
      ctx(_ctx),
      fsConfig(_config),
      rootDir(fsConfig->opts->fs_io->pathFromString(sourceDir)),
      naming(fsConfig->nameCoding),
      fs_io(fsConfig->opts->fs_io) {}

DirNode::~DirNode() {}

bool DirNode::hasDirectoryNameDependency() const {
  return naming ? naming->getChainedNameIV() : false;
}

string DirNode::rootDirectory() const {
  // don't update last access here, otherwise 'du' would cause lastAccess to
  // be reset.
  return rootDir;
}

Path DirNode::appendToRoot(const NameIOPath &path) const {
  // add encoded components back against the rootdir
  // to create the path
  auto toret = rootDir;
  for (const std::string &comp : path) {
    toret = toret.join(comp);
  }

  return std::move(toret);
}

static bool startswith(const std::string &a, const std::string &b) {
  if (a.size() < b.size()) return false;
  return !strncmp(a.c_str(), b.c_str(), b.length());
}

std::string DirNode::cipherPath(const char *plaintextPath) const {
  return cipherPath(fs_io->pathFromString(plaintextPath));
}

NameIOPath DirNode::pathToRelativeNameIOPath(const Path &plaintextPath) const {
  if (!path_is_parent(rootDir, plaintextPath) && rootDir != plaintextPath) {
    throw std::runtime_error(
        "bad path! \"" + (const std::string &)plaintextPath +
        "\" is not a child of \"" + (const std::string &)rootDir + "\"");
  }

  // turn those components of `plaintextPath` that come after `rootDir`
  // into a sequence suitable for naming->encodePath
  auto p = plaintextPath;
  NameIOPath pp;
  while (rootDir != p) {
    pp.push_back(p.basename());
    p = p.dirname();
  }
  pp.reverse();

  return std::move(pp);
}

Path DirNode::cipherPath(const Path &plaintextPath, uint64_t *iv) const {
  uint64_t iv2 = 0;
  if (!iv) iv = &iv2;

  // iv should be a zero value since this method only works
  // for the starting path
  assert(!*iv);

  auto pp = pathToRelativeNameIOPath(plaintextPath);

  auto parent_encrypted_path = rootDir;
  for (const auto &p : naming->encodePath(pp, iv)) {
    parent_encrypted_path = parent_encrypted_path.join(p);
  }

  return parent_encrypted_path;
}

Path DirNode::apiToInternal(const char *plaintextPath, uint64_t *iv) const {
  uint64_t iv2 = 0;
  if (!iv) iv = &iv2;
  return cipherPath(fs_io->pathFromString(plaintextPath), iv);
}

static string nameIOPathToRelativePosixPath(const NameIOPath &p) {
  return std::accumulate(p.begin(), p.end(), string(),
                         [](const std::string &acc, const std::string &elt) {
    return acc + "/" + elt;
  });
}

static NameIOPath posixPathToNameIOPath(const string &p) {
  NameIOPath toret;

  for (auto pit = p.cbegin(); pit != p.cend();) {
    if (*pit == '/') {
      ++pit;
      continue;
    }

    auto sit = std::find(pit, p.cend(), '/');
    toret.push_back(string(pit, sit));
    pit = sit;
  }

  return std::move(toret);
}

std::string DirNode::cipherPathWithoutRootPosix(std::string plaintextPath)
    const {
  return "+" + nameIOPathToRelativePosixPath(naming->encodePath(
                   posixPathToNameIOPath(std::move(plaintextPath))));
}

string DirNode::plainPathPosix(const char *cipherPath_) {
  // NB: this method only works when our base file system
  //     is posix!  (due to hardcoding of '/')

  try {
    if (startswith(cipherPath_, (const std::string &)rootDir + '/')) {
      auto decoded_path = naming->decodePath(
          pathToRelativeNameIOPath(fs_io->pathFromString(cipherPath_)));
      return nameIOPathToRelativePosixPath(std::move(decoded_path));
    } else {
      const char *start = cipherPath_;
      auto to_prepend = std::string("");

      // if the first character is "+" that means this is an absolute path
      if (start[0] == '+') {
        ++start;
        to_prepend = "/";
      }

      auto niopath = posixPathToNameIOPath(start);
      return to_prepend +
             nameIOPathToRelativePosixPath(naming->decodePath(niopath));
    }
  }
  catch (Error &err) {
    LOG(LERROR) << "decode err: " << err.what();
    return string();
  }
}

PosixSymlinkData DirNode::decryptLinkPath(PosixSymlinkData in) {
  auto buf = plainPathPosix(in.c_str());
  return PosixSymlinkData(std::move(buf));
}

string DirNode::relativeCipherPathPosix(const char *plaintextPath) {
  // NB: this method only works when our base file system
  //     is posix! (due to hardcoding of '/')
  try {
    return cipherPathWithoutRootPosix(plaintextPath);
  }
  catch (Error &err) {
    LOG(LERROR) << "encode err: " << err.what();
    return string();
  }
}

DirTraverse DirNode::openDir(const char *plaintextPath) const {
  uint64_t iv = 0;
  opt::optional<Path> maybeCyName;

  try {
    maybeCyName = apiToInternal(plaintextPath, &iv);
  }
  catch (Error &err) {
    LOG(LERROR) << "encode err: " << err.what();
    return DirTraverse();
  }

  assert(maybeCyName);

  opt::optional<Directory> dir_io;
  const int res = withExceptionCatcher((int)std::errc::io_error,
                                       bindMethod(fs_io, &FsIO::opendir),
                                       &dir_io, *maybeCyName);
  if (res < 0) return DirTraverse();

  assert(dir_io);

  return DirTraverse(std::move(*dir_io), iv, naming);
}

bool DirNode::genRenameList(list<RenameEl> &renameList, const Path &fromP,
                            const Path &toP) {
  uint64_t fromIV = 0, toIV = 0;

  // compute the IV for both paths
  auto sourcePath = cipherPath(fromP, &fromIV);
  cipherPath(toP, &toIV);

  // ok..... we wish it was so simple.. should almost never happen
  if (fromIV == toIV) return true;

  // generate the real destination path, where we expect to find the files..
  LOG(INFO) << "opendir " << sourcePath;

  opt::optional<Directory> dir_io;
  try {
    dir_io = fs_io->opendir(sourcePath);
  }
  catch (const Error &err) {
    LOG(WARNING) << "opendir(" << sourcePath << ") failed: " << err.what();
    return false;
  }

  while (true) {
    opt::optional<FsDirEnt> dir_ent;
    try {
      dir_ent = dir_io->readdir();
    }
    catch (const Error &err) {
      LOG(WARNING) << "readdir(" << sourcePath << ") failed: " << err.what();
      return false;
    }

    if (!dir_ent) break;

    // decode the name using the oldIV
    uint64_t localIV = fromIV;
    opt::optional<NameIOPath> maybePlainName;

    try {
      maybePlainName = naming->decodePath({dir_ent->name}, &localIV);
    }
    catch (Error &ex) {
      // if filename can't be decoded, then ignore it..
      continue;
    }

    auto plainName = std::move(*maybePlainName);

    // any error in the following will trigger a rename failure.
    try {
      // re-encode using the new IV..
      localIV = toIV;
      auto newName = naming->encodePath(plainName, &localIV);

      // store rename information..
      auto oldFull = sourcePath.join(dir_ent->name);
      auto newFull = sourcePath.join(newName.front());

      auto isDirectory_ = dir_ent->type == opt::nullopt
                              ? isDirectory(fs_io, oldFull.c_str())
                              : *dir_ent->type == FsFileType::DIRECTORY;

      RenameEl ren = {
          std::move(oldFull),            std::move(newFull),
          fromP.join(plainName.front()), toP.join(plainName.front()),
          isDirectory_};

      if (ren.isDirectory) {
        // recurse..  We want to add subdirectory elements before the
        // parent, as that is the logical rename order..
        if (!genRenameList(renameList, ren.oldPName, ren.newPName)) {
          return false;
        }
      }

      LOG(INFO) << "adding file " << ren.oldCName << " to rename list";
      renameList.push_back(std::move(ren));
    }
    catch (Error &err) {
      // We can't convert this name, because we don't have a valid IV for
      // it (or perhaps a valid key).. It will be inaccessible..
      LOG(WARNING) << "Aborting rename: error on file "
                   << sourcePath.join(dir_ent->name) << ":" << err.what();

      // abort.. Err on the side of safety and disallow rename, rather
      // then loosing files..
      return false;
    }
  }

  return true;
}

/*
    A bit of a pain.. If a directory is renamed in a filesystem with
    directory initialization vector chaining, then we have to recursively
    rename every descendent of this directory, as all initialization vectors
    will have changed..

    Returns a list of renamed items on success, a null list on failure.
*/
shared_ptr<RenameOp> DirNode::newRenameOp(const Path &fromP, const Path &toP) {
  // Do the rename in two stages to avoid chasing our tail
  // Undo everything if we encounter an error!
  shared_ptr<list<RenameEl>> renameList(new list<RenameEl>);
  if (!genRenameList(*renameList.get(), fromP, toP)) {
    LOG(WARNING) << "Error during generation of recursive rename list";
    return shared_ptr<RenameOp>();
  } else
    return std::make_shared<RenameOp>(this, renameList);
}

int DirNode::posix_mkdir(const char *plaintextPath, fs_posix_mode_t mode) {
  auto cyName = apiToInternal(plaintextPath);

  LOG(INFO) << "mkdir on " << cyName;

  return withExceptionCatcherNoRet((int)std::errc::io_error,
                                   bindMethod(fs_io, &FsIO::posix_mkdir),
                                   std::move(cyName), mode);
}

int DirNode::rename(const char *cfromPlaintext, const char *ctoPlaintext) {
  Lock _lock(mutex);

  auto fromPlaintext = fs_io->pathFromString(cfromPlaintext);
  auto toPlaintext = fs_io->pathFromString(ctoPlaintext);

  auto fromCName = apiToInternal(cfromPlaintext);
  auto toCName = apiToInternal(ctoPlaintext);

  LOG(INFO) << "rename " << fromCName << " -> " << toCName;

  shared_ptr<RenameOp> renameOp;
  if (hasDirectoryNameDependency() && isDirectory(fs_io, fromCName.c_str())) {
    LOG(INFO) << "recursive rename begin";
    renameOp = newRenameOp(fromPlaintext, toPlaintext);

    if (!renameOp || !renameOp->apply()) {
      if (renameOp) renameOp->undo();

      LOG(WARNING) << "rename aborted";
      return -(int)std::errc::permission_denied;
    }
    LOG(INFO) << "recursive rename end";
  }

  int res = 0;
  try {
    fs_time_t old_mtime;
    bool preserve_mtime = true;
    try {
      old_mtime = encfs::get_attrs(fs_io, fromCName).mtime;
    }
    catch (const std::exception &err) {
      LOG(WARNING) << "get_mtime error: " << err.what();
      preserve_mtime = false;
    }

    renameNode(fromPlaintext, toPlaintext);
    res = withExceptionCatcherNoRet((int)std::errc::io_error,
                                    bindMethod(fs_io, &FsIO::rename), fromCName,
                                    toCName);
    if (res < 0) {
      // undo
      renameNode(toPlaintext, fromPlaintext, false);

      if (renameOp) renameOp->undo();
    } else if (preserve_mtime) {
      withExceptionCatcherNoRet((int)std::errc::io_error,
                                bindMethod(fs_io, &FsIO::set_times), toCName,
                                opt::nullopt, old_mtime);
    }
  }
  catch (Error &err) {
    // exception from renameNode, just show the error and continue..
    LOG(LERROR) << "rename err: " << err.what();
    res = -(int)std::errc::io_error;
  }

  return res;
}

int DirNode::posix_link(const char *from, const char *to) {
  Lock _lock(mutex);

  auto fromCName = apiToInternal(from);
  auto toCName = apiToInternal(to);

  LOG(INFO) << "link " << fromCName << " -> " << toCName;

  int res = 0;
  if (fsConfig->config->external_iv()) {
    LOG(INFO) << "hard links not supported with external IV chaining!";
    res = -(int)std::errc::operation_not_permitted;
  } else {
    res = withExceptionCatcherNoRet((int)std::errc::io_error,
                                    bindMethod(fs_io, &FsIO::posix_link),
                                    fromCName, toCName);
  }

  return res;
}

/*
    The node is keyed by filename, so a rename means the internal node names
    must be changed.
*/
shared_ptr<FileNode> DirNode::renameNode(const Path &from, const Path &to,
                                         bool forwardMode) {
  if (ctx && ctx->lookupNode(to.c_str())) {
    LOG(WARNING) << "Refusing to rename over open file";
    throw Error("won't rename over open file");
  }

  shared_ptr<FileNode> node = findOrCreate(from);

  if (node) {
    uint64_t newIV = 0;
    auto cname = cipherPath(to, &newIV);

    LOG(INFO) << "renaming internal node " << node->cipherName() << " -> "
              << cname;

    if (!node->setName(to, cname, newIV, forwardMode)) {
      // rename error! - put it back
      LOG(LERROR) << "renameNode failed";
      throw Error("Internal node name change failed!");
    }
  }

  return node;
}

shared_ptr<FileNode> DirNode::findOrCreate(const Path &plainName) {
  shared_ptr<FileNode> node;
  if (ctx) node = ctx->lookupNode(plainName.c_str());

  if (!node) {
    uint64_t iv = 0;
    auto cipherName = cipherPath(plainName, &iv);
    node = std::make_shared<FileNode>(ctx, fsConfig, plainName, cipherName);

    node->setName(opt::nullopt, opt::nullopt, iv);

    // add weak reference to node
    ctx->trackNode(plainName.c_str(), node);

    LOG(INFO) << "created FileNode for " << node->cipherName();
  }

  return node;
}

shared_ptr<FileNode> DirNode::lookupNode(const char *plainName,
                                         const char * /*requestor*/) {
  Lock _lock(mutex);
  return findOrCreate(fs_io->pathFromString(plainName));
}

shared_ptr<FileNode> DirNode::_openNode(const Path &plainName,
                                        const char * /*requestor*/,
                                        bool requestWrite, bool createFile,
                                        int *result) {
  shared_ptr<FileNode> node = findOrCreate(plainName);
  if (node)
    *result = node->open(requestWrite, createFile);
  else
    *result = -(int)std::errc::io_error;

  if (*result < 0) node = nullptr;

  return node;
}

/*
    Similar to lookupNode, except that we also call open() and only return a
    node on sucess..  This is done in one step to avoid any race conditions
    with the stored state of the file.
*/
shared_ptr<FileNode> DirNode::openNode(const char *plainName,
                                       const char *requestor, bool requestWrite,
                                       bool createFile, int *result) {
  rAssert(result != NULL);
  Lock _lock(mutex);

  auto p = pathFromString(plainName);

  return _openNode(p, requestor, requestWrite, createFile, result);
}

int DirNode::unlink(const char *plaintextName) {
  auto cyName = apiToInternal(plaintextName);
  LOG(INFO) << "unlink " << cyName;

  Lock _lock(mutex);

  int res = 0;
  if (ctx && ctx->lookupNode(plaintextName)) {
    // If FUSE is running with "hard_remove" option where it doesn't
    // hide open files for us, then we can't allow an unlink of an open
    // file..
    LOG(WARNING) << "Refusing to unlink open file: " << cyName
                 << ", hard_remove option is probably in effect";
    res = -(int)std::errc::device_or_resource_busy;
  } else {
    res = withExceptionCatcherNoRet((int)std::errc::io_error,
                                    bindMethod(fs_io, &FsIO::unlink), cyName);
    if (res < 0) {
      LOG(INFO) << "unlink error: "
                << std::make_error_condition(static_cast<std::errc>(-res))
                       .message();
    }
  }

  return res;
}

int DirNode::mkdir(const char *plaintextName) {
  Lock _lock(mutex);
  auto fullName = apiToInternal(plaintextName);
  return withExceptionCatcherNoRet((int)std::errc::io_error,
                                   bindMethod(fs_io, &FsIO::mkdir), fullName);
}

FsFileAttrs DirNode::correct_attrs(FsFileAttrs attrs) const {
  // TODO: make sure this wrap code is similar to how FileIO is created
  // in FileNode
  attrs = CipherFileIO::wrapAttrs(fsConfig, std::move(attrs));

  if (fsConfig->config->block_mac_bytes() ||
      fsConfig->config->block_mac_rand_bytes()) {
    attrs = MACFileIO::wrapAttrs(fsConfig, std::move(attrs));
  }

  return attrs;
}

int DirNode::get_attrs(FsFileAttrs *attrs, const char *plaintextName) const {
  Lock _lock(mutex);

  auto cyName = apiToInternal(plaintextName);
  LOG(INFO) << "get_attrs " << cyName;

  auto ret = withExceptionCatcher((int)std::errc::io_error,
                                  encfs::get_attrs<decltype(fs_io)>, attrs,
                                  fs_io, cyName);
  if (ret < 0) return ret;

  *attrs = correct_attrs(*attrs);

  return ret;
}

int DirNode::posix_stat(FsFileAttrs *attrs, const char *plaintextName,
                        bool follow) {
  Lock _lock(mutex);

  auto cyName = apiToInternal(plaintextName);
  LOG(INFO) << "posix_stat " << cyName;

  int ret = withExceptionCatcher((int)std::errc::io_error,
                                 bindMethod(fs_io, &FsIO::posix_stat), attrs,
                                 cyName, follow);
  if (ret < 0) return ret;

  *attrs = correct_attrs(*attrs);

  // this should exist since we're calling posix_stat
  assert(attrs->posix);

  if (posix_is_symlink(attrs->posix->mode)) {
    opt::optional<PosixSymlinkData> buf;
    ret = withExceptionCatcher((int)std::errc::io_error,
                               bindMethod(this, &DirNode::_posix_readlink),
                               &buf, cyName);
    if (ret < 0) return ret;
    assert(buf);
    attrs->size = buf->size();
  }

  return ret;
}

PosixSymlinkData DirNode::_posix_readlink(const Path &cyPath) {
  auto link_buf = fs_io->posix_readlink(cyPath);
  // decrypt link buf
  return decryptLinkPath(std::move(link_buf));
}

int DirNode::posix_readlink(PosixSymlinkData *buf, const char *plaintextName) {
  Lock _lock(mutex);

  auto cyName = apiToInternal(plaintextName);
  return withExceptionCatcher((int)std::errc::io_error,
                              bindMethod(this, &DirNode::_posix_readlink), buf,
                              cyName);
}

int DirNode::posix_symlink(const char *path, const char *data) {
  // allow fully qualified names in symbolic links.
  auto toCName = apiToInternal(path);
  auto fromCName = relativeCipherPathPosix(data);

  LOG(INFO) << "symlink " << fromCName << " -> " << toCName;

  return withExceptionCatcherNoRet(
      (int)std::errc::io_error, bindMethod(fs_io, &FsIO::posix_symlink),
      std::move(toCName), PosixSymlinkData(std::move(fromCName)));
}

Path DirNode::pathFromString(const std::string &string) const {
  return fs_io->pathFromString(string);
}

int DirNode::posix_create(shared_ptr<FileNode> *fnode, const char *plainName,
                          fs_posix_mode_t mode) {
  rAssert(fnode);
  Lock _lock(mutex);

  // first call posix_create to clear node and whatever else
  auto cyName = apiToInternal(plainName);
  int ret = withExceptionCatcherNoRet((int)std::errc::io_error,
                                      bindMethod(fs_io, &FsIO::posix_create),
                                      cyName, mode);
  if (ret < 0) return ret;

  // then open node as usual, this is fine since the fs is locked during this
  const bool requestWrite = true;
  const bool createFile = true;
  *fnode = _openNode(fs_io->pathFromString(plainName), "posix_create",
                     requestWrite, createFile, &ret);
  if (!*fnode) return ret;
  return 0;
}

int DirNode::posix_mknod(const char *plaintextName, fs_posix_mode_t mode,
                         fs_posix_dev_t dev) {
  Lock _lock(mutex);

  auto fullName = apiToInternal(plaintextName);
  return withExceptionCatcherNoRet((int)std::errc::io_error,
                                   bindMethod(fs_io, &FsIO::posix_mknod),
                                   fullName, mode, dev);
}

int DirNode::posix_setfsgid(fs_posix_gid_t *oldgid, fs_posix_gid_t newgid) {
  Lock _lock(mutex);

  return withExceptionCatcher((int)std::errc::io_error,
                              bindMethod(fs_io, &FsIO::posix_setfsgid), oldgid,
                              newgid);
}

int DirNode::posix_setfsuid(fs_posix_uid_t *olduid, fs_posix_uid_t newuid) {
  Lock _lock(mutex);
  return withExceptionCatcher((int)std::errc::io_error,
                              bindMethod(fs_io, &FsIO::posix_setfsuid), olduid,
                              newuid);
}

int DirNode::rmdir(const char *plaintextName) {
  Lock _lock(mutex);
  auto fullName = apiToInternal(plaintextName);
  return withExceptionCatcherNoRet((int)std::errc::io_error,
                                   bindMethod(fs_io, &FsIO::rmdir), fullName);
}

int DirNode::set_times(const char *plaintextName,
                       opt::optional<fs_time_t> atime,
                       opt::optional<fs_time_t> mtime) {
  Lock _lock(mutex);
  auto fullName = apiToInternal(plaintextName);
  return withExceptionCatcherNoRet(
      (int)std::errc::io_error, bindMethod(fs_io, &FsIO::set_times), fullName,
      std::move(atime), std::move(mtime));
}

int DirNode::posix_chmod(const char *plaintextName, bool follow,
                         fs_posix_mode_t mode) {
  Lock _lock(mutex);

  auto fullName = apiToInternal(plaintextName);
  return withExceptionCatcherNoRet((int)std::errc::io_error,
                                   bindMethod(fs_io, &FsIO::posix_chmod),
                                   fullName, follow, mode);
}

int DirNode::posix_chown(const char *plaintextName, bool follow,
                         fs_posix_uid_t uid, fs_posix_gid_t gid) {
  Lock _lock(mutex);

  auto fullName = apiToInternal(plaintextName);
  return withExceptionCatcherNoRet((int)std::errc::io_error,
                                   bindMethod(fs_io, &FsIO::posix_chown),
                                   fullName, follow, uid, gid);
}

int DirNode::posix_setxattr(const char *plaintextName, bool follow,
                            std::string name, size_t offset,
                            std::vector<byte> buf, PosixSetxattrFlags flags) {
  Lock _lock(mutex);

  auto fullName = apiToInternal(plaintextName);
  return withExceptionCatcherNoRet((int)std::errc::io_error,
                                   bindMethod(fs_io, &FsIO::posix_setxattr),
                                   std::move(fullName), follow, std::move(name),
                                   offset, std::move(buf), std::move(flags));
}

int DirNode::posix_getxattr(opt::optional<std::vector<byte>> *ret,
                            const char *plaintextName, bool follow,
                            std::string name, size_t offset, size_t amt) {
  Lock _lock(mutex);

  auto fullName = apiToInternal(plaintextName);
  return withExceptionCatcher(
      (int)std::errc::io_error, bindMethod(fs_io, &FsIO::posix_getxattr), ret,
      std::move(fullName), follow, std::move(name), offset, amt);
}

int DirNode::posix_listxattr(opt::optional<PosixXattrList> *ret,
                             const char *plaintextName, bool follow) {
  Lock _lock(mutex);

  auto fullName = apiToInternal(plaintextName);
  return withExceptionCatcher((int)std::errc::io_error,
                              bindMethod(fs_io, &FsIO::posix_listxattr), ret,
                              std::move(fullName), follow);
}

int DirNode::posix_removexattr(const char *plaintextName, bool follow,
                               std::string name) {
  Lock _lock(mutex);

  auto fullName = apiToInternal(plaintextName);
  return withExceptionCatcherNoRet(
      (int)std::errc::io_error, bindMethod(fs_io, &FsIO::posix_removexattr),
      std::move(fullName), follow, std::move(name));
}

}  // namespace encfs
