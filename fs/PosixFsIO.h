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
    PosixFsIO();
    virtual ~PosixFsIO();

    virtual int opendir( const char *path, encfs_dir_handle_t *handle  ) override;
    virtual int readdir( encfs_dir_handle_t handle, char **name,
                         encfs_file_type_t *type, encfs_ino_t *ino ) override;
    virtual int closedir( encfs_dir_handle_t handle ) override;

    virtual int mkdir( const char *path, encfs_mode_t mode,
                       encfs_uid_t uid = 0, encfs_gid_t gid = 0) override;

    virtual int rename( const char *from_path, const char *to_path ) override;

    virtual int link( const char *from_path, const char *to_path ) override;

    virtual int unlink( const char *plaintextName ) override;

    virtual int get_mtime( const char *path, encfs_time_t *mtime ) override;
    virtual int set_mtime( const char *path, encfs_time_t mtime ) override;

    virtual const char *strerror( int err ) override;

};

}  // namespace encfs

#endif

