/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2003-2007, Valient Gough
 * 
 * This program is free software; you can distribute it and/or modify it under 
 * the terms of the GNU General Public License (GPL), as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "base/config.h"

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <cerrno>
#include <sys/statvfs.h>
#include <sys/time.h>

#include <sys/types.h>
#ifdef linux
#include <sys/fsuid.h>
#endif

#ifdef HAVE_ATTR_XATTR_H
#include <attr/xattr.h>
#elif defined(HAVE_SYS_XATTR_H)
#include <sys/xattr.h>
#endif

#include <cstdio>
#include <cstring>

#include <functional>
#include <map>
#include <memory>
#include <string>

#ifdef HAVE_TR1_TUPLE
#include <tr1/tuple>
using std::tr1::get;
using std::tr1::make_tuple;
using std::tr1::tuple;
#else
#include <tuple>
using std::get;
using std::make_tuple;
using std::tuple;
#endif

#include <glog/logging.h>

#include "base/Mutex.h"
#include "base/Error.h"

#include "cipher/MemoryPool.h"

#include "fs/Context.h"
#include "fs/DirNode.h"
#include "fs/FileUtils.h"
#include "fs/PosixFsIO.h"

#include "fs/encfs.h"

using std::map;
using std::string;
using std::vector;
using std::shared_ptr;

