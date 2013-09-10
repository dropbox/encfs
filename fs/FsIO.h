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

/* a X-Platform FS interface */

#ifndef _FsIO_incl_
#define _FsIO_incl_

#include <cassert>
#include <cstdint>

#include <iostream>
#include <map>
#include <memory>

#include "base/optional.h"
#include "base/shared_ptr.h"
#include "fs/FileIO.h"

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

enum class FsErrorCondition {
  NONE,
  ACCESS,
  IO,
  BUSY,
  GENERIC,
};

class Path;

class PathPoly
{
public:
    virtual ~PathPoly() =0;

    virtual operator const std::string & () const =0;
    virtual const char *c_str() const =0;
    virtual Path join(const std::string & path) const =0;
    virtual std::string basename() const =0;
    virtual Path dirname() const =0;
    virtual bool operator==(const shared_ptr<PathPoly> &p) const =0;
};

std::ostream& operator << (std::ostream& os, const Path& s);

class FsDirEnt
{
public:
    std::string name;
    opt::optional<FsFileType> type;

    FsDirEnt(std::string && name_,
              opt::optional<FsFileType> && type_)
    : name( std::move( name_ ) )
    , type( std::move( type_ ) )
    {}

    FsDirEnt(std::string && name_)
    : FsDirEnt( std::move( name_ ), opt::nullopt )
    {}
};

class DirectoryIO
{
public:
    virtual ~DirectoryIO() =0;
    virtual opt::optional<FsDirEnt> readdir() =0;
};

/* wraps a polymorphic PathPoly pointer */
class Path
{
private:
    shared_ptr<PathPoly> _impl;

public:
    template<class T,
      typename std::enable_if<std::is_convertible<T *, PathPoly *>::value, int>::type = 0>
    Path(const shared_ptr<T> &from)
      : _impl( from )
    {}

    operator const std::string & () const
    {
      return (const std::string &) *_impl;
    }

    const char *c_str() const
    {
      return _impl->c_str();
    }

    Path join(const std::string & path) const
    {
      return _impl->join( path );
    }

    std::string basename() const
    {
      return _impl->basename();
    }

    Path dirname() const
    {
      return _impl->dirname();
    }

    bool operator==(const shared_ptr<PathPoly> &p) const
    {
      return (*_impl) == p;
    }

    operator shared_ptr<PathPoly> ()
    {
      return _impl;
    }
};

/* wraps a polymorphic DirectoryIO pointer */
class Directory
{
private:
    std::unique_ptr<DirectoryIO> _impl;

public:
    template<class T,
      typename std::enable_if<std::is_convertible<T *, DirectoryIO *>::value, int>::type = 0>
    Directory(std::unique_ptr<T> && from)
      : _impl(std::move( from ) )
    {}

    Directory(Directory && d) = default;
    Directory &operator=(Directory && d) = default;

    opt::optional<FsDirEnt> readdir()
    {
      return _impl->readdir();
    }

    operator std::unique_ptr<DirectoryIO> ()
    {
      return std::move( _impl );
    }
};

/* wraps a polymorphic FileIO pointer */
class File
{
private:
    std::unique_ptr<FileIO> _impl;

public:
    template<class T,
      typename std::enable_if<std::is_convertible<T *, FileIO *>::value, int>::type = 0>
    File(std::unique_ptr<T> && from)
    : _impl( std::move( from ) )
    {}

    File(File && d) = default;
    File &operator=(File && d) = default;

    Interface interface() const
    {
      return _impl->interface();
    }

    int blockSize() const
    {
      return _impl->blockSize();
    }

    void setFileName(const char *fileName)
    {
      return _impl->setFileName( fileName );
    }
    const char *getFileName() const
    {
      return _impl->getFileName();
    }

    bool setIV( uint64_t iv )
    {
      return _impl->setIV( iv );
    }

    int open( int flags )
    {
      return _impl->open( flags );
    }

    // get filesystem attributes for a file
    int getAttr( struct stat *stbuf ) const
    {
      return _impl->getAttr( stbuf );
    }
    off_t getSize( ) const
    {
      return _impl->getSize();
    }

    ssize_t read( const IORequest &req ) const
    {
      return _impl->read( req );
    }
    bool write( const IORequest &req )
    {
      return _impl->write( req );
    }

    int truncate( off_t size )
    {
      return _impl->truncate( size );
    }

    bool isWritable() const
    {
      return _impl->isWritable();
    }

    operator std::unique_ptr<FileIO> ()
    {
      return std::move( _impl );
    }
};

class FsIO
{
public:
    virtual ~FsIO() =0;

    virtual Path pathFromString(const std::string &path) =0;

    virtual Directory opendir(const Path &path) =0;
    virtual File openfile(const Path &path,
                          bool open_for_write = false,
                          bool create = false) =0;

    virtual void mkdir(const Path &path,
                       fs_posix_mode_t mode = 0,
                       fs_posix_uid_t uid = 0,
                       fs_posix_gid_t gid = 0) =0;

    virtual void rename(const Path &pathSrc, const Path &pathDst) =0;

    virtual void link(const Path &pathSrc, const Path &pathDst) =0;

    virtual void unlink(const Path &path) =0;
    virtual void rmdir(const Path &path) =0;

    virtual fs_time_t get_mtime(const Path &path) =0;
    virtual void set_mtime(const Path &path, fs_time_t mtime) =0;

    virtual FsFileType get_type(const Path &path) =0;
};

}  // namespace encfs

#endif

