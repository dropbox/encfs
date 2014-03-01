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

#include "encfs/encfs.h"

#include "fs/FsIO.h"

#include "cipher/MemoryPool.h"

#include "base/logging.h"
#include "base/Mutex.h"
#include "base/Error.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "encfs/xattr.h"

#include <dirent.h>

using std::map;
using std::string;
using std::vector;
using std::shared_ptr;

namespace encfs {

enum {
  ESUCCESS = 0,
};

enum {
  NO_FOLLOW,
  YES_FOLLOW,
};

EncFSFuseContext *get_global_encfs_fuse_context() {
  return (EncFSFuseContext *)fuse_get_context()->private_data;
}

static FsIO &gGetFS() { return *get_global_encfs_fuse_context()->getFS(); }

static bool gIsPublic() { return get_global_encfs_fuse_context()->isPublic(); }

static void gSaveFile(struct fuse_file_info *fi, File f) {
  return get_global_encfs_fuse_context()->saveFile(fi, std::move(f));
}

static File &gGetFile(const char *path, struct fuse_file_info *fi) {
  return get_global_encfs_fuse_context()->getFile(path, fi);
}

static File gReleaseFile(const char *path, struct fuse_file_info *fi) {
  return get_global_encfs_fuse_context()->releaseFile(path, fi);
}

/*
    The log messages below always print out encrypted filenames, not
    plaintext.  The reason is so that it isn't possible to leak information
    about the encrypted data through logging interfaces.


    The purpose of this layer of code is to take the FUSE request and dispatch
    to the internal interfaces.  Any marshaling of arguments and return types
    can be done here.
*/

static int encfs_file_type_to_dirent_type(FsFileType ft) {
  static std::map<FsFileType, int> theMapper = {
      {FsFileType::DIRECTORY, DT_DIR}, {FsFileType::REGULAR, DT_REG}, };
  /* DT_UNKNOWN should be zero since that is what is returned if
     the type constant does not exist in the map */
  static_assert(!DT_UNKNOWN, "DT_UNKNOWN must be 0");
  return theMapper[ft];
}

static int encfs_file_type_to_stat_type(FsFileType ft) {
  static std::map<FsFileType, int> theMapper = {
      {FsFileType::DIRECTORY, S_IFDIR}, {FsFileType::REGULAR, S_IFREG}, };
  return theMapper[ft];
}

static void fill_stbuf_from_file_attrs(struct stat *stbuf, FsFileAttrs *attrs) {
  stbuf->st_size = attrs->size;
  if (attrs->posix) {
    stbuf->st_gid = attrs->posix->gid;
    stbuf->st_uid = attrs->posix->uid;
    stbuf->st_mode = attrs->posix->mode;
  } else
    stbuf->st_mode = encfs_file_type_to_stat_type(attrs->type) | 0777;
  stbuf->st_mtime = attrs->mtime;
}

int encfs_getattr(const char *cpath, struct stat *stbuf) {
  auto &fs_io = gGetFS();
  auto path = fs_io.pathFromString(cpath);

  FsFileAttrs attrs;
  int ret = withExceptionCatcher(EIO, bindMethod(fs_io, &FsIO::posix_stat),
                                 &attrs, path, NO_FOLLOW);
  if (ret < 0) return ret;

  fill_stbuf_from_file_attrs(stbuf, &attrs);

  return ret;
}

int encfs_fgetattr(const char *path, struct stat *stbuf,
                   struct fuse_file_info *fi) {
  auto &fref = gGetFile(path, fi);

  FsFileAttrs attrs;
  int ret =
      withExceptionCatcher(EIO, bindMethod(fref, &File::get_attrs), &attrs);
  if (ret < 0) return ret;

  fill_stbuf_from_file_attrs(stbuf, &attrs);