namespace encfs {

#ifndef MIN
#define MIN(a,b) (((a)<(b)) ? (a): (b))
#endif

#define ESUCCESS 0

#define GET_FN(ctx, finfo) ctx->getNode((void*)(uintptr_t)finfo->fh)

static EncFS_Context * context()
{
    return (EncFS_Context*)fuse_get_context()->private_data;
}

/*
    The log messages below always print out encrypted filenames, not
    plaintext.  The reason is so that it isn't possible to leak information
    about the encrypted data through logging interfaces.


    The purpose of this layer of code is to take the FUSE request and dispatch
    to the internal interfaces.  Any marshaling of arguments and return types
    can be done here.
*/

template<typename T, typename R, typename... Args>
std::function<int(R *, const std::string &, Args...)> fsMethodToCStyleFn(const shared_ptr<T> &obj,
                                                                         R (T::*fn)(const Path &, Args...))
{
  std::function<R(const std::string &, Args...)> string_fn = [=] (const std::string &p, Args... args) {
    return (obj.get()->*fn)( obj->pathFromString( p ), args... );
  };
  return wrapWithExceptionCatcher( EIO, std::move( string_fn ) );
}

template<typename T, typename... Args>
std::function<int(const std::string &, Args...)> fsMethodToCStyleFn(const shared_ptr<T> &obj,
                                                                    void (T::*fn)(const Path &, Args...))
{
  std::function<void(const std::string &, Args...)> string_fn = [=] (const std::string &p, Args... args) {
    return (obj.get()->*fn)( obj->pathFromString( p ), args... );
  };
  return wrapWithExceptionCatcher( EIO, std::move( string_fn ) );
}

// helper function -- apply a functor to a cipher path, given the plain path
template<typename T, typename... Args>
static int withCipherPath( const char *opName, T op,
                           const char *path, Args... args )
{
  EncFS_Context *ctx = context();

  int res = -EIO;
  shared_ptr<DirNode> FSRoot = ctx->getRoot(&res);
  if(!FSRoot)
    return res;

  try
  {
    string cyName = FSRoot->cipherPath( path );
    VLOG(1) << opName << " " << cyName.c_str();

    res = op( cyName, args... );
    if(res < 0) LOG(INFO) << opName << " error: " << strerror(-res);
  } catch( Error &err )
  {
    LOG(ERROR) << "error caught in " << opName <<
      ":" << err.what();
  }
  return res;
}

template<typename T, typename R, typename... Args>
std::function<int(R *, const char *, Args...)> defaultFsWrap(const char *requestor,
                                                             const shared_ptr<T> &obj,
                                                             R (T::*fn)(const Path &, Args...))
{
  return [=] (R *ret, const char *path, Args... args) {
    auto c_mknod_ = fsMethodToCStyleFn( obj, fn );
    // curry in the ret argument
    auto c_mknod = [=] (const string &path, Args... args) {
      return c_mknod_( ret, path , args... );
    };
    return withCipherPath( requestor, c_mknod, path, args... );
  };
}

template<typename T, typename... Args>
std::function<int(const char *, Args...)> defaultFsWrap(const char *requestor,
                                                        const shared_ptr<T> &obj,
                                                        void (T::*fn)(const Path &, Args...))
{
  auto c_mknod = fsMethodToCStyleFn( obj, fn );
  return [=] (const char *path, Args... args) {
    return withCipherPath(requestor, c_mknod, path, args...);
  };
}

// helper function -- apply a functor to a node
template<typename T>
static int withFileNode( const char *opName,
    const char *path, struct fuse_file_info *fi, 
    int (*op)(FileNode *, T data ), T data )
{
  EncFS_Context *ctx = context();

  int res = -EIO;
  shared_ptr<DirNode> FSRoot = ctx->getRoot(&res);
  if(!FSRoot)
    return res;

  try
  {
    shared_ptr<FileNode> fnode;

    if(fi != NULL)
      fnode = GET_FN(ctx, fi);
    else
      fnode = FSRoot->lookupNode( path, opName );

    rAssert((bool) fnode);
    VLOG(1) << opName << " " << fnode->cipherName();
    res = op( fnode.get(), data );

    LOG_IF(INFO, res < 0) << opName << " error: " << strerror(-res);
  } catch( Error &err )
  {
    LOG(ERROR) << "error caught in " << opName << 
      ":" << err.what();
  }
  return res;
}

static int encfs_file_type_to_dirent_type(FsFileType ft) {
  static std::map<FsFileType, int> theMapper = {
    {FsFileType::DIRECTORY, DT_DIR},
    {FsFileType::REGULAR, DT_REG},
  };
  /* DT_UNKNOWN should be zero since that is what is returned if
     the type constant does not exist in the map */
  static_assert(!DT_UNKNOWN, "DT_UNKNOWN must be 0");
  return theMapper[ft];
}

static int encfs_file_type_to_stat_type(FsFileType ft) {
  static std::map<FsFileType, int> theMapper = {
    {FsFileType::DIRECTORY, S_IFDIR},
    {FsFileType::REGULAR, S_IFREG},
  };
  return theMapper[ft];
}

static void fill_stbuf_from_file_attrs(struct stat *stbuf, FsFileAttrs *attrs)
{
  stbuf->st_size = attrs->size;
  PosixFsExtraFileAttrs *extra = NULL;
  if (attrs->extra &&
      (extra = dynamic_cast<PosixFsExtraFileAttrs *>(attrs->extra.get())))
  {
    stbuf->st_mode = extra->mode;
    stbuf->st_gid = extra->gid;
    stbuf->st_uid = extra->uid;
  } else
    stbuf->st_mode = encfs_file_type_to_stat_type( attrs->type ) | 0777;
  stbuf->st_mtime = attrs->mtime;
}

int _do_fgetattr(FileNode *fnode, struct stat *stbuf)
{
  FsFileAttrs attrs;
  int res = fnode->getAttr(attrs);
  if(res == ESUCCESS) fill_stbuf_from_file_attrs(stbuf, &attrs);
  if(res == ESUCCESS && S_ISLNK( stbuf->st_mode ))
  {
    EncFS_Context *ctx = context();
    shared_ptr<DirNode> FSRoot = ctx->getRoot(&res);
    if(FSRoot)
    {
      // determine plaintext link size..  Easiest to read and decrypt..
      vector<char> buf(stbuf->st_size+1, 0);

      res = ::readlink( fnode->cipherName(), &buf[0], stbuf->st_size );
      if(res >= 0)
      {
        // other functions expect c-strings to be null-terminated, which
        // readlink doesn't provide
        buf[res] = '\0';

        stbuf->st_size = FSRoot->plainPath( &buf[0] ).length();

        res = ESUCCESS;
      } else
        res = -errno;
    }
  }

  return res;
}

int encfs_getattr(const char *path, struct stat *stbuf)
{
  EncFS_Context *ctx = context();
  int res = -EIO;
  shared_ptr<DirNode> FSRoot = ctx->getRoot( &res );
  if(!FSRoot) return res;

  FsFileAttrs attrs;
  res = FSRoot->get_attrs( &attrs, path );
  if (res < 0) return res;

  fill_stbuf_from_file_attrs( stbuf, &attrs );

  return ESUCCESS;
}

int encfs_fgetattr(const char *path, struct stat *stbuf,
    struct fuse_file_info *fi)
{
  return withFileNode( "fgetattr", path, fi, _do_fgetattr, stbuf );
}

int encfs_getdir(const char *path, fuse_dirh_t h, fuse_dirfil_t filler)
{
  EncFS_Context *ctx = context();

  int res = ESUCCESS;
  shared_ptr<DirNode> FSRoot = ctx->getRoot(&res);
  if(!FSRoot)
    return res;

  try
  {

    DirTraverse dt = FSRoot->openDir( path );

    VLOG(1) << "getdir on " << FSRoot->cipherPath(path);

    if(dt.valid())
    {
      FsFileType fileType = FsFileType::UNKNOWN;

      // TODO: fileType is not good enough, it doesn't support all posix types
      // (implement dt.nextPosixName()) or something like that
      fs_file_id_t inode_i = 0;
      std::string name = dt.nextPlaintextName( &fileType, &inode_i );
      while( !name.empty() )
      {
        res = filler( h, name.c_str(), encfs_file_type_to_dirent_type(fileType), (ino_t) inode_i );

        if(res != ESUCCESS)
          break;

        name = dt.nextPlaintextName( &fileType, &inode_i );
      }
    } else
    {
      LOG(INFO) << "getdir request invalid, path: '" << path << "'";
    }

    return res;
  } catch( Error &err )
  {
    LOG(ERROR) << "error caught in getdir: " << err.what();
    return -EIO;
  }
}

int _do_mknod(const string &cyName, mode_t mode, dev_t rdev, uid_t uid, gid_t gid)
{
  EncFS_Context *ctx = context();

  int res = -1;
  shared_ptr<DirNode> FSRoot = ctx->getRoot(&res);
  if(!FSRoot)
    return res;

  assert( !S_ISREG( mode ) );
  if (S_ISFIFO( mode ))
  {
    res = ::mkfifo( cyName.c_str(), mode );
    if (res < 0) res = -errno;
  }
  else
  {
    auto posix_fs = std::dynamic_pointer_cast<PosixFsIO>( FSRoot->get_fs() );
    if(posix_fs)
    {
      auto c_mknod = fsMethodToCStyleFn( posix_fs, &PosixFsIO::mknod );
      res = c_mknod( cyName, mode, rdev, uid, gid );
    }
  }

  return res;
}

int encfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
  fuse_context *fctx = fuse_get_context();
  EncFS_Context *ctx = context();

