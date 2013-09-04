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

#include <map>
#include <stdexcept>
#include <vector>

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

class ErrMap
{
private:
  std::map<FsError, int> fsErrorToPosixErrnoMap;
  std::map<int, FsError> posixErrnoToFsErrorMap;

public:
  ErrMap(const std::vector< std::pair< FsError, std::vector<int> > > &masterErrorMap)
  {
    for(const auto & p : masterErrorMap)
    {
      for(const auto & errno_ : p.second)
      {
        assert( !this->fsErrorToPosixErrnoMap.count( p.first ) );
        assert( !this->posixErrnoToFsErrorMap.count( errno_ ) );
        this->fsErrorToPosixErrnoMap[p.first] = errno_;
        this->posixErrnoToFsErrorMap[errno_] = p.first;
      }
    }
  }

  FsError posixErrnoToFsError(int err) const
  {
    try
    {
      return this->posixErrnoToFsErrorMap.at( err );
    } catch ( const std::out_of_range &oor )
    {
      return FsError::GENERIC;
    }
  }

  int fsErrorToPosixErrno(FsError err) const
  {
    if(err == FsError::GENERIC)
    {
      return -EIO;
    }
    /* this should never throw an exception, if it does
       then it's programmer error
     */
    return this->fsErrorToPosixErrnoMap.at( err );
  }
};

static const ErrMap _privateErrMap(std::vector<std::pair<FsError, std::vector<int> > >({
  {FsError::NONE, {0}},
  {FsError::ACCESS, {EACCES}},
  {FsError::IO, {EIO}},
  {FsError::BUSY, {EBUSY}},
}));

FsError posixErrnoToFsError(int err)
{
  return _privateErrMap.posixErrnoToFsError(err);
}

int fsErrorToPosixErrno(FsError err)
{
  return _privateErrMap.fsErrorToPosixErrno(err);
}

static FsError currentFsError() {
  return posixErrnoToFsError(errno);
}

FsError PosixFsIO::opendir(const char *dirname, fs_dir_handle_t *handle)
{
  DIR *const res = ::opendir( dirname );
  if(!res)
    return currentFsError();

  *handle = (fs_dir_handle_t) res;
  return FsError::NONE;
}

FsError PosixFsIO::readdir(fs_dir_handle_t handle, char **name,
                           FsFileType *type, fs_posix_ino_t *ino)
{
  DIR *const dir = (DIR *) handle;

  while(true)
  {
    errno = 0;
    struct dirent *const de = ::readdir( dir );
    if(!de)
    {
      if(errno)
      {
        return currentFsError();
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
    *type = FsFileType::UNKNOWN;
    break;
  }

  return FsError::NONE;
}

FsError PosixFsIO::closedir( fs_dir_handle_t handle )
{
  DIR *const dir = (DIR *) handle;
  const int res_closedir = ::closedir(dir);
  return res_closedir < 0 ? currentFsError() : FsError::NONE;
}

FsError PosixFsIO::mkdir(const char *path, fs_posix_mode_t mode,
                         fs_posix_uid_t uid, fs_posix_gid_t gid)
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
  const int save_errno = errno;

#ifdef linux
  if(olduid >= 0)
    setfsuid( olduid );
  if(oldgid >= 0)
    setfsgid( oldgid );
#endif

  return res < 0 ? posixErrnoToFsError(save_errno) : FsError::NONE;
}

FsError PosixFsIO::rename(const char *from_path, const char *to_path)
{
  const int res = ::rename( from_path, to_path );
  return res < 0 ? currentFsError() : FsError::NONE;
}

FsError PosixFsIO::link(const char *from_path, const char *to_path)
{
  const int res = ::link( from_path, to_path );
  return res < 0 ? currentFsError() : FsError::NONE;
}

FsError PosixFsIO::unlink(const char *path)
{
  const int res = ::unlink( path );
  return res < 0 ? currentFsError() : FsError::NONE;
}

FsError PosixFsIO::get_type(const char *path, FsFileType *type)
{
  struct stat st;
  const int res_stat = ::stat( path, &st );
  if (res_stat < 0)
    return currentFsError();

  if (S_ISDIR(st.st_mode)) {
    *type = FsFileType::DIRECTORY;
  }
  else if (S_ISREG(st.st_mode)) {
    *type = FsFileType::REGULAR;
  }
  else {
    *type = FsFileType::UNKNOWN;
  }

  return FsError::NONE;
}

FsError PosixFsIO::get_mtime(const char *path, fs_time_t *mtime)
{
  struct stat st;
  const int res_stat = ::stat( path, &st );
  if (res_stat < 0)
    return currentFsError();

  assert( st.st_mtime >= FS_TIME_MIN );
  assert( st.st_mtime <= FS_TIME_MAX );
  *mtime = (fs_time_t) st.st_mtime;
  return FsError::NONE;
}

FsError PosixFsIO::set_mtime(const char *path, fs_time_t mtime)
{
  struct timeval new_times[2] = {
    {0, 0},
    {mtime, 0},
  };

  const int res_gettimeofday = ::gettimeofday( &new_times[0], NULL );
  if(res_gettimeofday < 0)
    return currentFsError();

  const int res_utimes = ::utimes( path, new_times );
  if(res_utimes < 0)
    return currentFsError();

  return FsError::NONE;
}

const char *PosixFsIO::path_sep()
{
  return "/";
}

bool PosixFsIO::is_valid_path(const char *path)
{
  /* this needs to be extended */
  return path[0] == '/';
}



}  // namespace encfs
