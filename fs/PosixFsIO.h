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
#include <memory>

#include "fs/FsIO.h"

namespace encfs {

/* forward declaration */
class PosixFsIO;

class PosixFsIO : public FsIO
{
public:
    virtual Path pathFromString(const std::string &path) override;

    virtual Directory opendir(const Path &path) override;
    virtual File openfile(const Path &path,
                          bool open_for_write = false,
                          bool create = false) override;

    virtual void mkdir(const Path &path,
                       fs_posix_mode_t mode = 0,
                       fs_posix_uid_t uid = 0,
                       fs_posix_gid_t gid = 0) override;

    virtual void rename(const Path &pathSrc, const Path &pathDst) override;

    virtual void link(const Path &pathSrc, const Path &pathDst) override;

    virtual void unlink(const Path &path) override;
    virtual void rmdir(const Path &path) override;

    virtual void set_mtime(const Path &path, fs_time_t mtime) override;

    virtual FsFileAttrs get_attrs(const Path &path) override;
};

}  // namespace encfs
#endif

