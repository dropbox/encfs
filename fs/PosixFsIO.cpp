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

#include "base/optional.h"

#include "fs/PosixFsIO.h"
#include "fs/RawFileIO.h"

using std::shared_ptr;

namespace encfs {

class PosixPath : public PathPoly
{
private:
    std::string _path;

public:
    PosixPath(std::string path)
    : _path( std::move( path ) )
    {}

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
      return std::make_shared<PosixPath>( _path + '/' + name );
    }

    virtual bool is_root() const
    {
      return _path == "/";
    }

    virtual std::string basename() const override
    {
      if (is_root()) {
        throw std::logic_error("basename on root path is undefined");
      }

      auto slash_pos = _path.rfind('/');
      return _path.substr( slash_pos );
    }

    virtual Path dirname() const override
    {
      auto slash_pos = _path.rfind('/');

      if (!slash_pos) {
        return std::make_shared<PosixPath>( "/" );
      }

      return std::make_shared<PosixPath>( _path.substr( 0, slash_pos ) );
    }

    virtual bool operator==(const shared_ptr<PathPoly> &p) const override
    {
      shared_ptr<PosixPath> p2 = std::dynamic_pointer_cast<PosixPath>(p);
      if (!p2) {
        return false;
      }
      return (const std::string &) *p2 == (const std::string &) *this;
    }

    friend class PosixFsIO;
};

static void current_fs_error(int thiserror = -1) {
  if (thiserror < 0) thiserror = errno;
  throw std::system_error( thiserror, errno_category() );
}

class PosixDirectoryIO final : public DirectoryIO
{
private:
    DIR *_dirp;
    // Only FsIO can instantiate this class
    PosixDirectoryIO(DIR *dirp) noexcept : _dirp(dirp)  {}

public:
    virtual ~PosixDirectoryIO() override;
    virtual opt::optional<FsDirEnt> readdir() override;

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

opt::optional<FsDirEnt> PosixDirectoryIO::readdir()
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

    return FsDirEnt( de->d_name );
  }

  return opt::nullopt;
}

static bool endswith(const std::string &haystack,
                     const std::string &needle) {
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
    throw std::runtime_error("Not absolute path");
  }

  std::string newpath = path;
  while (endswith(newpath, "/")) {
    newpath = path.substr(0, path.length() - 1);
  }

  return std::make_shared<PosixPath>( std::move( newpath ) );
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
  const int ret_open = file_->open( flags, 0777 );
  if(ret_open < 0) current_fs_error( -ret_open );
  return std::move( file_ );
}

void PosixFsIO::mkdir(const Path &path,
                      mode_t mode, uid_t uid, gid_t gid)
{
  // if uid or gid are set, then that should be the directory owner
  int olduid = -1;
  int oldgid = -1;

  if(uid != 0)
    olduid = setfsuid( uid );
  if(gid != 0)
    oldgid = setfsgid( gid );

  const int res = ::mkdir( path.c_str(), mode );
  const int save_errno = errno;

  /* NB: the following should really not fail */
  if(olduid >= 0)
    setfsuid( olduid );
  if(oldgid >= 0)
    setfsgid( oldgid );

  if(res < 0) current_fs_error( save_errno );
}

void PosixFsIO::mkdir(const Path &path)
{
  mkdir( path, 0777, 0, 0 );
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

FsFileAttrs PosixFsIO::get_attrs(const Path &path)
{
  struct stat st;
  const int res_stat = ::stat( path.c_str(), &st );
  if (res_stat < 0) current_fs_error();

  const FsFileType type = (S_ISDIR(st.st_mode) ? FsFileType::DIRECTORY :
                           S_ISREG(st.st_mode) ? FsFileType::REGULAR :
                           FsFileType::UNKNOWN);
  assert( st.st_mtime >= FS_TIME_MIN );
  assert( st.st_mtime <= FS_TIME_MAX );
  const fs_time_t mtime = (fs_time_t) st.st_mtime;
  const fs_off_t size = (fs_off_t) st.st_size;
  std::unique_ptr<PosixFsExtraFileAttrs> extra ( new PosixFsExtraFileAttrs() );
  extra->gid = st.st_gid;
  extra->uid = st.st_uid;
  extra->mode = st.st_mode;

  return { .type = type, .mtime = mtime, .size = size, .extra = std::move( extra ) };
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

void PosixFsIO::mknod(const Path &path,
                      mode_t mode, dev_t rdev, uid_t uid, gid_t gid)
{
  int olduid = -1;
  int oldgid = -1;

  if(uid != 0)
  {
    olduid = setfsuid( uid );
    if(olduid < 0) current_fs_error();
  }
  if(gid != 0)
  {
    oldgid = setfsgid( gid );
    if(oldgid < 0) current_fs_error();
  }

  /*
   * cf. xmp_mknod() in fusexmp.c
   * The regular file stuff could be stripped off if there
   * were a create method (advised to have)
   */
  int res = ::mknod( path.c_str(), mode, rdev );

  if(olduid >= 0)
    setfsuid( olduid );
  if(oldgid >= 0)
    setfsgid( oldgid );

  if(res == -1) current_fs_error();
}


}  // namespace encfs