  int res = -EIO;
  shared_ptr<DirNode> FSRoot = ctx->getRoot(&res);
  if(!FSRoot)
    return res;

  auto posix_fs = std::dynamic_pointer_cast<PosixFsIO>( FSRoot->get_fs() );
  if(!posix_fs)
    return res;

  using namespace std::placeholders;
  std::function<int(uid_t, gid_t)> mknod_fn;
  if(S_ISREG( mode ))
  {
    const bool requestWrite = false;
    const bool createFile = true;
    mknod_fn = [=] (uid_t /*uid*/, gid_t /*gid*/) {
      // TODO: use uid, gid
      int res;
      FSRoot->openNode(path, "mknod", requestWrite, createFile, &res);
      return res;
    };
  } else
  {
    mknod_fn = std::bind(withCipherPath<decltype(_do_mknod), mode_t, dev_t, uid_t, gid_t>,
                         "mknod", _do_mknod,
                         path, mode, rdev, _1, _2);
  }

  try
  {
    uid_t uid = 0;
    gid_t gid = 0;
    if(ctx->publicFilesystem)
    {
      uid = fctx->uid;
      gid = fctx->gid;
    }

    res = mknod_fn( uid, gid );

    // Is this error due to access problems?
    if(ctx->publicFilesystem && res == EACCES)
    {
      // try again using the parent dir's group
      string parent = FSRoot->plaintextParent( path );
      LOG(INFO) << "attempting public filesystem workaround for " 
                << parent.c_str();
      shared_ptr<FileNode> dnode = 
        FSRoot->lookupNode( parent.c_str(), "mknod" );

      FsFileAttrs attrs;
      PosixFsExtraFileAttrs *posix_attrs;
      if(dnode->getAttr( attrs ) == 0 &&
         attrs.extra &&
         (posix_attrs = dynamic_cast<PosixFsExtraFileAttrs *>(attrs.extra.get())))
        res = mknod_fn( uid, posix_attrs->gid );
    }
  } catch( Error &err )
  {
    LOG(ERROR) << "error caught in mknod: " << err.what();
  }
  return res;
}

