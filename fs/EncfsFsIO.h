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

#ifndef _EncfsFsIO_incl_
#define _EncfsFsIO_incl_

#include <cstdint>

#include <memory>
#include <vector>

#include "fs/Context.h"
#include "fs/FileUtils.h"
#include "fs/FsIO.h"
#include "fs/fsconfig.pb.h"

namespace encfs {

class EncfsFsIO : public FsIO {
  std::shared_ptr<EncFS_Context> ctx;

  std::shared_ptr<DirNode> getRoot() const;

 public:
  EncfsFsIO();
  virtual ~EncfsFsIO() override;

  // methods specific to EncfsFsIO
  void initFS(const std::shared_ptr<EncFS_Opts> &opts,
              opt::optional<EncfsConfig> oCfg);

  // generic fs methods
  virtual Path pathFromString(const std::string &path) const override;

  virtual Directory opendir(const Path &path) const override;
  virtual File openfile(const Path &path, bool open_for_write = false,
                        bool create = false) override;

  virtual void mkdir(const Path &path) override;

  virtual void rename(const Path &pathSrc, const Path &pathDst) override;

  virtual void unlink(const Path &path) override;
  virtual void rmdir(const Path &path) override;

  virtual FsFileAttrs get_attrs(const encfs::Path &path) const override;
  virtual void set_times(const Path &path,
                         const opt::optional<fs_time_t> &atime,
                         const opt::optional<fs_time_t> &mtime) override;

  // extra posix-specific methods, might throw an error at runtime
  // if not supported by the underlying fs
  virtual fs_posix_uid_t posix_setfsuid(fs_posix_uid_t uid) override;
  virtual fs_posix_gid_t posix_setfsgid(fs_posix_gid_t gid) override;
  virtual File posix_create(const Path &pathSrc, fs_posix_mode_t mode) override;
  virtual void posix_mkdir(const Path &path, fs_posix_mode_t mode) override;
  virtual void posix_mknod(const Path &path, fs_posix_mode_t mode,
                           fs_posix_dev_t dev) override;
  virtual void posix_link(const Path &pathSrc, const Path &pathDst) override;
  virtual void posix_symlink(const Path &path,
                             PosixSymlinkData pathDst) override;
  virtual PosixSymlinkData posix_readlink(const Path &path) const override;
  virtual void posix_chmod(const Path &pathSrc, bool follow,
                           fs_posix_mode_t mode) override;
  virtual void posix_chown(const Path &pathSrc, bool follow, fs_posix_uid_t uid,
                           fs_posix_gid_t gid) override;
  virtual void posix_setxattr(const Path &path, bool follow, std::string name,
                              size_t offset, std::vector<byte> buf,
                              PosixSetxattrFlags flags) override;
  virtual std::vector<byte> posix_getxattr(const Path &path, bool follow,
                                           std::string name, size_t offset,
                                           size_t amt) const override;
  virtual PosixXattrList posix_listxattr(const Path &path, bool follow) const
      override;
  virtual void posix_removexattr(const Path &path, bool follow,
                                 std::string name) override;
  virtual FsFileAttrs posix_stat(const Path &path, bool follow) const override;
};

}  // namespace encfs
#endif
