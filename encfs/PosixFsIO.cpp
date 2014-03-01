/*****************************************************************************
 * Author:   Rian Hunter <rian@alum.mit.edu>
 *
 *****************************************************************************
 * Copyright (c) 2013, Dropbox, Inc.
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

#include "encfs/xattr.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "base/optional.h"

#include "encfs/RawFileIO.h"
#include "fs/FsIO.h"

#include "encfs/PosixFsIO.h"

// TODO: check for existence of functions at build configuration time
//       don't do it conditionally on linux
#ifndef linux
int setfsuid(uid_t uid) {
  uid_t olduid = geteuid();

  int res = seteuid(uid);
  if (res == -1) return res;

  return olduid;
}

int setfsgid(gid_t gid) {
  gid_t oldgid = getegid();

  int res = setegid(gid);
  if (res == -1) return res;

  return oldgid;
}
#endif

using std::shared_ptr;

namespace encfs {

static void current_fs_error(int thiserror = -1) {
  if (thiserror < 0) thiserror = errno;
  throw std::system_error(thiserror, errno_category());
}

extern const char POSIX_PATH_SEP[] = "/";
class PosixPath final : public StringPath<PosixPath, POSIX_PATH_SEP> {
 protected:
  virtual std::unique_ptr<PathPoly> _from_string(std::string str) const {
    return std::unique_ptr<PosixPath>(new PosixPath(std::move(str)));
  }

  virtual bool _filename_valid(const std::string &a) const {
    return !a.empty() && a.find('/') == std::string::npos;
  }

 public:
  PosixPath(std::string str) : StringPath(std::move(str)) {
    if (((const std::string &)*this).empty())
      throw std::runtime_error("EMPTY STRING IS BAD");
  }

  virtual std::unique_ptr<PathPoly> dirname() const override {
    const auto &str = (const std::string &)*this;

    auto slash_pos = str.rfind('/');

    if (!slash_pos) {
      return _from_string(POSIX_PATH_SEP);
    }

    return _from_string(str.substr(0, slash_pos));
  }

  virtual bool is_root() const {
    return (const std::string &)*this == POSIX_PATH_SEP;
  }
};

class PosixDirectoryIO final : public DirectoryIO {
 private:
  DIR *_dirp;
  // Only PosixFsIO can instantiate this class
  PosixDirectoryIO(DIR *dirp) noexcept : _dirp(dirp) {}

 public:
  virtual ~PosixDirectoryIO() override;
  virtual opt::optional<FsDirEnt> readdir() override;

  friend class PosixFsIO;
};

PosixDirectoryIO::~PosixDirectoryIO() {
  const int ret = ::closedir(_dirp);
  if (ret < 0) {
    /* this can't fail */
    abort();
  }
}

opt::optional<FsDirEnt> PosixDirectoryIO::readdir() {
  while (true) {
    errno = 0;
    struct dirent *const de = ::readdir(_dirp);
    if (!de) {
      if (errno)
        current_fs_error();
      else {
        break;
      }
    }

    if ((de->d_name[0] == '.') &&
        ((de->d_name[1] == '\0') ||
         ((de->d_name[1] == '.') && (de->d_name[2] == '\0')))) {
      // skip "." and ".."
      continue;
    }

    auto toret = FsDirEnt(de->d_name, de->d_ino);
    switch (de->d_type) {
      case DT_REG:
        toret.type = FsFileType::REGULAR;
        break;
      case DT_DIR:
        toret.type = FsFileType::DIRECTORY;
        break;
    }

    return toret;
  }

  return opt::nullopt;
}

static bool endswith(const std::string &haystack, const std::string &needle) {
  if (needle.length() > haystack.length()) {
    return false;
  }

  return !haystack.compare(haystack.length() - needle.length(), needle.length(),
                           needle);
}

Path PosixFsIO::pathFromString(const std::string &path) const {
  if (path.empty()) throw std::runtime_error("EMPTY STRING IS NOT ALLOWED");

  /* TODO: throw exception if path is not a UTF-8 posix path */
  if (path[0] != '/')
    throw std::runtime_error("Not absolute path: \"" + path + "\"");

  if (path == "/") return std::unique_ptr<PosixPath>(new PosixPath(path));

  std::string newpath = path;
  while (endswith(newpath, "/")) {
    newpath = path.substr(0, path.length() - 1);
  }

  return std::unique_ptr<PosixPath>(new PosixPath(std::move(newpath)));
}

