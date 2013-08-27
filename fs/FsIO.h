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

#ifndef _FsIO_incl_
#define _FsIO_incl_

#include <cassert>

/* libstdc++ on mac doesn't have cstdint */
#include <stdint.h>

namespace encfs {

typedef uintmax_t encfs_mode_t;
typedef uintmax_t encfs_uid_t;
typedef uintmax_t encfs_gid_t;
typedef uintmax_t encfs_ino_t;
typedef uintptr_t encfs_dir_handle_t;
typedef intmax_t encfs_time_t;

enum {
  ENCFS_TIME_MIN=INTMAX_MIN,
  ENCFS_TIME_MAX=INTMAX_MAX,
};

typedef enum {
  ENCFS_FILE_TYPE_UNKNOWN,
  ENCFS_FILE_TYPE_DIRECTORY,
} encfs_file_type_t;

class FsIO
{
public:
    virtual int opendir( const char *plaintDirName, encfs_dir_handle_t *handle  ) =0;
    virtual int readdir( encfs_dir_handle_t handle, char **name,
                         encfs_file_type_t *type, encfs_ino_t *ino ) =0;
    virtual int closedir( encfs_dir_handle_t handle ) =0;

    virtual int mkdir( const char *plaintextPath, encfs_mode_t mode,
                       encfs_uid_t uid = 0, encfs_gid_t gid = 0) =0;

    virtual int rename( const char *fromPlaintext, const char *toPlaintext ) =0;

    virtual int link( const char *from, const char *to ) =0;

    virtual int unlink( const char *plaintextName ) =0;

    virtual int get_mtime( const char *path, encfs_time_t *mtime ) =0;
    virtual int set_mtime( const char *path, encfs_time_t mtime ) =0;

    virtual const char *strerror( int err ) =0;
};

/* RH:
 * Please don't judge me because this is called "Factory." I am not even
 * a Java programmer. This seemed to be the best way to allow DirNode to
 * use different "raw" FsIO implementations without refactoring it
 * significantly, e.g. by using templates.
 */
class FsIOFactory
{
public:
    virtual FsIO *operator() ( ) =0;
};

template<typename T>
class TemplateFsIOFactory : public FsIOFactory
{
public:
    FsIO *operator() ( ) override
    {
        return new T();
    }
};


}  // namespace encfs

#endif