int encfs_mkdir(const char *path, mode_t mode)
{
  fuse_context *fctx = fuse_get_context();
  EncFS_Context *ctx = context();

  int res = -EIO;
  shared_ptr<DirNode> FSRoot = ctx->getRoot(&res);
  if(!FSRoot)
    return res;

  auto posix_fs = std::dynamic_pointer_cast<PosixFsIO>( FSRoot->get_fs() );
  if(!posix_fs)
    return res;

  auto mkdir_fn = defaultFsWrap( "mkdir", posix_fs,
                                 // pick the posix form of mkdir
                                 (void (PosixFsIO::*)(const Path &, mode_t, uid_t, gid_t)) &PosixFsIO::mkdir );

  try
  {
    uid_t uid = 0;
    gid_t gid = 0;
    if(ctx->publicFilesystem)
    {
      uid = fctx->uid;
      gid = fctx->gid;
    }
    res = mkdir_fn( path, mode, uid, gid );
    // Is this error due to access problems?
    if(ctx->publicFilesystem && -res == EACCES)
    {
      // try again using the parent dir's group
      string parent = FSRoot->plaintextParent( path );
      shared_ptr<FileNode> dnode = 
        FSRoot->lookupNode( parent.c_str(), "mkdir" );

      FsFileAttrs attrs;
      PosixFsExtraFileAttrs *posix_attrs;
      if(dnode->getAttr( attrs ) == 0 &&
         attrs.extra &&
         (posix_attrs = dynamic_cast<PosixFsExtraFileAttrs *>(attrs.extra.get())))
        res = mkdir_fn( path, mode, uid, posix_attrs->gid );
    }
  } catch( Error &err )
  {
    LOG(ERROR) << "error caught in mkdir: " << err.what();
  }
  return res;
}

int encfs_unlink(const char *path)
{
  EncFS_Context *ctx = context();

  int res = -EIO;
  shared_ptr<DirNode> FSRoot = ctx->getRoot(&res);
  if(!FSRoot)
    return res;

  try
  {
    // let DirNode handle it atomically so that it can handle race
    // conditions
    res = FSRoot->unlink( path );
  } catch( Error &err )
  {
    LOG(ERROR) << "error caught in unlink: " << err.what();
  }
  return res;
}


int _do_rmdir(const string &cipherPath)
{
  const int res = rmdir( cipherPath.c_str() );
  return (res == -1) ? -errno : ESUCCESS;
}

int encfs_rmdir(const char *path)
{
  return withCipherPath( "rmdir", _do_rmdir, path );
}

