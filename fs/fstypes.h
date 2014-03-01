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

#ifndef _fstypes_incl_
#define _fstypes_incl_

#include <cstdint>

#include <vector>
#include <memory>

#include "base/optional.h"

namespace encfs {

typedef intmax_t fs_time_t;
typedef intmax_t fs_off_t;
typedef uintmax_t fs_file_id_t;
typedef uintmax_t fs_volume_id_t;
typedef uintmax_t fs_posix_uid_t;
typedef uintmax_t fs_posix_gid_t;
typedef uintmax_t fs_posix_mode_t;
typedef uintmax_t fs_posix_dev_t;

enum class FsFileType {
  UNKNOWN,
  DIRECTORY,
  REGULAR,
};

struct FsPosixAttrs {
  fs_posix_mode_t mode;
  fs_posix_uid_t uid;
  fs_posix_gid_t gid;

  FsPosixAttrs(fs_posix_mode_t mode_, fs_posix_uid_t uid_, fs_posix_gid_t gid_)
      : mode(mode_), uid(uid_), gid(gid_) {}
};

struct FsFileAttrs {
  FsFileType type;
  fs_time_t mtime;
  fs_off_t size;
  fs_file_id_t file_id;
  fs_volume_id_t volume_id;
  opt::optional<FsPosixAttrs> posix;
};

inline bool posix_is_symlink(fs_posix_mode_t mode) { return mode & 0120000; }

// posix symlink data is just arbitrary 0-terminated data
typedef std::string PosixSymlinkData;

struct PosixSetxattrFlags {
  bool create : 1;
  bool replace : 1;

  PosixSetxattrFlags(bool create_ = false, bool replace_ = false)
      : create(create_), replace(replace_) {}
};

typedef std::vector<std::string> PosixXattrList;

}  // namespace encfs

#endif
