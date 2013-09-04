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

/* Low-level IO class */

/* TODO:
 * Move low-level File IO into this class,
 * Make FileNode/DirNode C++-like interfaces into this backend
 */

#ifndef _FsIO_incl_
#define _FsIO_incl_

#include <cassert>
#include <cstdint>

#include <map>

namespace encfs {

typedef uintptr_t fs_dir_handle_t;
typedef intmax_t fs_time_t;
typedef uintmax_t fs_posix_mode_t;
typedef uintmax_t fs_posix_uid_t;
typedef uintmax_t fs_posix_gid_t;
typedef uintmax_t fs_posix_ino_t;

enum {
  FS_TIME_MIN=INTMAX_MIN,
  FS_TIME_MAX=INTMAX_MAX,
};

enum class FsFileType {
  UNKNOWN,
  DIRECTORY,
  REGULAR,
};

enum class FsError {
  NONE,
  ACCESS,
  IO,
  BUSY,
  GENERIC,
};

const char *fsErrorString(FsError err);

static constexpr bool isError(FsError err)
{
  return err != FsError::NONE;
}

class FsIO
{
public:
    virtual FsError opendir(const char *plaintDirName, fs_dir_handle_t *handle) =0;
    virtual FsError readdir(fs_dir_handle_t handle, char **name,
                            FsFileType *type, fs_posix_ino_t *ino) =0;
    virtual FsError closedir(fs_dir_handle_t handle) =0;

    virtual FsError mkdir(const char *plaintextPath, fs_posix_mode_t mode,
                          fs_posix_uid_t uid = 0, fs_posix_gid_t gid = 0) =0;

    virtual FsError rename(const char *fromPlaintext, const char *toPlaintext) =0;

    virtual FsError link(const char *from, const char *to) =0;

    virtual FsError unlink(const char *plaintextName) =0;

    virtual FsError get_mtime(const char *path, fs_time_t *mtime) =0;
    virtual FsError set_mtime(const char *path, fs_time_t mtime) =0;

    virtual FsError get_type(const char *path, FsFileType *filetype) =0;

    virtual const char *path_sep() =0;
    virtual bool is_valid_path(const char *) =0;
};

}  // namespace encfs

#endif

