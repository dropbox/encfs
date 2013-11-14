/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2003, Valient Gough
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

#ifndef _DirNode_incl_
#define _DirNode_incl_

#include <cstdint>

#include <map>
#include <memory>
#include <list>
#include <string>

#include "base/Mutex.h"
#include "base/optional.h"

#include "cipher/CipherKey.h"

#include "fs/FileNode.h"
#include "fs/NameIO.h"
#include "fs/FSConfig.h"
#include "fs/FsIO.h"

namespace encfs {

class Cipher;
class RenameOp;
struct RenameEl;
class EncFS_Context;

class DirTraverse {
 public:
  DirTraverse();
  DirTraverse(Directory &&dir_io, uint64_t iv,
              const std::shared_ptr<NameIO> &naming);
  DirTraverse(DirTraverse &&) = default;
  DirTraverse &operator=(DirTraverse &&) = default;

  DirTraverse(const DirTraverse &) = delete;
  DirTraverse &operator=(const DirTraverse &) = delete;

  // returns FALSE to indicate an invalid DirTraverse (such as when
  // an invalid directory is requested for traversal)
  bool valid() const;

  // return next plaintext filename
  // If fileType is not 0, then it is used to return the filetype
  // (or 0 if unknown)
  std::string nextPlaintextName(FsFileType *fileType = 0,
                                fs_file_id_t *inode = 0);

  // Return cipher name of next undecodable filename..
  // The opposite of nextPlaintextName(), as that skips undecodable names..
  std::string nextInvalid();

 private:
  opt::optional<Directory> dir_io;
  // initialization vector to use.  Not very general purpose, but makes it
  // more efficient to support filename IV chaining..
  uint64_t iv;
  std::shared_ptr<NameIO> naming;
};
inline bool DirTraverse::valid() const { return (bool)dir_io; }

class DirNode {
 public:
  // sourceDir points to where raw files are stored
  DirNode(std::shared_ptr<EncFS_Context> ctx, const std::string &sourceDir,
          const FSConfigPtr &config);
  ~DirNode();

  // return the path to the root directory
  std::string rootDirectory() const;

  // find files
  std::shared_ptr<FileNode> lookupNode(const char *plaintextName,
                                       const char *requestor);

  /*
      Combined lookupNode + node->open() call.  If the open fails, then the
      node is not retained.  If the open succeeds, then the node is returned.
  */
  std::shared_ptr<FileNode> openNode(const char *plaintextName,
                                     const char *requestor, bool requestWrite,
                                     bool createNode, int *openResult);

  std::string cipherPath(const char *plaintextPath) const;
  std::string cipherPathWithoutRootPosix(std::string plaintextPath) const;
  Path cipherPath(const Path &plaintextPath, uint64_t *iv = 0) const;
  NameIOPath pathToRelativeNameIOPath(const Path &plaintextPath) const;

  // relative cipherPath is the same as cipherPath except that it doesn't
  // prepent the mount point.  That it, it doesn't return a fully qualified
  // name, just a relative path within the encrypted filesystem.
  std::string relativeCipherPathPosix(const char *plaintextPath);
  std::string plainPathPosix(const char *cipherPath);

  /*
      Returns true if file names are dependent on the parent directory name.
      If a directory name is changed, then all the filenames must also be
      changed.
  */
  bool hasDirectoryNameDependency() const;

  // FS wrappers

  Path pathFromString(const std::string &string) const;

  DirTraverse openDir(const char *plainDirName) const;
  int get_attrs(FsFileAttrs *attrs, const char *plaintextName) const;
  int rename(const char *fromPlaintext, const char *toPlaintext);
  int unlink(const char *plaintextName);
  int mkdir(const char *plaintextName);
  int rmdir(const char *plaintextName);
  int set_times(const char *plaintextName, opt::optional<fs_time_t> atime,
                opt::optional<fs_time_t> mtime);

  int posix_link(const char *from, const char *to);
  int posix_mkdir(const char *plaintextPath, fs_posix_mode_t mode);
  int posix_mknod(const char *plaintextPath, fs_posix_mode_t mode,
                  fs_posix_dev_t dev);
  int posix_readlink(PosixSymlinkData *buf, const char *plaintextName);
  int posix_symlink(const char *path, const char *data);
  int posix_create(std::shared_ptr<FileNode> *fnode, const char *plainName,
                   fs_posix_mode_t mode);
  int posix_setfsgid(fs_posix_gid_t *oldgid, fs_posix_gid_t newgid);
  int posix_setfsuid(fs_posix_uid_t *olduid, fs_posix_uid_t newuid);
  int posix_chmod(const char *path, bool follow, fs_posix_mode_t mode);
  int posix_chown(const char *path, bool follow, fs_posix_uid_t uid,
                  fs_posix_gid_t gid);
  int posix_setxattr(const char *path, bool follow, std::string name,
                     size_t offset, std::vector<byte> buf,
                     PosixSetxattrFlags flags);
  int posix_getxattr(opt::optional<std::vector<byte>> *ret, const char *path,
                     bool follow, std::string name, size_t offset, size_t amt);
  int posix_listxattr(opt::optional<PosixXattrList> *ret, const char *path,
                      bool follow);
  int posix_removexattr(const char *path, bool follow, std::string name);
  int posix_stat(FsFileAttrs *posix_attrs, const char *plaintextName,
                 bool follow);

 protected:
  /*
      notify that a file is being renamed.
      This renames the internal node, if any.  If the file is not open, then
      this call has no effect.
      Returns the FileNode if it was found.
  */
  std::shared_ptr<FileNode> renameNode(const Path &from, const Path &to,
                                       bool forwardMode = true);
  /*
      when directory IV chaining is enabled, a directory can't be renamed
      without renaming all its contents as well.  recursiveRename should be
      called after renaming the directory, passing in the plaintext from and
      to paths.
  */
  std::shared_ptr<RenameOp> newRenameOp(const Path &from, const Path &to);

 private:
  friend class RenameOp;
  friend class DirTraverse;

  bool genRenameList(std::list<RenameEl> &list, const Path &fromP,
                     const Path &toP);

  std::shared_ptr<FileNode> findOrCreate(const Path &plainName);
  Path appendToRoot(const NameIOPath &path) const;
  Path apiToInternal(const char *plaintextPath, uint64_t *iv = 0) const;

  PosixSymlinkData decryptLinkPath(PosixSymlinkData in);
  PosixSymlinkData _posix_readlink(const Path &cyPath);
  std::shared_ptr<FileNode> _openNode(const Path &plainName,
                                      const char *requestor, bool requestWrite,
                                      bool createFile, int *result);
  FsFileAttrs correct_attrs(FsFileAttrs attrs) const;

  mutable Mutex mutex;

  std::shared_ptr<EncFS_Context> ctx;

  // passed in as configuration
  FSConfigPtr fsConfig;
  Path rootDir;

  std::shared_ptr<NameIO> naming;
  std::shared_ptr<FsIO> fs_io;
};

}  // namespace encfs

#endif
