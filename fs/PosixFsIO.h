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

#ifndef _PosixFsIO_incl_
#define _PosixFsIO_incl_

#include <inttypes.h>

#include "fs/FsIO.h"

namespace encfs {

class PosixFsIO : public FsIO
{
public:
    virtual FsError opendir(const char *path, fs_dir_handle_t *handle) override;
    virtual FsError readdir(fs_dir_handle_t handle, char **name,
                            FsFileType *type, fs_posix_ino_t *ino) override;
    virtual FsError closedir(fs_dir_handle_t handle) override;

    virtual FsError mkdir(const char *path, fs_posix_mode_t mode,
                          fs_posix_uid_t uid = 0, fs_posix_gid_t gid = 0) override;

    virtual FsError rename(const char *from_path, const char *to_path) override;

    virtual FsError link(const char *from_path, const char *to_path) override;

    virtual FsError unlink(const char *plaintextName ) override;

    virtual FsError get_mtime(const char *path, fs_time_t *mtime) override;
    virtual FsError set_mtime(const char *path, fs_time_t mtime) override;

    virtual FsError get_type(const char *path, FsFileType *filetype) override;

    virtual const char *path_sep() override;
    virtual bool is_valid_path(const char *) override;
};

FsError posixErrnoToFsError(int err);
int fsErrorToPosixErrno(FsError err);

}  // namespace encfs
#endif