int _do_readlink(const string &cyName, char *buf, size_t size)
{
  EncFS_Context *ctx = context();
  int res = ESUCCESS;
  shared_ptr<DirNode> FSRoot = ctx->getRoot(&res);
  if(!FSRoot)
    return res;

  res = ::readlink( cyName.c_str(), buf, size-1 );

  if(res == -1)
    return -errno;

  buf[res] = '\0'; // ensure null termination
  string decodedName;
  try
  {
    decodedName = FSRoot->plainPath( buf );
  } catch(...) { }

  if(!decodedName.empty())
  {
    strncpy(buf, decodedName.c_str(), size-1);
    buf[size-1] = '\0';

    return ESUCCESS;
  } else
  {
    LOG(WARNING) << "Error decoding link";
    return -EIO;
  }
}

int encfs_readlink(const char *path, char *buf, size_t size)
{
  return withCipherPath( "readlink", _do_readlink, path, buf, size );
}

int encfs_symlink(const char *from, const char *to)
{
  EncFS_Context *ctx = context();

  int res = -EIO;
  shared_ptr<DirNode> FSRoot = ctx->getRoot(&res);
  if(!FSRoot)
    return res;

  try
  {
    // allow fully qualified names in symbolic links.
    string fromCName = FSRoot->relativeCipherPath( from );
    string toCName = FSRoot->cipherPath( to );

    VLOG(1) << "symlink " << fromCName << " -> " << toCName;

    // use setfsuid / setfsgid so that the new link will be owned by the
    // uid/gid provided by the fuse_context.
    int olduid = -1;
    int oldgid = -1;
    if(ctx->publicFilesystem)
    {
      fuse_context *context = fuse_get_context();
      olduid = setfsuid( context->uid );
      oldgid = setfsgid( context->gid );
    }
    res = ::symlink( fromCName.c_str(), toCName.c_str() );
    if(olduid >= 0)
      setfsuid( olduid );
    if(oldgid >= 0)
      setfsgid( oldgid );

    if(res == -1)
      res = -errno;
    else
      res = ESUCCESS;
  } catch( Error &err )
  {
    LOG(ERROR) << "error caught in symlink: " << err.what();
  }
  return res;
}

int encfs_link(const char *from, const char *to)
{
  EncFS_Context *ctx = context();

  int res = -EIO;
  shared_ptr<DirNode> FSRoot = ctx->getRoot(&res);
  if(!FSRoot)
    return res;

  try
  {
    res = FSRoot->link( from, to );
  } catch( Error &err )
  {
    LOG(ERROR) << "error caught in link: " << err.what();
  }
  return res;
}

int encfs_rename(const char *from, const char *to)
{
  EncFS_Context *ctx = context();

  int res = -EIO;
  shared_ptr<DirNode> FSRoot = ctx->getRoot(&res);
  if(!FSRoot)
    return res;

  try
  {
    res = FSRoot->rename( from, to );
  } catch( Error &err )
  {
    LOG(ERROR) << "error caught in rename: " << err.what();
  }
  return res;
}

int _do_chmod(const string &cipherPath, mode_t mode)
{
  // TODO: wtf?!? fix this
  int res;

#ifdef HAVE_LCHMOD
  res = lchmod( cipherPath.c_str(), mode );
#else
  res = -1;
  errno = ENOSYS;
#endif

  if(res == -1 && errno == ENOSYS)
  {
    res = chmod( cipherPath.c_str(), mode );
  }

  return (res == -1) ? -errno : ESUCCESS;
}

int encfs_chmod(const char *path, mode_t mode)
{
  return withCipherPath( "chmod", _do_chmod, path, mode );
}

int _do_chown(const string &cyName, uid_t uid, gid_t gid)
{
  int res = lchown( cyName.c_str(), uid, gid );
  return (res == -1) ? -errno : ESUCCESS;
}

int encfs_chown(const char *path, uid_t uid, gid_t gid)
{
  return withCipherPath( "chown", _do_chown, path, uid, gid);
}

int _do_truncate( FileNode *fnode, off_t size )
{
  return fnode->truncate( size );
}

int encfs_truncate(const char *path, off_t size)
{
  /* TODO: use FsIO truncate, this is currently hacked in FileNode.cpp */
  return withFileNode( "truncate", path, NULL, _do_truncate, size );
}

