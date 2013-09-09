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

#include <algorithm>
#include <map>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <vector>


#include "fs/PosixFsIO.h"
#include "fs/RawFileIO.h"

namespace encfs {

// create a custom posix category that returns the right
// error condition in default_error_condition
// (using generic_category to match with errc, not every implementation of
//  system_category does: (required by the C++11 standard, 19.5.1.5p4)
//  system_category().default_error_condition(errno).category == generic_category() )
class posix_error_category : public std::error_category
{
public:
  posix_error_category() {}

  std::error_condition
  default_error_condition(int __i) const noexcept
  { return std::make_error_condition( static_cast<std::errc>( __i ) ); }

  virtual const char *name() const noexcept { return "posix_error"; }
  virtual std::string message( int cond ) const { return strerror( cond ); }
};

const std::error_category &posix_category() noexcept
{
  static const posix_error_category posix_category_instance;
  return posix_category_instance;
}

static char *strdup_x(const char *s)
{
  size_t len = strlen(s);
  char *toret = (char *) malloc(len + 1);
  if (!toret) {
    return NULL;
  }
  return (char *) memcpy(toret, s, len + 1);
}

class PosixPath : public PathPoly
{
private:
    std::string _path;

public:
    PosixPath(const char *p);
    PosixPath(const std::string & str);
    PosixPath(std::string && path);

    virtual operator const std::string &() const override
    {
      return _path;
    }

    virtual const char *c_str() const override
    {
      return _path.c_str();
    }

    virtual Path join(const std::string &name) const override
    {
      return Path( std::make_shared<PosixPath>( _path + '/' + name ) );
    }

    virtual std::string basename() const override
    {
      return "";
    }

    virtual Path dirname() const override
    {
      return Path( std::make_shared<PosixPath>( "" ) );
    }

    virtual bool operator==(const shared_ptr<PathPoly> &p) const override
    {
      return true;
    }

    friend class PosixFsIO;
};

PosixPath::PosixPath(const char *p) : _path( p )
{}

PosixPath::PosixPath(const std::string & str) : _path( str )
{}

PosixPath::PosixPath(std::string && str) : _path( std::move( str ) )
{}

static void current_fs_error(int thiserror = -1) {
  if (thiserror < 0) thiserror = errno;
  throw std::system_error( thiserror, posix_category() );
}

class PosixDirectoryIO final : public DirectoryIO
{
private:
    DIR *_dirp;
    // Only FsIO can instantiate this class
    PosixDirectoryIO(DIR *dirp) noexcept : _dirp(dirp)  {}

public:
    ~PosixDirectoryIO() override;
    optional<FsDirEnt> readdir() override;

    friend class PosixFsIO;
};

PosixDirectoryIO::~PosixDirectoryIO()
{
  const int ret = ::closedir(_dirp);
  if(ret < 0)
  {
    /* this can't fail */
    abort();
  }
}

optional<FsDirEnt> PosixDirectoryIO::readdir()
{
  while(true)
  {
    errno = 0;
    struct dirent *const de = ::readdir( _dirp );
    if(!de)
    {
      if(errno) current_fs_error();
      else
      {
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

    return FsDirEnt( de->d_name, nullopt );
  }

  return nullopt;
}

static bool endswith(const std::string & haystack,
                     const std::string & needle) {
  if (needle.length() > haystack.length()) {
    return false;
  }

  return !haystack.compare(haystack.length() - needle.length(),
                           needle.length(), needle);
}

Path PosixFsIO::pathFromString(const std::string &path)
{
  /* TODO: throw exception if path is not a UTF-8 posix path */
  if (path[0] != '/') {
    throw std::runtime_error("Bad path");
  }

  std::string newpath = path;
  while (endswith(newpath, "/")) {
    newpath = path.substr(0, path.length() - 1);
  }

  return std::make_shared<PosixPath>(newpath);
}

Directory PosixFsIO::opendir(const Path &path)
{
  DIR *const res = ::opendir( path.c_str() );
  if(!res) current_fs_error();

  return std::unique_ptr<PosixDirectoryIO>( new PosixDirectoryIO( res ) );
}

File PosixFsIO::openfile(const Path &path,
                         bool open_for_write,
                         bool create)
{
  auto file_ = std::unique_ptr<RawFileIO>( new RawFileIO( path ) );
  int flags = O_RDONLY
    | (open_for_write ? O_WRONLY : 0)
    | (create ? O_CREAT : 0);
  const int ret_open = file_->open( flags );
  if (ret_open < 0) current_fs_error( -ret_open );
  return std::move( file_ );
}

void PosixFsIO::mkdir(const Path &path,
                      fs_posix_mode_t mode,
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

  const int res = ::mkdir( path.c_str(), mode );
  const int save_errno = errno;

#ifdef linux
  if(olduid >= 0)
    setfsuid( olduid );
  if(oldgid >= 0)
    setfsgid( oldgid );
#endif

  if(res < 0) current_fs_error( save_errno );
}

void PosixFsIO::rename(const Path &path_src, const Path &path_dst)
{
  const int res = ::rename( path_src.c_str(), path_dst.c_str() );
  if(res < 0) current_fs_error();
}

void PosixFsIO::link(const Path &path_src, const Path &path_dst)
{
  const int res = ::link( path_src.c_str(), path_dst.c_str() );
  if(res < 0) current_fs_error();
}

void PosixFsIO::unlink(const Path &path)
{
  const int res = ::unlink( path.c_str() );
  if(res < 0) current_fs_error();
}

void PosixFsIO::rmdir(const Path &path)
{
  const int res = ::rmdir( path.c_str() );
  if(res < 0) current_fs_error();
}

FsFileType PosixFsIO::get_type(const Path &path)
{
  struct stat st;
  const int res_stat = ::stat( path.c_str(), &st );
  if (res_stat < 0) current_fs_error();

  if (S_ISDIR(st.st_mode)) {
    return FsFileType::DIRECTORY;
  }
  else if (S_ISREG(st.st_mode)) {
    return FsFileType::REGULAR;
  }
  else {
    return FsFileType::UNKNOWN;
  }
}

fs_time_t PosixFsIO::get_mtime(const Path &path)
{
  struct stat st;
  const int res_stat = ::stat( path.c_str(), &st );
  if (res_stat < 0) current_fs_error();

  assert( st.st_mtime >= FS_TIME_MIN );
  assert( st.st_mtime <= FS_TIME_MAX );
  return (fs_time_t) st.st_mtime;
}

void PosixFsIO::set_mtime(const Path &path, fs_time_t mtime)
{
  struct timeval new_times[2] = {
    {0, 0},
    {mtime, 0},
  };

  const int res_gettimeofday = ::gettimeofday( &new_times[0], NULL );
  if (res_gettimeofday < 0) current_fs_error();

  const int res_utimes = ::utimes( path.c_str(), new_times );
  if (res_utimes < 0) current_fs_error();
}

}  // namespace encfs
