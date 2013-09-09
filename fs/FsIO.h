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
    optional<FsFileType> type;

    FsDirEnt(std::string && name_,
              optional<FsFileType> && type_)
    : name( std::move( name_ ) )
    , type( std::move( type_ ) )
    {}

    FsDirEnt(std::string && name_)
    : FsDirEnt( std::move( name_ ), nullopt )
    {}
};

class DirectoryIO
{
public:
    virtual ~DirectoryIO() =0;
    virtual optional<FsDirEnt> readdir() =0;
};

/* wraps a polymorphic PathPoly pointer */
class Path : public PathPoly
{
private:
    shared_ptr<PathPoly> _impl;

public:
    template<class T,
      typename std::enable_if<std::is_convertible<T *, PathPoly *>::value, int>::type = 0>
    Path(const shared_ptr<T> &from)
      : _impl( from )
    {}

    virtual operator const std::string & () const override
    {
      return (const std::string &) *_impl;
    }

    virtual const char *c_str() const override
    {
      return _impl->c_str();
    }

    virtual Path join(const std::string & path) const override
    {
      return _impl->join( path );
    }

    virtual std::string basename() const override
    {
      return _impl->basename();
    }

    virtual Path dirname() const override
    {
      return _impl->dirname();
    }

    virtual bool operator==(const shared_ptr<PathPoly> &p) const override
    {
      return (*_impl) == p;
    }

    operator shared_ptr<PathPoly> ()
    {
      return _impl;
    }
};

/* wraps a polymorphic DirectoryIO pointer */
class Directory : public DirectoryIO
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

    virtual optional<FsDirEnt> readdir() override
    {
      return _impl->readdir();
    }

    operator std::unique_ptr<DirectoryIO> ()
    {
      return std::move( _impl );
    }
};

/* wraps a polymorphic FileIO pointer */
class File : public FileIO
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

    virtual Interface interface() const override
    {
      return makeInterface("FileIO/File", 1, 0, 0);
    }

    // default implementation returns 1, meaning this is not block oriented.
    virtual int blockSize() const override
    {
      return _impl->blockSize();
    }

    virtual void setFileName(const char *fileName) override
    {
      return _impl->setFileName( fileName );
    }
    virtual const char *getFileName() const override
    {
      return _impl->getFileName();
    }

    // Not sure about this -- it is specific to CipherFileIO, but the
    // alternative methods of exposing this interface aren't much nicer..
    virtual bool setIV( uint64_t iv ) override
    {
      return _impl->setIV( iv );
    }

    // open file for specified mode.  There is no corresponding close, so a
    // file is open until the FileIO interface is destroyed.
    virtual int open( int flags ) override
    {
      return _impl->open( flags );
    }

    // get filesystem attributes for a file
    virtual int getAttr( struct stat *stbuf ) const
    {
      return _impl->getAttr( stbuf );
    }
    virtual off_t getSize( ) const
    {
      return _impl->getSize();
    }

    virtual ssize_t read( const IORequest &req ) const
    {
      return _impl->read( req );
    }
    virtual bool write( const IORequest &req )
    {
      return _impl->write( req );
    }

    virtual int truncate( off_t size )
    {
      return _impl->truncate( size );
    }

    virtual bool isWritable() const
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

