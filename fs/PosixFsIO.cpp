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

#ifdef linux
#include <sys/fsuid.h>
#endif
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <dirent.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "fs/PosixFsIO.h"

namespace encfs {

static char *strdup_x(const char *s)
{
  size_t len = strlen(s);
  char *toret = (char *) malloc(len + 1);
  if (!toret) {
    return NULL;
  }
  return (char *) memcpy(toret, s, len + 1);
}

PosixFsIO::PosixFsIO( )
{
}

PosixFsIO::~PosixFsIO( )
{
}

int PosixFsIO::opendir( const char *dirname, encfs_dir_handle_t *handle )
{
  DIR *const res = ::opendir( dirname );
  if (!res) {
    return -errno;
  }
  *handle = (encfs_dir_handle_t) res;
  return 0;
}

int PosixFsIO::readdir( encfs_dir_handle_t handle, char **name,
                        encfs_file_type_t *type, encfs_ino_t *ino )
{
    DIR *const dir = (DIR *) handle;

    while (true) {
      errno = 0;
      struct dirent *const de = ::readdir( dir );
      if(!de) {
        if(errno) {
          return -errno;
        } else
        {
          *name = NULL;
          break;
        }
      }

      if((de->d_name[0] == '.') &&
         ((de->d_name[1] == '\0')
          || ((de->d_name[1] == '.') && (de->d_name[2] == '\0'))))
        {
          // skip "." and ".."
          continue;
        }

      *name = strdup_x( de->d_name );
      /* TODO: base on de->d_type */
      *type = ENCFS_FILE_TYPE_UNKNOWN;
      break;
    }

    return 0;
}

int PosixFsIO::closedir( encfs_dir_handle_t handle )
{
  DIR *const dir = (DIR *) handle;
  const int res_closdir = ::closedir(dir);
  return res_closdir < 0 ? -errno : 0;
}

int PosixFsIO::mkdir( const char *path, encfs_mode_t mode,
                      encfs_uid_t uid, encfs_gid_t gid)
{
#ifdef linux
  // if uid or gid are set, then that should be the directory owner
  int olduid = -1;
  int oldgid = -1;

  if(uid != 0)
    olduid = setfsuid( uid );
  if(gid != 0)
    oldgid = setfsgid( gid );
#endif

  const int res = ::mkdir( path, mode );

#ifdef linux
  if(olduid >= 0)
    setfsuid( olduid );
  if(oldgid >= 0)
    setfsgid( oldgid );
#endif

  return res < 0 ? -errno : res;
}

int PosixFsIO::rename( const char *from_path, const char *to_path )
{
  const int res = ::rename( from_path, to_path );
  return res < 0 ? -errno : res;
}

int PosixFsIO::link( const char *from_path, const char *to_path )
{
  const int res = ::link( from_path, to_path );
  return res < 0 ? -errno : res;
}

int PosixFsIO::unlink( const char *path )
{
  const int res = ::unlink( path );
  return res < 0 ? -errno : res;
}

int PosixFsIO::get_mtime( const char *path, encfs_time_t *mtime )
{
  struct stat st;
  const int res_stat = ::stat( path, &st );
  if (res_stat < 0) {
    return -errno;
  }

  assert( st.st_mtime >= ENCFS_TIME_MIN );
  assert( st.st_mtime <= ENCFS_TIME_MAX );
  *mtime = (encfs_time_t) st.st_mtime;
  return 0;
}

int PosixFsIO::set_mtime( const char *path, encfs_time_t mtime )
{
  struct timeval new_times[2] = {
    {0, 0},
    {mtime, 0},
  };

  const int res_gettimeofday = ::gettimeofday(&new_times[0], NULL);
  if (res_gettimeofday < 0) {
    return -errno;
  }

  const int res_utimes = ::utimes( path, new_times );
  if (res_utimes < 0) {
    return -errno;
  }

  return 0;
}

const char *PosixFsIO::strerror( int err )
{
  return ::strerror( err );
}

}  // namespace encfs
