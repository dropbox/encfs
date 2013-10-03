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

#include <memory>
#include <vector>

#include "base/optional.h"

#include "fs/fstypes.h"
#include "fs/FileIO.h"

namespace encfs {

class FsDirEnt
{
public:
    std::string name;
    fs_file_id_t file_id;
    opt::optional<FsFileType> type;

    explicit FsDirEnt(std::string name_,
                      fs_file_id_t file_id_,
                      opt::optional<FsFileType> type_ = opt::nullopt)
    : name( std::move( name_ ) )
    , file_id( std::move( file_id_ ) )
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
    virtual bool operator==(const std::shared_ptr<PathPoly> &p) const =0;
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
    std::shared_ptr<PathPoly> _impl;

public:
    template<class T,
      typename std::enable_if<std::is_convertible<T *, PathPoly *>::value, int>::type = 0>
    Path(std::shared_ptr<T> from)
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

    bool operator==(const std::shared_ptr<PathPoly> &p) const
    {
      return (*_impl) == p;
    }

    operator std::shared_ptr<PathPoly> () const
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

    FsFileAttrs get_attrs() const
    {
      return _impl->get_attrs();
    }

    size_t read( const IORequest &req ) const
    {
      return _impl->read( req );
    }

    size_t read(fs_off_t offset, byte *data, size_t dataLen) const
    {
      return read( IORequest( offset, data, dataLen ) );
    }

    void write( const IORequest &req )
    {
      return _impl->write( req );
    }

    void write( fs_off_t offset, const byte *data, size_t dataLen )
    {
      return write( IORequest( offset, (byte *) data, dataLen ) );
    }

    void truncate( off_t size )
    {
      return _impl->truncate( size );
    }

    bool isWritable() const
    {
      return _impl->isWritable();
    }

    void sync(bool datasync)
    {
      return _impl->sync( datasync );
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

    virtual void mkdir(const Path &path) =0;

    virtual void rename(const Path &pathSrc, const Path &pathDst) =0;

    virtual void unlink(const Path &path) =0;
    virtual void rmdir(const Path &path) =0;

    virtual void set_times(const Path &path,
                           const opt::optional<fs_time_t> &atime,
                           const opt::optional<fs_time_t> &mtime) =0;

    virtual FsFileAttrs get_attrs(const Path &path) =0;

    // optional methods to support full file system emulation on posix
    virtual fs_posix_uid_t posix_setfsuid(fs_posix_uid_t uid);
    virtual fs_posix_gid_t posix_setfsgid(fs_posix_gid_t gid);
    virtual File posix_create(const Path &pathSrc, fs_posix_mode_t mode);
    virtual void posix_mkdir(const Path &path, fs_posix_mode_t mode);
    virtual void posix_mknod(const Path &path, fs_posix_mode_t mode, fs_posix_dev_t dev);
    virtual void posix_mkfifo(const Path &path, fs_posix_mode_t mode);
    virtual void posix_link(const Path &pathSrc, const Path &pathDst);
    virtual void posix_symlink(const Path &path, PosixSymlinkData link_data);
    virtual PosixSymlinkData posix_readlink(const Path &path) const;
    virtual void posix_chmod(const Path &pathSrc, fs_posix_mode_t mode);
    virtual void posix_chown(const Path &pathSrc, fs_posix_uid_t uid, fs_posix_gid_t gid);
    virtual void posix_setxattr(const Path &path, bool follow,
                                std::string name, size_t offset,
                                std::vector<byte> buf, PosixSetxattrFlags flags);
    virtual std::vector<byte> posix_getxattr(const Path &path, bool follow,
                                             std::string name, size_t offset, size_t amt) const;
    virtual PosixXattrList posix_listxattr(const Path &path, bool follow) const;
    virtual void posix_removexattr(const Path &path, bool follow, std::string name);
};

}  // namespace encfs

#endif