int encfs_ftruncate(const char *path, off_t size, struct fuse_file_info *fi)
{
  return withFileNode( "ftruncate", path, fi, _do_truncate, size );
}

int _do_utime(const string &cyName, struct utimbuf *buf)
{
  int res = utime( cyName.c_str(), buf);
  return (res == -1) ? -errno : ESUCCESS;
}

int encfs_utime(const char *path, struct utimbuf *buf)
{
  return withCipherPath( "utime", _do_utime, path, buf );
}

int _do_utimens(const string &cyName, const struct timespec ts[2])
{
  struct timeval tv[2];
  tv[0].tv_sec = ts[0].tv_sec;
  tv[0].tv_usec = ts[0].tv_nsec / 1000;
  tv[1].tv_sec = ts[1].tv_sec;
  tv[1].tv_usec = ts[1].tv_nsec / 1000;

  int res = lutimes( cyName.c_str(), tv);
  return (res == -1) ? -errno : ESUCCESS;
}

int encfs_utimens(const char *path, const struct timespec ts[2])
{
  return withCipherPath( "utimens", _do_utimens, path, ts );
}

int encfs_open(const char *path, struct fuse_file_info *file)
{
  EncFS_Context *ctx = context();

  int res = -EIO;
  shared_ptr<DirNode> FSRoot = ctx->getRoot(&res);
  if(!FSRoot)
    return res;

  try
  {
    bool requestWrite = ((file->flags & O_RDWR) ||
                         (file->flags & O_WRONLY));

    bool createFile = false;
    shared_ptr<FileNode> fnode =
      FSRoot->openNode( path, "open", requestWrite, createFile, &res );

    if(fnode)
    {
      VLOG(1) << "encfs_open for " << fnode->cipherName()
              << ", flags " << file->flags;

      if( res >= 0 )
      {
        file->fh = (uintptr_t)ctx->putNode(path, fnode);
        res = ESUCCESS;
      }
    }
  } catch( Error &err )
  {
    LOG(ERROR) << "error caught in open: " << err.what();
  }

  return res;
}

int _do_flush(FileNode *fnode, int )
{
  /* Flush can be called multiple times for an open file, so it doesn't
     close the file.  However it is important to call close() for some
     underlying filesystems (like NFS).
   */
  return withExceptionCatcher( EIO, bindMethod( fnode, &FileNode::flush ) );
}

int encfs_flush(const char *path, struct fuse_file_info *fi)
{
  return withFileNode( "flush", path, fi, _do_flush, 0 );
}

/*
Note: This is advisory -- it might benefit us to keep file nodes around for a
bit after they are released just in case they are reopened soon.  But that
requires a cache layer.
 */
int encfs_release(const char *path, struct fuse_file_info *finfo)
{
  EncFS_Context *ctx = context();

  try
  {
    ctx->eraseNode( path, (void*)(uintptr_t)finfo->fh );
    return ESUCCESS;
  } catch( Error &err )
  {
    LOG(ERROR) << "error caught in release: " << err.what();
    return -EIO;
  }
}

int _do_read(FileNode *fnode, tuple<unsigned char *, size_t, off_t> data)
{
  return fnode->read( get<2>(data), get<0>(data), get<1>(data));
}

int encfs_read(const char *path, char *buf, size_t size, off_t offset,
    struct fuse_file_info *file)
{
  return withFileNode( "read", path, file, _do_read,
      make_tuple((unsigned char *)buf, size, offset));
}

int _do_fsync(FileNode *fnode, int dataSync)
{
  return fnode->sync( dataSync != 0 );
}

int encfs_fsync(const char *path, int dataSync,
    struct fuse_file_info *file)
{
  return withFileNode( "fsync", path, file, _do_fsync, dataSync );
}

int _do_write(FileNode *fnode, tuple<const char *, size_t, off_t> data)
{
  size_t size = get<1>(data);
  if(fnode->write( get<2>(data), (unsigned char *)get<0>(data), size ))
    return size;
  else
    return -EIO;
}

