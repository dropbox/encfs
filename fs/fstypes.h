/*****************************************************************************
 * Author:   Rian Hunter <rian@alum.mit.edu>
 *
 *****************************************************************************
 * Copyright (c) 2013, Rian Hunter
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

#include <memory>

namespace encfs {

typedef uintptr_t fs_dir_handle_t;
typedef intmax_t fs_time_t;
typedef intmax_t fs_off_t;
typedef intmax_t fs_file_id_t;

enum
{
  FS_TIME_MIN=INTMAX_MIN,
  FS_TIME_MAX=INTMAX_MAX,
};

enum class FsFileType
{
  UNKNOWN,
  DIRECTORY,
  REGULAR,
};

class FsExtraFileAttrs {
public:
  virtual ~FsExtraFileAttrs() {};
};

class FsFileAttrs
{
public:
  FsFileType type;
  fs_time_t mtime;
  fs_off_t size;
  std::unique_ptr<FsExtraFileAttrs> extra;
};

}  // namespace encfs

#endif
