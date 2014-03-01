/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2004, Valient Gough
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
#define _XOPEN_SOURCE 500  // pick up pread , pwrite
#endif

#include "encfs/RawFileIO.h"

#include "base/logging.h"
#include "base/Error.h"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

namespace encfs {

static Interface RawFileIO_iface = makeInterface("FileIO/Raw", 1, 0, 0);

inline void swap(int &x, int &y) {
  int tmp = x;
  x = y;
  y = tmp;
}

RawFileIO::RawFileIO(const std::string &fileName)
    : name(fileName), fd(-1), canWrite(false) {}

RawFileIO::~RawFileIO() {
  int _fd = -1;

  swap(_fd, fd);

  if (_fd != -1) {
    int ret = close(_fd);
    LOG_IF(WARNING, ret) << "Close failed, leaking file descriptor!";
  }
}

Interface RawFileIO::interface() const { return RawFileIO_iface; }

/* wrapper for posix open */
int RawFileIO::open(int flags, mode_t mode) {
  if (fd >= 0) throw std::runtime_error("already opened!");

  bool requestWrite = ((flags & O_RDWR) || (flags & O_WRONLY));
  int newFd = flags & O_CREAT ? ::open(name.c_str(), flags, mode)
                              : ::open(name.c_str(), flags);

  int result;
  if (newFd >= 0) {
    canWrite = requestWrite;
    result = fd = newFd;
  } else {
    result = -errno;
    LOG(INFO) << "::open error: " << strerror(errno);
  }

  LOG_IF(INFO, result < 0) << "file " << name << " open failure: " << -result;

  return result;
}

FsFileAttrs RawFileIO::get_attrs() const {
  struct stat st;
  memset(&st, 0, sizeof(struct stat));
  int res = fstat(fd, &st);

  if (res < 0) {
    int eno = errno;
    LOG_IF(INFO, res < 0) << "getAttr error on " << name << ": "
                          << strerror(eno);
    throw std::system_error(eno, errno_category());
  }

  return stat_to_fs_file_attrs(st);
}

size_t RawFileIO::read(const IORequest &req) const {
  rAssert(fd >= 0);

  LOG(INFO) << "Read " << req.dataLen << " bytes from offset " << req.offset;
  ssize_t readSize = pread(fd, req.data, req.dataLen, req.offset);

  if (readSize < 0) {
    int eno = errno;
    LOG(INFO) << "read failed at offset " << req.offset << " for "
              << req.dataLen << " bytes: " << strerror(errno);
    throw std::system_error(eno, errno_category());
  }

  return readSize;
}

void RawFileIO::write(const IORequest &req) {
  rAssert(fd >= 0);
  rAssert(true == canWrite);

  LOG(INFO) << "Write " << req.dataLen << " bytes to offset " << req.offset;

  int retrys = 10;
  void *buf = req.data;
  size_t bytes = req.dataLen;
  fs_off_t offset = req.offset;

  while (bytes && retrys > 0) {
    ssize_t writeSize = ::pwrite(fd, buf, bytes, offset);

    if (writeSize < 0) {
      if (errno == EINTR) continue;

      // NB: this is really bad, we potentially already wrote some
      //     data. we should probably just keep iterating
      //     or change this api to return how much was actually written
      int eno = errno;
      LOG(INFO) << "write failed at offset " << offset << " for " << bytes
                << " bytes: " << strerror(eno);
      throw std::system_error(eno, errno_category());
    }

    assert(bytes >= (size_t)writeSize);
    bytes -= writeSize;
    offset += writeSize;
    buf = (void *)((char *)buf + writeSize);
    --retrys;
  }

  if (bytes != 0) {
    LOG(LERROR) << "Write error: wrote " << (req.dataLen - bytes)
                << " bytes of " << req.dataLen << ", max retries reached";
    throw std::system_error(EIO, errno_category());
  }
}

void RawFileIO::truncate(fs_off_t size) {
  int res;

  if (fd >= 0 && canWrite) {
    res = ::ftruncate(fd, size);
#ifdef linux
    ::fdatasync(fd);
#endif
  } else
    res = ::truncate(name.c_str(), size);

  if (res < 0) {
    int eno = errno;
    LOG(INFO) << "truncate failed for " << name << " (" << fd << ") size "
              << size << ", error " << strerror(eno);
    throw std::system_error(eno, errno_category());
  }
}

bool RawFileIO::isWritable() const {
  if (fd < 0) throw std::runtime_error("file not open!");
  return canWrite;
}

void RawFileIO::sync(bool datasync) {
  int res = -EIO;
#ifdef linux
  if (datasync)
    res = fdatasync(fd);
  else
    res = fsync(fd);
#else
  (void)datasync;
  // no fdatasync support
  // TODO: use autoconfig to check for it..
  res = fsync(fd);
#endif
  if (res < 0) throw std::system_error(errno, errno_category());
}

FsFileAttrs stat_to_fs_file_attrs(const struct stat &st) {
  FsFileAttrs attrs;

  attrs.type =
      (S_ISDIR(st.st_mode) ? FsFileType::DIRECTORY : S_ISREG(st.st_mode)
                                                         ? FsFileType::REGULAR
                                                         : FsFileType::UNKNOWN);
  attrs.mtime = {st.st_mtime};
  attrs.size = {st.st_size};
  attrs.file_id = {st.st_ino};
  attrs.volume_id = {st.st_dev};
  attrs.posix = FsPosixAttrs(st.st_mode, st.st_uid, st.st_gid);

  return attrs;
}

}  // namespace encfs