int encfs_write(const char *path, const char *buf, size_t size,
                off_t offset, struct fuse_file_info *file)
{
  return withFileNode("write", path, file, _do_write,
      make_tuple(buf, size, offset));
}

// statfs works even if encfs is detached..
int encfs_statfs(const char *path, struct statvfs *st)
{
  EncFS_Context *ctx = context();

  int res = -EIO;
  try
  {
    (void)path; // path should always be '/' for now..
    rAssert( st != NULL );
    string cyName = ctx->rootCipherDir;

    VLOG(1) << "doing statfs of " << cyName;
    res = statvfs( cyName.c_str(), st );
    if(!res) 
    {
      // adjust maximum name length..
      st->f_namemax = 6 * (st->f_namemax - 2) / 8; // approx..
    }
    if(res == -1)
      res = -errno;
  } catch( Error &err )
  {
    LOG(ERROR) << "error caught in statfs: " << err.what();
  }
  return res;
}

#ifdef HAVE_XATTR


#ifdef XATTR_ADD_OPT
int _do_setxattr(const string &cyName,
    const char *name, const char *value, size_t size, uint32_t position)
{
  const int options = XATTR_NOFOLLOW;
  int res = ::setxattr( cyName.c_str(), name, value, size, position, options );
  return (res == -1) ? -errno : ESUCCESS;
}
int encfs_setxattr( const char *path, const char *name,
    const char *value, size_t size, int flags, uint32_t position )
{
  (void)flags;
  return withCipherPath( "setxattr", _do_setxattr,
                         path, name, value, size, position);
}
#else
int _do_setxattr(const string &cyName,
                 const char *name, const char *value, size_t size, int flags)
{
  int res = ::setxattr( cyName.c_str(), name, value, size, flags );
  return (res == -1) ? -errno : ESUCCESS;
}
int encfs_setxattr( const char *path, const char *name,
    const char *value, size_t size, int flags )
{
  return withCipherPath( "setxattr", _do_setxattr,
                         path, name, value, size, flags );
}
#endif


#ifdef XATTR_ADD_OPT
int _do_getxattr(const string &cyName,
                 const char *name, void *value, size_t size, uint32_t position)
{
  int options = 0;
  int res = ::getxattr( cyName.c_str(), name, value, size, position, options );
  return (res == -1) ? -errno : res;
}
int encfs_getxattr( const char *path, const char *name,
    char *value, size_t size, uint32_t position )
{
  return withCipherPath( "getxattr", _do_getxattr,
                         path, name, (void *) value, size, position );
}
#else
int _do_getxattr(const string &cyName,
                 const char *name, void *value, size_t size)
{
  int res = ::getxattr( cyName.c_str(), name, value, size );
  return (res == -1) ? -errno : res;
}
int encfs_getxattr( const char *path, const char *name,
                    char *value, size_t size )
{
  return withCipherPath( "getxattr", _do_getxattr,
                          path, name, (void *) value, size );
}
#endif


int _do_listxattr(const string &cyName,
                  char *list, size_t size)
{
#ifdef XATTR_ADD_OPT
  int options = 0;
  int res = ::listxattr( cyName.c_str(), list, size, options);
#else
  int res = ::listxattr( cyName.c_str(), list, size );
#endif
  return (res == -1) ? -errno : res;
}

int encfs_listxattr( const char *path, char *list, size_t size )
{
  return withCipherPath( "listxattr", _do_listxattr, path,
                         list, size );
}

int _do_removexattr(const string &cyName, const char *name)
{
#ifdef XATTR_ADD_OPT
  int options = 0;
  int res = ::removexattr( cyName.c_str(), name, options );
#else
  int res = ::removexattr( cyName.c_str(), name );
#endif
  return (res == -1) ? -errno : ESUCCESS;
}

int encfs_removexattr( const char *path, const char *name )
{
  return withCipherPath( "removexattr", _do_removexattr, path, name );
}

}  // namespace encfs

#endif // HAVE_XATTR

