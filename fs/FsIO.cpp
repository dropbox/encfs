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

#include <iostream>

#include "fs/FsIO.h"

namespace encfs {

std::ostream &operator<<(std::ostream &os, const Path &s) {
  os << "Path(\"" << (const std::string &)s << "\")";
  return os;
}

bool path_is_parent(Path potential_parent, Path potential_child) {
  auto cur_path = potential_child.dirname();
  while (!cur_path.is_root()) {
    if (potential_parent == cur_path) return true;
    cur_path = cur_path.dirname();
  }

  return cur_path == potential_parent;
}

PathPoly::~PathPoly() {}

DirectoryIO::~DirectoryIO() {}

FsIO::~FsIO() {}

fs_posix_uid_t FsIO::posix_setfsuid(fs_posix_uid_t /*uid*/) {
  throw create_errno_system_error(std::errc::function_not_supported);
}

fs_posix_gid_t FsIO::posix_setfsgid(fs_posix_gid_t /*uid*/) {
  throw create_errno_system_error(std::errc::function_not_supported);
}

File FsIO::posix_create(const Path & /*pathSrc*/, fs_posix_mode_t /* mode */) {
  throw create_errno_system_error(std::errc::function_not_supported);
}

void FsIO::posix_mkdir(const Path & /*path*/, fs_posix_mode_t /* mode*/) {
  throw create_errno_system_error(std::errc::function_not_supported);
}

void FsIO::posix_mknod(const Path & /*path*/, fs_posix_mode_t /* mode*/,
                       fs_posix_dev_t /* dev*/) {
  throw create_errno_system_error(std::errc::function_not_supported);
}

void FsIO::posix_link(const Path & /*pathSrc*/, const Path & /*pathDst*/) {
  throw create_errno_system_error(std::errc::function_not_supported);
}

void FsIO::posix_symlink(const Path & /*path*/,
                         PosixSymlinkData /*link_data*/) {
  throw create_errno_system_error(std::errc::function_not_supported);
}

PosixSymlinkData FsIO::posix_readlink(const Path & /*path*/) const {
  throw create_errno_system_error(std::errc::function_not_supported);
}

void FsIO::posix_mkfifo(const Path & /*path*/, fs_posix_mode_t /*mode*/) {
  throw create_errno_system_error(std::errc::function_not_supported);
}

void FsIO::posix_chmod(const Path & /*pathSrc*/, bool /*follow*/,
                       fs_posix_mode_t /*mode*/) {
  throw create_errno_system_error(std::errc::function_not_supported);
}

void FsIO::posix_chown(const Path & /*pathSrc*/, bool /*follow*/,
                       fs_posix_uid_t /*uid*/, fs_posix_gid_t /*gid*/) {
  throw create_errno_system_error(std::errc::function_not_supported);
}

void FsIO::posix_setxattr(const Path & /*path*/, bool /*follow*/,
                          std::string /*name*/, size_t /*offset*/,
                          std::vector<byte> /*buf*/,
                          PosixSetxattrFlags /*flags*/) {
  throw create_errno_system_error(std::errc::function_not_supported);
}

std::vector<byte> FsIO::posix_getxattr(const Path & /*path*/, bool /*follow*/,
                                       std::string /*name*/, size_t /*offset*/,
                                       size_t /*amt*/) const {
  throw create_errno_system_error(std::errc::function_not_supported);
}

PosixXattrList FsIO::posix_listxattr(const Path & /*path*/,
                                     bool /*follow*/) const {
  throw create_errno_system_error(std::errc::function_not_supported);
}

void FsIO::posix_removexattr(const Path & /*path*/, bool /*follow*/,
                             std::string /*name*/) {
  throw create_errno_system_error(std::errc::function_not_supported);
}

FsFileAttrs FsIO::posix_stat(const Path & /*path*/, bool /*follow*/) const {
  throw create_errno_system_error(std::errc::function_not_supported);
}

}  // namespace encfs