  return ret;
}

int encfs_getdir(const char *cpath, fuse_dirh_t h, fuse_dirfil_t filler) {
  auto &fs_io = gGetFS();
  auto path = fs_io.pathFromString(cpath);

  opt::optional<Directory> dir;
  int ret =
      withExceptionCatcher(EIO, bindMethod(fs_io, &FsIO::opendir), &dir, path);
  if (ret < 0) return ret;

  assert(dir);

  opt::optional<FsDirEnt> dirent;
  while ((dirent = dir->readdir())) {
    int dtype = DT_UNKNOWN;
    if (dirent->type) dtype = encfs_file_type_to_dirent_type(*dirent->type);

    ret = filler(h, dirent->name.c_str(), dtype, (ino_t)dirent->file_id);

    if (ret != ESUCCESS) break;
  }

  return ret;
}

template <typename F, class... Args>
static int _do_mk_preserve(F f, const char *cpath, Args... args) {
  auto &fs_io = gGetFS();
  auto path = fs_io.pathFromString(cpath);

  fs_posix_uid_t olduid = 0;
  fs_posix_gid_t oldgid = 0;

  if (gIsPublic()) {
    auto fctx = fuse_get_context();
    // TODO: no exception
    olduid = fs_io.posix_setfsuid(fctx->uid);
    oldgid = fs_io.posix_setfsgid(fctx->gid);
  }

  int res = withExceptionCatcherNoRet(EIO, f, fs_io, path, args...);

  // Is this error due to access problems?
  if (gIsPublic() && -res == EACCES) {
    // try again using the parent dir's group
    auto parent = path.dirname();
    LOG(INFO) << "attempting public filesystem workaround for "
              << parent.c_str();

    // TODO: no exception
    auto attrs = encfs::get_attrs(&fs_io, parent);
    if (attrs.posix) fs_io.posix_setfsgid(attrs.posix->gid);
    res = withExceptionCatcherNoRet(EIO, f, fs_io, path, args...);
  }

  if (gIsPublic()) {
    // TODO: no exception
    fs_io.posix_setfsuid(olduid);
    fs_io.posix_setfsgid(oldgid);
  }

  return res;
}

static void _smart_mknod(FsIO &fs_io, const Path &path, mode_t mode,
                         dev_t rdev) {
  if (S_ISREG(mode)) {
    fs_io.posix_create(path, mode);
  } else if (S_ISFIFO(mode)) {
    fs_io.posix_mkfifo(path, mode);
  } else {
    fs_io.posix_mknod(path, mode, rdev);
  }
}

int encfs_mknod(const char *cpath, mode_t mode, dev_t rdev) {
  return _do_mk_preserve(_smart_mknod, cpath, mode, rdev);
}

int encfs_mkdir(const char *cpath, mode_t mode) {
  auto to_call = [=](FsIO &fs_io, const Path &p,
                     mode_t mode) { return fs_io.posix_mkdir(p, mode); };
  return _do_mk_preserve(to_call, cpath, mode);
}

template <typename U, typename... Args>
static int _do_one_path(U m, const char *cpath, Args... args) {
  auto &fs_io = gGetFS();
  auto path = fs_io.pathFromString(cpath);

  return withExceptionCatcherNoRet(EIO, bindMethod(fs_io, m), std::move(path),
                                   args...);
}

template <typename U, typename... Args>
static int _do_two_path(U m, const char *from, const char *to, Args... args) {
  auto &fs_io = gGetFS();
  auto from_path = fs_io.pathFromString(from);
  auto to_path = fs_io.pathFromString(to);

  return withExceptionCatcherNoRet(EIO, bindMethod(fs_io, m),
                                   std::move(from_path), std::move(to_path),
                                   args...);
}

int encfs_unlink(const char *cpath) {
  return _do_one_path(&FsIO::unlink, cpath);
}

int encfs_rmdir(const char *cpath) { return _do_one_path(&FsIO::rmdir, cpath); }

int encfs_readlink(const char *cpath, char *buf, size_t size) {
  auto &fs_io = gGetFS();
  auto path = fs_io.pathFromString(cpath);

  opt::optional<PosixSymlinkData> link_data;
  int res = withExceptionCatcher(EIO, bindMethod(fs_io, &FsIO::posix_readlink),
                                 &link_data, path);
  if (res < 0) return res;

  assert(link_data);

  size_t amt_to_copy = std::min(size - 1, link_data->size());
  memmove(buf, link_data->data(), amt_to_copy);
  buf[amt_to_copy] = '\0';

  return ESUCCESS;
}

int encfs_symlink(const char *from, const char *to) {
  auto &fs_io = gGetFS();
  auto link_data = PosixSymlinkData(from);
  auto to_path = fs_io.pathFromString(to);

  // TODO: make sure link will be owned by client uid if public
  return withExceptionCatcherNoRet(EIO, bindMethod(fs_io, &FsIO::posix_symlink),
                                   std::move(to_path), std::move(link_data));
}

int encfs_link(const char *from, const char *to) {
  // TODO: make sure link will be owned by client uid if public
  return _do_two_path(&FsIO::posix_link, from, to);
}

int encfs_rename(const char *from, const char *to) {
  return _do_two_path(&FsIO::rename, from, to);
}

int encfs_chmod(const char *cpath, mode_t mode) {
  return _do_one_path(&FsIO::posix_chmod, cpath, NO_FOLLOW, mode);
}

int encfs_chown(const char *cpath, uid_t uid, gid_t gid) {
  return _do_one_path(&FsIO::posix_chown, cpath, NO_FOLLOW, uid, gid);
}

int encfs_truncate(const char *cpath, off_t size) {
  auto &fs_io = gGetFS();
  auto path = fs_io.pathFromString(cpath);

  opt::optional<File> maybeFile;
  const bool open_for_write = true;
  const bool create_file = false;
  const int ret =
      withExceptionCatcher(EIO, bindMethod(fs_io, &FsIO::openfile), &maybeFile,
                           path, open_for_write, create_file);
  if (ret < 0) return ret;

  assert(maybeFile);

  // NB: there is a slight race between opening the file and truncating it here
  // but the ambiguity is allowed from the client's perspective
  // i.e. we still avoid TOCTTOU bugs

  return withExceptionCatcherNoRet(EIO, bindMethod(*maybeFile, &File::truncate),
                                   size);
}

int encfs_ftruncate(const char *path, off_t size, struct fuse_file_info *fi) {
  auto &fref = gGetFile(path, fi);
  return withExceptionCatcherNoRet(EIO, bindMethod(fref, &File::truncate),
                                   size);
}

int encfs_utimens(const char *cpath, const struct timespec ts[2]) {
  auto &fs_io = gGetFS();
  auto path = fs_io.pathFromString(cpath);

  // NB: we don't support nanosecond resolution

  return withExceptionCatcherNoRet(EIO, bindMethod(fs_io, &FsIO::set_times),
                                   path, ts[0].tv_sec, ts[1].tv_sec);
}

int encfs_open(const char *cpath, struct fuse_file_info *fi) {
  auto &fs_io = gGetFS();
  auto path = fs_io.pathFromString(cpath);

  bool requestWrite = ((fi->flags & O_RDWR) || (fi->flags & O_WRONLY));
  bool createFile = false;
  opt::optional<File> maybeFile;
  int ret = withExceptionCatcher(EIO, bindMethod(fs_io, &FsIO::openfile),
                                 &maybeFile, path, requestWrite, createFile);
  if (ret < 0) return ret;

  assert(maybeFile);

  // save the file reference
  gSaveFile(fi, std::move(*maybeFile));

  return 0;
}

int encfs_flush(const char *path, struct fuse_file_info *fi) {
  /* Flush can be called multiple times for an open file, so it doesn't
     close the file.  However it is important to call close() for some
     underlying filesystems (like NFS).

     NB: we now call FileIO::sync to fill the same purpose
     (NFS does the same thing during fsync(2) as close(2))
   */
  auto &fref = gGetFile(path, fi);
  return withExceptionCatcherNoRet(EIO, bindMethod(fref, &File::sync), false);
}

/*
Note: This is advisory -- it might benefit us to keep file nodes around for a
bit after they are released just in case they are reopened soon.  But that
requires a cache layer.
 */
int encfs_release(const char *path, struct fuse_file_info *fi) {
  gReleaseFile(path, fi);
  /* NB: return value is ignored */
  return 0;
}

int encfs_read(const char *path, char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi) {
  auto &fref = gGetFile(path, fi);

  size_t amt_read;
  int ret = withExceptionCatcher(
      EIO, bindMethod(fref, (size_t (File::*)(fs_off_t, byte *, size_t) const) &
                                File::read),
      &amt_read, (fs_off_t)offset, (byte *)buf, size);
  if (ret < 0) return ret;

  assert(amt_read <= std::numeric_limits<int>::max());
  return amt_read;
}

int encfs_fsync(const char *path, int dataSync, struct fuse_file_info *fi) {
  auto &fref = gGetFile(path, fi);
  return withExceptionCatcherNoRet(EIO, bindMethod(fref, &File::sync),
                                   dataSync);
}

int encfs_write(const char *path, const char *buf, size_t size, off_t offset,
                struct fuse_file_info *fi) {
  auto &fref = gGetFile(path, fi);

