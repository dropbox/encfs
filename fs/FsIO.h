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
#include "fs/fstypes.h"
#include "fs/FileIO.h"

namespace encfs {

class FsDirEnt
{
public:
    std::string name;
    opt::optional<FsFileType> type;

    explicit FsDirEnt(std::string name_,
                      opt::optional<FsFileType> type_ = opt::nullopt)
    : name( std::move( name_ ) )
    , type( std::move( type_ ) )
    {}
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
    Path(shared_ptr<T> from)
      : _impl( std::move( from ) )
    {
      assert(_impl);
    }

    operator const std::string & () const
    {
      return (const std::string &) *_impl;
    }

    const char *c_str() const
    {
      return _impl->c_str();
    }

    Path join(const std::string &path) const
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

    operator shared_ptr<PathPoly> () const
    {
      return _impl;
    }
};

std::ostream& operator << (std::ostream& os, const Path& s);

template<class T>
class _UniqueWrapper
{
protected:
    typedef T element_type;
    std::unique_ptr<T> _impl;

public:
    _UniqueWrapper(std::unique_ptr<T> from)
    : _impl( std::move( from ) )
    {
      assert( _impl );
    }

    std::unique_ptr<T> take_ptr()
    {
      return std::move( _impl );
    }
};

/* wraps a polymorphic DirectoryIO pointer */
class Directory : public _UniqueWrapper<DirectoryIO>
{
public:
    template<class U,
      typename std::enable_if<std::is_convertible<U *, element_type *>::value, int>::type = 0>
    Directory(std::unique_ptr<U> from)
    : _UniqueWrapper( std::move( from ) )
    {}

    opt::optional<FsDirEnt> readdir()
    {
      return _impl->readdir();
    }
};

/* wraps a polymorphic FileIO pointer */
class File : public _UniqueWrapper<FileIO>
{
public:
    template<class U,
      typename std::enable_if<std::is_convertible<U *, element_type *>::value, int>::type = 0>
    File(std::unique_ptr<U> from)
    : _UniqueWrapper( std::move( from ) )
    {}

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
    int getAttr( FsFileAttrs &stbuf ) const
    {
      return _impl->getAttr( stbuf );
    }

    FsFileAttrs get_attrs() const
    {
      FsFileAttrs attrs;
      auto ret = getAttr(attrs);
      if (ret) {
        throw std::system_error( -ret, errno_category() );
      }
      return std::move( attrs );
    }

    fs_off_t getSize() const
    {
      return _impl->getSize();
    }

    ssize_t read( const IORequest &req ) const
    {
      return _impl->read( req );
    }

    ssize_t read(fs_off_t offset, byte *data, size_t dataLen) const
    {
      return read( IORequest( offset, data, dataLen ) );
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

    virtual void set_mtime(const Path &path, fs_time_t mtime) =0;

    virtual FsFileAttrs get_attrs(const Path &path) =0;
};

}  // namespace encfs

#endif