Directory PosixFsIO::opendir(const Path &path) const {
  DIR *const res = ::opendir(path.c_str());
  if (!res) current_fs_error();

  return std::unique_ptr<PosixDirectoryIO>(new PosixDirectoryIO(res));
}

File PosixFsIO::openfile(const Path &path, bool open_for_write, bool create) {
  auto file_ = std::unique_ptr<RawFileIO>(new RawFileIO(path));
  int flags = (open_for_write ? O_RDWR : O_RDONLY) | (create ? O_CREAT : 0);
  const int ret_open = file_->open(flags, 0777);
  if (ret_open < 0) current_fs_error(-ret_open);
  return std::move(file_);
}

void PosixFsIO::mkdir(const Path &path) { posix_mkdir(path, 0777); }

void PosixFsIO::rename(const Path &path_src, const Path &path_dst) {
  const int res = ::rename(path_src.c_str(), path_dst.c_str());
  if (res < 0) current_fs_error();
}

void PosixFsIO::unlink(const Path &path) {
  const int res = ::unlink(path.c_str());
  if (res < 0) current_fs_error();
}

void PosixFsIO::rmdir(const Path &path) {
  const int res = ::rmdir(path.c_str());
  if (res < 0) current_fs_error();
}

FsFileAttrs PosixFsIO::get_attrs(const Path &path) const {
  return posix_stat(path, /* follow =*/true);
}

void PosixFsIO::set_times(const Path &path,
                          const opt::optional<fs_time_t> &atime,
                          const opt::optional<fs_time_t> &mtime) {
  struct timeval now;
  struct timeval new_times[2];

  if (!atime || !mtime) {
    const int res_gettimeofday = ::gettimeofday(&now, nullptr);
    if (res_gettimeofday < 0) current_fs_error();
  }

  typedef decltype(new_times[0].tv_sec) _second_type;

  if (atime && (*atime > std::numeric_limits<_second_type>::max() ||
                *atime < std::numeric_limits<_second_type>::lowest())) {
    current_fs_error(EINVAL);
  }

  new_times[0] = atime ? (struct timeval) {(_second_type) * atime, 0} : now;

  if (mtime && (*mtime > std::numeric_limits<_second_type>::max() ||
                *mtime < std::numeric_limits<_second_type>::lowest())) {
    current_fs_error(EINVAL);
  }

  new_times[1] = mtime ? (struct timeval) {(_second_type) * mtime, 0} : now;

  const int res_utimes = ::utimes(path.c_str(), new_times);
  if (res_utimes < 0) current_fs_error();
}

fs_posix_uid_t PosixFsIO::posix_setfsuid(fs_posix_uid_t uid) {
  const int res = ::setfsuid(uid);
  if (res < 0) current_fs_error();
  return res;
}

fs_posix_gid_t PosixFsIO::posix_setfsgid(fs_posix_gid_t gid) {
  const int res = ::setfsgid(gid);
  if (res < 0) current_fs_error();
  return res;
}

File PosixFsIO::posix_create(const Path &path, fs_posix_mode_t mode) {
  auto file_ = std::unique_ptr<RawFileIO>(new RawFileIO(path));
  int flags = O_CREAT | O_TRUNC | O_WRONLY;
  const int ret_open = file_->open(flags, mode);
  if (ret_open < 0) current_fs_error(-ret_open);
  return std::move(file_);
}

void PosixFsIO::posix_mkdir(const Path &path, fs_posix_mode_t mode) {
  const int res = ::mkdir(path.c_str(), mode);
  if (res < 0) current_fs_error();
}

void PosixFsIO::posix_mknod(const Path &path, fs_posix_mode_t mode,
                            fs_posix_dev_t rdev) {
  int res = ::mknod(path.c_str(), mode, rdev);
  if (res == -1) current_fs_error();
}

void PosixFsIO::posix_link(const Path &path_src, const Path &path_dst) {
  const int res = ::link(path_src.c_str(), path_dst.c_str());
  if (res < 0) current_fs_error();
}

void PosixFsIO::posix_symlink(const Path &path, PosixSymlinkData link_data) {
  const int res = ::symlink(link_data.c_str(), path.c_str());
  if (res < 0) current_fs_error();
}