  int ret = withExceptionCatcherNoRet(
      EIO, bindMethod(fref, (void (File::*)(fs_off_t, const byte *, size_t)) &
                                File::write),
      (fs_off_t)offset, (byte *)buf, size);
  if (ret < 0) return ret;

  assert(size <= std::numeric_limits<int>::max());
  return size;
}

// statfs works even if encfs is detached..
int encfs_statfs(const char * /*path*/, struct statvfs * /*st*/) {
  /*
    res = statvfs( cyName.c_str(), st );
    if(!res)
    {
      // adjust maximum name length..
      st->f_namemax = 6 * (st->f_namemax - 2) / 8; // approx..
    }
  */
  return -ENOSYS;
}

// the same define used in fuse.h
#ifdef __APPLE__
int encfs_setxattr(const char *cpath, const char *cname, const char *value,
                   size_t size, int flags_, uint32_t position) {
  // we shouldn't get no follow in the flags since encfs
  // dispatches on inode
  assert(!(flags_ & XATTR_NOFOLLOW));

#else
int encfs_setxattr(const char *cpath, const char *cname, const char *value,
                   size_t size, int flags_) {
  uint32_t position = 0;

#endif

  PosixSetxattrFlags flags(flags_ & XATTR_CREATE, flags_ & XATTR_REPLACE);

  auto name = string(cname);
  auto buf = vector<byte>(size);
  copy((byte *)value, (byte *)value + size, buf.begin());

  /* NB: it would be nice if a smart compiler optimized these
     all to moves automatically (since they are auto variables
     and will no longer be used */
  return _do_one_path(&FsIO::posix_setxattr, cpath, NO_FOLLOW, std::move(name),
                      (size_t)position, std::move(buf), std::move(flags));
}

// the same define used in fuse.h
#ifdef __APPLE__
int encfs_getxattr(const char *cpath, const char *cname, char *value,
                   size_t size, uint32_t position) {
#else
int encfs_getxattr(const char *cpath, const char *cname, char *value,
                   size_t size) {
  uint32_t position = 0;
#endif

  auto &fs_io = gGetFS();
  auto path = fs_io.pathFromString(cpath);
  auto name = string(cname);

  opt::optional<std::vector<byte>> buf;
  int ret =
      withExceptionCatcher(EIO, bindMethod(fs_io, &FsIO::posix_getxattr), &buf,
                           path, NO_FOLLOW, name, (size_t)position, size);
  if (ret < 0) return ret;

  assert(buf);
  assert(buf->size() <= size);

  memcpy(value, buf->data(), buf->size());

  return buf->size();
}

int encfs_listxattr(const char *cpath, char *list, size_t size) {
  auto &fs_io = gGetFS();
  auto path = fs_io.pathFromString(cpath);

  opt::optional<PosixXattrList> maybeList;
  int ret = withExceptionCatcher(EIO, bindMethod(fs_io, &FsIO::posix_listxattr),
                                 &maybeList, path, NO_FOLLOW);
  if (ret < 0) return ret;

  assert(maybeList);

  size_t list_bytes_consumed = 0;
  for (const auto &xattr_name : *maybeList) {
    if (list_bytes_consumed + xattr_name.size() + 1 <= size && list) {
      memcpy(list, xattr_name.c_str(), xattr_name.size() + 1);
    }

    list_bytes_consumed += xattr_name.size() + 1;
  }

  return list_bytes_consumed;
}

int encfs_removexattr(const char *cpath, const char *name) {
  return _do_one_path(&FsIO::posix_removexattr, cpath, NO_FOLLOW, string(name));
}

}  // namespace encfs
