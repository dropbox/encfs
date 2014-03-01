/*****************************************************************************
 * Author:   Rian Hunter <rian@alum.mit.edu>
 *
 *****************************************************************************
 * Copyright (c) 2013, Dropbox, Inc.
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

#ifndef _RootPathPrependFs_incl_
#define _RootPathPrependFs_incl_

#include <memory>
#include <vector>

#include "fs/FsIO.h"

namespace encfs {

class RootPathPrependFs : public FsIO {
  std::shared_ptr<FsIO> _base_fs;
  Path _oldRoot;
  Path _newRoot;

  Path transformPath(Path input) const {
    if (!path_is_parent(_oldRoot, input)) return std::move(input);

    std::vector<std::string> to_append;
    while (input != _oldRoot) {
      to_append.push_back(input.basename());
      input = input.dirname();
    }

    auto toret = _newRoot;
    for (auto it = to_append.rbegin(); it != to_append.rend();) {
      toret = toret.join(std::move(*it));
    }

    return std::move(toret);
  }

 public:
  RootPathPrependFs(std::shared_ptr<FsIO> base_fs, Path oldRoot, Path newRoot)
      : _base_fs(std::move(base_fs)),
        _oldRoot(std::move(oldRoot)),
        _newRoot(std::move(newRoot)) {}
  virtual ~RootPathPrependFs() override = default;

  // generic fs methods
  virtual const std::string &path_sep() const override {
    return _base_fs->path_sep();
  }

  virtual Path pathFromString(const std::string &path) const override {
    // we do all the transformation in the actual methods
    // since we don't make a new type for our paths
    return _base_fs->pathFromString(path);
  }

  virtual bool filename_equal(const std::string &a, const std::string &b) const
      override {
    return _base_fs->filename_equal(a, b);
  }

  virtual Directory opendir(const Path &path) const override {
    return _base_fs->opendir(transformPath(path));
  }

  virtual File openfile(const Path &path, bool open_for_write = false,
                        bool create = false) override {
    return _base_fs->openfile(transformPath(path), open_for_write, create);
  }

  virtual void mkdir(const Path &path) override {
    return _base_fs->mkdir(transformPath(path));
  }

  virtual void rename(const Path &pathSrc, const Path &pathDst) override {
    return _base_fs->rename(transformPath(pathSrc), transformPath(pathDst));
  }

  virtual void unlink(const Path &path) override {
    return _base_fs->unlink(transformPath(path));
  }

  virtual void rmdir(const Path &path) override {
    return _base_fs->rmdir(transformPath(path));
  }

  virtual void set_times(const Path &path,
                         const opt::optional<fs_time_t> &atime,
                         const opt::optional<fs_time_t> &mtime) override {
    return _base_fs->set_times(transformPath(path), atime, mtime);
  }

  // extra posix-specific methods, might throw an error at runtime
  // if not supported by the underlying fs
  virtual fs_posix_uid_t posix_setfsuid(fs_posix_uid_t uid) override {
    return _base_fs->posix_setfsuid(uid);
  }

  virtual fs_posix_gid_t posix_setfsgid(fs_posix_gid_t gid) override {
    return _base_fs->posix_setfsgid(gid);
  }

  virtual File posix_create(const Path &pathSrc,
                            fs_posix_mode_t mode) override {
    return _base_fs->posix_create(transformPath(pathSrc), mode);
  }

  virtual void posix_mkdir(const Path &path, fs_posix_mode_t mode) override {
    return _base_fs->posix_mkdir(transformPath(path), mode);
  }

  virtual void posix_mknod(const Path &path, fs_posix_mode_t mode,
                           fs_posix_dev_t dev) override {
    return _base_fs->posix_mknod(transformPath(path), mode, dev);
  }

  virtual void posix_link(const Path &pathSrc, const Path &pathDst) override {
    return _base_fs->posix_link(transformPath(pathSrc), transformPath(pathDst));
  }

  virtual void posix_symlink(const Path &path,
                             PosixSymlinkData pathDst) override {
    return _base_fs->posix_symlink(transformPath(path), std::move(pathDst));
  }

  virtual PosixSymlinkData posix_readlink(const Path &path) const override {
    return _base_fs->posix_readlink(transformPath(path));
  }

  virtual void posix_chmod(const Path &pathSrc, bool follow,
                           fs_posix_mode_t mode) override {
    return _base_fs->posix_chmod(transformPath(pathSrc), follow, mode);
  }

  virtual void posix_chown(const Path &pathSrc, bool follow, fs_posix_uid_t uid,
                           fs_posix_gid_t gid) override {
    return _base_fs->posix_chown(transformPath(pathSrc), follow, uid, gid);
  }

  virtual void posix_setxattr(const Path &path, bool follow, std::string name,
                              size_t offset, std::vector<byte> buf,
                              PosixSetxattrFlags flags) override {
    return _base_fs->posix_setxattr(transformPath(path), follow,
                                    std::move(name), offset, std::move(buf),
                                    std::move(flags));
  }

  virtual std::vector<byte> posix_getxattr(const Path &path, bool follow,
                                           std::string name, size_t offset,
                                           size_t amt) const override {
    return _base_fs->posix_getxattr(transformPath(path), follow,
                                    std::move(name), offset, amt);
  }

  virtual PosixXattrList posix_listxattr(const Path &path, bool follow) const
      override {
    return _base_fs->posix_listxattr(transformPath(path), follow);
  }

  virtual void posix_removexattr(const Path &path, bool follow,
                                 std::string name) override {
    return _base_fs->posix_removexattr(transformPath(path), follow,
                                       std::move(name));
  }

  virtual FsFileAttrs posix_stat(const Path &path, bool follow) const override {
    return _base_fs->posix_stat(transformPath(path), follow);
  }
};

}  // namespace encfs
#endif