PosixSymlinkData PosixFsIO::posix_readlink(const Path &path) const {
  // TODO: could this be allocated dynamically?
  char buf[PATH_MAX];
  const ssize_t res = ::readlink(path.c_str(), buf, sizeof(buf));
  if (res < 0) current_fs_error();

  // make sure nothing returned from readlink has a null byte
  assert(std::all_of(buf, buf + res, [](decltype(buf[0]) elt) { return elt; }));

  return PosixSymlinkData(buf, (size_t)res);
}

void PosixFsIO::posix_chmod(const Path &path, bool follow,
                            fs_posix_mode_t mode) {
  if (!follow) return current_fs_error(ENOSYS);
  const int res = ::chmod(path.c_str(), mode);
  if (res < 0) current_fs_error();
}

void PosixFsIO::posix_chown(const Path &path, bool follow, fs_posix_uid_t uid,
                            fs_posix_gid_t gid) {
  if (!follow) return current_fs_error(ENOSYS);
  const int res = ::chown(path.c_str(), uid, gid);
  if (res < 0) current_fs_error();
}

#ifdef HAVE_XATTR

void PosixFsIO::posix_setxattr(const Path &path, bool follow, std::string name,
                               size_t offset, std::vector<byte> buf,
                               PosixSetxattrFlags flags) {
  int options =
      (flags.replace ? XATTR_REPLACE : 0) | (flags.create ? XATTR_CREATE : 0);

#ifdef XATTR_ADD_OPT
  if (!follow) options |= XATTR_NOFOLLOW;
  const int res = ::setxattr(path.c_str(), name.c_str(), (void *)buf.data(),
                             buf.size(), (u_int32_t)offset, options);
#else
  // we don't support offset writes on linux yet
  if (offset) throw std::runtime_error("not supported");

  const auto fn = follow ? &::setxattr : &::lsetxattr;
  const int res =
      fn(path.c_str(), name.c_str(), (void *)buf.data(), buf.size(), options);
#endif

  if (res < 0) current_fs_error();
}

std::vector<byte> PosixFsIO::posix_getxattr(const Path &path, bool follow,
                                            std::string name, size_t offset,
                                            size_t amt) const {
#ifdef XATTR_ADD_OPT
  int options = follow ? 0 : XATTR_NOFOLLOW;
  std::vector<byte> data(amt);
  int res = ::getxattr(path.c_str(), name.c_str(), data.data(), data.size(),
                       offset, options);
#else
  // we don't support offset reads on linux yet
  if (offset) throw std::runtime_error("not supported");

  const auto fn = follow ? &::getxattr : &::lgetxattr;
  std::vector<byte> data(amt);
  int res = fn(path.c_str(), name.c_str(), data.data(), data.size());
#endif

  if (res < 0) current_fs_error();

  return std::move(data);
}

PosixXattrList PosixFsIO::posix_listxattr(const Path &path, bool follow) const {
  using namespace std::placeholders;

#ifdef XATTR_ADD_OPT
  int options = follow ? 0 : XATTR_NOFOLLOW;
  const auto fn = std::bind(&::listxattr, path.c_str(), _1, _2, options);
#else
  const auto fn =
      std::bind(follow ? &::listxattr : &::llistxattr, path.c_str(), _1, _2);
#endif

  ssize_t ret = fn(nullptr, 0);
  if (ret < 0) current_fs_error();

  std::unique_ptr<char[]> buf(new char[ret]);

  ret = fn(buf.get(), ret);
  if (ret < 0) current_fs_error();

  PosixXattrList toret;
  char *searchp = buf.get();
  while (searchp < (buf.get() + ret)) {
    toret.emplace_back(searchp);
    searchp = strchr(searchp, '\0') + 1;
  }

  return std::move(toret);
}

void PosixFsIO::posix_removexattr(const Path &path, bool follow,
                                  std::string name) {
#ifdef XATTR_ADD_OPT
  int options = follow ? 0 : XATTR_NOFOLLOW;
  const int ret = ::removexattr(path.c_str(), name.c_str(), options);
#else
  const auto fn = follow ? &::removexattr : &::lremovexattr;
  const int ret = fn(path.c_str(), name.c_str());
#endif

  if (ret < 0) current_fs_error();
}

#endif

FsFileAttrs PosixFsIO::posix_stat(const Path &path, bool follow) const {
  const auto fn = follow ? &::stat : &::lstat;
  struct stat st;
  const int ret = fn(path.c_str(), &st);
  if (ret < 0) current_fs_error();
  return stat_to_fs_file_attrs(st);
}

}  // namespace encfs
