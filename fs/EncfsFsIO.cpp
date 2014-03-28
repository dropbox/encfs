/*****************************************************************************
 * Author:   Rian Hunter <rian@alum.mit.edu>
 *
 *****************************************************************************
 * Copyright (c) 2013, Dropbox, Inc.
 *
 * This program is free software; you can distribute it and/or modify it under
 * the terms of the GNU General Public License (GPL), as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

// Encrypted file system implementation
// TODO: this should merge with DirNode.cpp

#include "base/config.h"

#include "fs/EncfsFsIO.h"

#include "base/logging.h"
#include "base/util.h"

#include "fs/Context.h"
#include "fs/DirNode.h"
#include "fs/FileUtils.h"

#include <memory>
#include <string>

#include <cstring>

namespace encfs {

// private class
class EncfsDirectoryIO final : public DirectoryIO {
  DirTraverse _dt;

  // Only EncfsFsIO can instantiate this class
  EncfsDirectoryIO(DirTraverse dt);

 public:
  virtual ~EncfsDirectoryIO() override = default;
  virtual opt::optional<FsDirEnt> readdir() override;

  friend class EncfsFsIO;
};

// private class
class EncfsFileIO final : public FileIO {
  // Only EncfsFsIO can instantiate this class
  std::shared_ptr<FileNode> _fnode;
  bool _isWritable;

  EncfsFileIO(std::shared_ptr<FileNode> fnode, bool isWritable);

 public:
  virtual ~EncfsFileIO() override = default;

  virtual Interface interface() const override;

  virtual FsFileAttrs get_attrs() const override;

  virtual size_t read(const IORequest &req) const override;
  virtual void write(const IORequest &req) override;

  virtual void truncate(fs_off_t size) override;

  virtual bool isWritable() const override;

  virtual void sync(bool datasync) override;

  friend class EncfsFsIO;
};

EncfsDirectoryIO::EncfsDirectoryIO(DirTraverse dt) : _dt(std::move(dt)) {}

opt::optional<FsDirEnt> EncfsDirectoryIO::readdir() {
  FsFileType fileType = FsFileType::UNKNOWN;
  fs_file_id_t inode_i = 0;

  auto name = _dt.nextPlaintextName(&fileType, &inode_i);
  if (name.empty()) return opt::nullopt;

  auto toret = FsDirEnt(std::move(name), std::move(inode_i));
  if (fileType != FsFileType::UNKNOWN) toret.type = fileType;

  return std::move(toret);
}

static Interface EncfsFileIO_iface = makeInterface("FileIO/Encfs", 1, 0, 0);

EncfsFileIO::EncfsFileIO(std::shared_ptr<FileNode> fnode, bool isWritable)
    : _fnode(std::move(fnode)), _isWritable(std::move(isWritable)) {}

Interface EncfsFileIO::interface() const { return EncfsFileIO_iface; }

FsFileAttrs EncfsFileIO::get_attrs() const {
  FsFileAttrs attrs;
  zero_memory(attrs);
  int res = _fnode->getAttr(attrs);
  if (res < 0) throw create_errno_system_error(-res);
  return std::move(attrs);
}

size_t EncfsFileIO::read(const IORequest &req) const {
  ssize_t res = _fnode->read(req.offset, req.data, req.dataLen);
  if (res < 0) throw create_errno_system_error(-res);
  return (size_t)res;
}

void EncfsFileIO::write(const IORequest &req) {
  if (!isWritable())
    throw create_errno_system_error(std::errc::bad_file_descriptor);
  bool res = _fnode->write(req.offset, req.data, req.dataLen);
  if (!res) throw create_errno_system_error(std::errc::io_error);
}

void EncfsFileIO::truncate(fs_off_t size) {
  if (!isWritable())
    throw create_errno_system_error(std::errc::bad_file_descriptor);
  int res = _fnode->truncate(size);
  if (res < 0) throw create_errno_system_error(-res);
}

bool EncfsFileIO::isWritable() const { return _isWritable; }

void EncfsFileIO::sync(bool datasync) {
  int res = _fnode->sync(datasync);
  if (res < 0) throw create_errno_system_error(-res);
}

EncfsFsIO::EncfsFsIO() {}

void EncfsFsIO::initFS(const std::shared_ptr<EncFS_Opts> &opts,
                       opt::optional<EncfsConfig> oCfg) {
  ctx = std::make_shared<EncFS_Context>();
  bool throw_exception_on_bad_password = true;
  auto rootInfo =
      encfs::initFS(ctx, opts, oCfg, throw_exception_on_bad_password);

  if (rootInfo) {
    // set the globally visible root directory node
    // WARNING: THIS CREATES A CIRCULAR REFERENCE
    //          rootInfo->root already points to ctx by this point
    // => it's okay because we manually break the link from ctx to root
    //    in FileUtils.cpp::remountFS() and in ~EncfsFsIO
    ctx->setRoot(rootInfo->root);
  } else {
    throw std::runtime_error("couldn't create rootInfo");
  }
}

EncfsFsIO::~EncfsFsIO() {
  // need to manually reset root because of circular reference
  ctx->setRoot(nullptr);
}

std::shared_ptr<DirNode> EncfsFsIO::getRoot() const { return ctx->getRoot(); }

Path EncfsFsIO::pathFromString(const std::string &path) const {
  return ctx->getRoot()->pathFromString(path);
}

Directory EncfsFsIO::opendir(const Path &path) const {
  auto dt = getRoot()->openDir(path.c_str());
  if (!dt.valid()) throw create_errno_system_error(std::errc::io_error);
  return std::unique_ptr<EncfsDirectoryIO>(new EncfsDirectoryIO(std::move(dt)));
}

File EncfsFsIO::openfile(const Path &path, bool requestWrite, bool createFile) {
  int res;
  auto fnode =
      getRoot()->openNode(path.c_str(), "open", requestWrite, createFile, &res);
  if (!fnode) throw create_errno_system_error(-res);

  return std::unique_ptr<EncfsFileIO>(
      new EncfsFileIO(std::move(fnode), requestWrite));
}

void EncfsFsIO::mkdir(const Path &path) {
  const int res = getRoot()->mkdir(path.c_str());
  if (res < 0) throw create_errno_system_error(-res);
}

void EncfsFsIO::rename(const Path &pathSrc, const Path &pathDst) {
  const int res = getRoot()->rename(pathSrc.c_str(), pathDst.c_str());
  if (res < 0) throw create_errno_system_error(-res);
}

void EncfsFsIO::unlink(const Path &path) {
  const int res = getRoot()->unlink(path.c_str());
  if (res < 0) throw create_errno_system_error(-res);
}

void EncfsFsIO::rmdir(const Path &path) {
  const int res = getRoot()->rmdir(path.c_str());
  if (res < 0) throw create_errno_system_error(-res);
}

FsFileAttrs EncfsFsIO::get_attrs(const encfs::Path &path) const {
  FsFileAttrs attrs;
  zero_memory(attrs);
  const int res = getRoot()->get_attrs(&attrs, path.c_str());
  if (res < 0) throw create_errno_system_error(-res);
  return std::move(attrs);
}

void EncfsFsIO::set_times(const Path &path,
                          const opt::optional<fs_time_t> &atime,
                          const opt::optional<fs_time_t> &mtime) {
  const int res = getRoot()->set_times(path.c_str(), atime, mtime);
  if (res < 0) throw create_errno_system_error(-res);
}

fs_posix_uid_t EncfsFsIO::posix_setfsuid(fs_posix_uid_t uid) {
  fs_posix_uid_t olduid = 0;
  const int res = getRoot()->posix_setfsuid(&olduid, uid);
  if (res < 0) throw create_errno_system_error(-res);
  return olduid;
}

fs_posix_gid_t EncfsFsIO::posix_setfsgid(fs_posix_gid_t gid) {
  fs_posix_gid_t oldgid = 0;
  const int res = getRoot()->posix_setfsgid(&oldgid, gid);
  if (res < 0) throw create_errno_system_error(-res);
  return oldgid;
}

File EncfsFsIO::posix_create(const Path &path, fs_posix_mode_t mode) {
  std::shared_ptr<FileNode> fnode;
  int res = getRoot()->posix_create(&fnode, path.c_str(), mode);
  if (res < 0) throw create_errno_system_error(-res);

  assert(fnode);

  const bool requestWrite = true;
  return std::unique_ptr<EncfsFileIO>(
      new EncfsFileIO(std::move(fnode), requestWrite));
}

void EncfsFsIO::posix_mkdir(const Path &pathSrc, fs_posix_mode_t mode) {
  const int res = getRoot()->posix_mkdir(pathSrc.c_str(), mode);
  if (res < 0) throw create_errno_system_error(-res);
}

void EncfsFsIO::posix_mknod(const Path &pathSrc, fs_posix_mode_t mode,
                            fs_posix_dev_t dev) {
  const int res = getRoot()->posix_mknod(pathSrc.c_str(), mode, dev);
  if (res < 0) throw create_errno_system_error(-res);
}

void EncfsFsIO::posix_link(const Path &pathSrc, const Path &pathDst) {
  const int res = getRoot()->posix_link(pathSrc.c_str(), pathDst.c_str());
  if (res < 0) throw create_errno_system_error(-res);
}

void EncfsFsIO::posix_symlink(const Path &path, PosixSymlinkData link_data) {
  const int res = getRoot()->posix_symlink(path.c_str(), link_data.c_str());
  if (res < 0) throw create_errno_system_error(-res);
}

PosixSymlinkData EncfsFsIO::posix_readlink(const Path &path) const {
  PosixSymlinkData toret;
  const int res = getRoot()->posix_readlink(&toret, path.c_str());
  if (res < 0) throw create_errno_system_error(-res);

  return std::move(toret);
}

void EncfsFsIO::posix_chmod(const Path &path, bool follow,
                            fs_posix_mode_t mode) {
  const int res = getRoot()->posix_chmod(path.c_str(), follow, mode);
  if (res < 0) throw create_errno_system_error(-res);
}

void EncfsFsIO::posix_chown(const Path &path, bool follow, fs_posix_uid_t uid,
                            fs_posix_gid_t gid) {
  const int res = getRoot()->posix_chown(path.c_str(), follow, uid, gid);
  if (res < 0) throw create_errno_system_error(-res);
}

void EncfsFsIO::posix_setxattr(const Path &path, bool follow, std::string name,
                               size_t offset, std::vector<byte> buf,
                               PosixSetxattrFlags flags) {
  const int res =
      getRoot()->posix_setxattr(path.c_str(), follow, std::move(name), offset,
                                std::move(buf), std::move(flags));
  if (res < 0) throw create_errno_system_error(-res);
}

std::vector<byte> EncfsFsIO::posix_getxattr(const Path &path, bool follow,
                                            std::string name, size_t offset,
                                            size_t amt) const {
  opt::optional<std::vector<byte>> ret;
  const int res = getRoot()->posix_getxattr(&ret, path.c_str(), follow,
                                            std::move(name), offset, amt);
  if (res < 0) throw create_errno_system_error(-res);

  assert(ret);

  return *ret;
}

PosixXattrList EncfsFsIO::posix_listxattr(const Path &path, bool follow) const {
  opt::optional<PosixXattrList> ret;
  const int res = getRoot()->posix_listxattr(&ret, path.c_str(), follow);
  if (res < 0) throw create_errno_system_error(-res);

  assert(ret);

  return *ret;
}

void EncfsFsIO::posix_removexattr(const Path &path, bool follow,
                                  std::string name) {
  const int res = getRoot()->posix_removexattr(path.c_str(), follow, name);
  if (res < 0) throw create_errno_system_error(-res);
}

FsFileAttrs EncfsFsIO::posix_stat(const Path &path, bool follow) const {
  FsFileAttrs posix_attrs;
  zero_memory(posix_attrs);
  const int res = getRoot()->posix_stat(&posix_attrs, path.c_str(), follow);
  if (res < 0) throw create_errno_system_error(-res);
  return posix_attrs;
}
}
