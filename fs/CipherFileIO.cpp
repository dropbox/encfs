/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2004-2013, Valient Gough
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

#include "fs/CipherFileIO.h"

#include "base/logging.h"
#include "base/Error.h"

#include "cipher/CipherV1.h"
#include "cipher/MemoryPool.h"

#include "fs/fsconfig.pb.h"

using std::shared_ptr;

namespace encfs {

/*
   Version 2:0 adds support for a per-file initialization vector with a
   fixed 8 byte header.  The headers are enabled globally within a
   filesystem at the filesystem configuration level.
   When headers are disabled, 2:0 is compatible with version 1:0.
*/
static Interface CipherFileIO_iface = makeInterface("FileIO/Cipher", 3, 0, 2);

CipherFileIO::CipherFileIO(const shared_ptr<FileIO> &_base,
                           const FSConfigPtr &cfg)
    : BlockFileIO(cfg->config->block_size(), cfg),
      base(_base),
      headerLen(0),
      perFileIV(cfg->config->unique_iv()),
      externalIV(0),
      fileIV(0) {
  fsConfig = cfg;
  cipher = cfg->cipher;

  if (perFileIV) headerLen += sizeof(uint64_t);  // 64bit IV per file

  int blockBoundary =
      fsConfig->config->block_size() % fsConfig->cipher->cipherBlockSize();
  if (blockBoundary != 0) {
    LOG(LERROR)
        << "CipherFileIO: blocks should be multiple of cipher block size";
  }
}

CipherFileIO::~CipherFileIO() {}

Interface CipherFileIO::interface() const { return CipherFileIO_iface; }

bool CipherFileIO::setIV(uint64_t iv) {
  LOG(INFO) << "in setIV, current IV = " << externalIV << ", new IV = " << iv
            << ", fileIV = " << fileIV;
  if (externalIV == 0) {
    // we're just being told about which IV to use.  since we haven't
    // initialized the fileIV, there is no need to just yet..
    externalIV = iv;
    LOG_IF(WARNING, fileIV != 0) << "fileIV initialized before externalIV! ("
                                 << fileIV << ", " << externalIV << ")";
  } else if (perFileIV) {
    // we have an old IV, and now a new IV, so we need to update the fileIV
    // on disk.

    // ensure the file is open for read/write..
    if (!isWritable()) {
      LOG(INFO) << "writeHeader failed to re-open for write";
      return false;
    }

    initHeader();

    uint64_t oldIV = externalIV;
    externalIV = iv;
    if (!writeHeader()) {
      externalIV = oldIV;
      return false;
    }
  }

  return true;
}

void CipherFileIO::setBase(const shared_ptr<FileIO> &base_) {
  base = base_;
  // NB: base must refer to the same underlying file
  //     since we can't check that, we signal to
  //     write out the header again
  fileIV = 0;
}

std::shared_ptr<FileIO> CipherFileIO::getBase() const { return base; }

FsFileAttrs CipherFileIO::wrapAttrs(int headerLen, FsFileAttrs attrs) {
  // adjust size if we have a file header
  if (attrs.type == FsFileType::REGULAR && attrs.size >= headerLen)
    attrs.size -= headerLen;

  return std::move(attrs);
}

FsFileAttrs CipherFileIO::wrapAttrs(const FSConfigPtr &cfg, FsFileAttrs attrs) {
  const fs_off_t headerLen = cfg->config->unique_iv() ? sizeof(uint64_t) : 0;

  return wrapAttrs(headerLen, std::move(attrs));
}

void CipherFileIO::ensureBase() const {
  if (!base)
    throw std::system_error((int)std::errc::io_error, std::generic_category());
}

FsFileAttrs CipherFileIO::get_attrs() const {
  ensureBase();
  return wrapAttrs(headerLen, base->get_attrs());
}

/* TODO: this should really return an error or throw an exception
   when initializing the file header fails */
void CipherFileIO::initHeader() {
  ensureBase();

  int cbs = cipher->cipherBlockSize();

  MemBlock mb;
  mb.allocate(cbs);

  // check if the file has a header, and read it if it does..  Otherwise,
  // create one.
  auto rawSize = base->get_attrs().size;
  if (rawSize >= headerLen) {
    LOG(INFO) << "reading existing header, rawSize = " << rawSize;

    IORequest req;
    req.offset = 0;
    req.data = mb.data;
    req.dataLen = sizeof(uint64_t);
    base->read(req);

    if (perFileIV) {
      cipher->streamDecode(mb.data, sizeof(uint64_t), externalIV);

      fileIV = 0;
      for (unsigned int i = 0; i < sizeof(uint64_t); ++i)
        fileIV = (fileIV << 8) | (uint64_t)mb.data[i];

      rAssert(fileIV != 0);  // 0 is never used..
    }
  } else if (perFileIV) {
    LOG(INFO) << "creating new file IV header";

    do {
      if (!cipher->pseudoRandomize(mb.data, 8))
        throw Error("Unable to generate a random file IV");

      fileIV = 0;
      for (unsigned int i = 0; i < sizeof(uint64_t); ++i)
        fileIV = (fileIV << 8) | (uint64_t)mb.data[i];

      LOG_IF(WARNING, fileIV == 0)
          << "Unexpected result: randomize returned 8 null bytes!";
    } while (fileIV == 0);  // don't accept 0 as an option..

    cipher->streamEncode(mb.data, sizeof(uint64_t), externalIV);

    IORequest req;
    req.offset = 0;
    req.data = mb.data;
    req.dataLen = sizeof(uint64_t);

    assert(base->isWritable());
    base->write(req);
  }
  LOG(INFO) << "initHeader finished, fileIV = " << fileIV;
}

bool CipherFileIO::writeHeader() {
  if (!base->isWritable()) {
    LOG(INFO) << "writeHeader failed to re-open for write";
    return false;
  }

  LOG_IF(LERROR, fileIV == 0)
      << "Internal error: fileIV == 0 in writeHeader!!!";
  LOG(INFO) << "writing fileIV " << fileIV;

  MemBlock mb;
  mb.allocate(headerLen);

  if (perFileIV) {
    unsigned char *buf = mb.data;

    assert(headerLen == sizeof(fileIV));
    for (int i = headerLen - 1; i >= 0; --i) {
      buf[i] = (unsigned char)(fileIV & 0xff);
      fileIV >>= 8;
    }

    cipher->streamEncode(buf, sizeof(uint64_t), externalIV);
  }

  IORequest req;
  req.offset = 0;
  req.data = mb.data;
  req.dataLen = headerLen;

  /* TODO: return failure if write fails */
  base->write(req);

  return true;
}

ssize_t CipherFileIO::readOneBlock(const IORequest &req) const {
  ensureBase();

  // read raw data, then decipher it..
  auto bs = blockSize();
  assert(bs >= 0);
  assert(req.offset >= 0);
  assert(!(req.offset % bs));
  rAssert(req.dataLen <= (size_t)bs);

  fs_off_t blockNum = req.offset / bs;

  ssize_t readSize = 0;
  IORequest tmpReq = req;

  MemBlock mb;
  tmpReq.offset += headerLen;

  int maxReadSize = req.dataLen;
  readSize = base->read(tmpReq);

  if (readSize > 0) {
    bool ok;
    if (headerLen != 0 && fileIV == 0)
      const_cast<CipherFileIO *>(this)->initHeader();

    if (readSize == bs) {
      ok = blockRead(tmpReq.data, bs, blockNum ^ fileIV);
    } else {
      ok = streamRead(tmpReq.data, (int)readSize, blockNum ^ fileIV);
    }

    if (!ok) {
      LOG(INFO) << "decodeBlock failed for block " << blockNum << ", size "
                << readSize;
      readSize = -1;
    } else if (tmpReq.data != req.data) {
      if (readSize > maxReadSize) readSize = maxReadSize;
      memcpy(req.data, tmpReq.data, readSize);
    }
  } else
    LOG(INFO) << "readSize zero for offset " << req.offset;

  return readSize;
}

bool CipherFileIO::writeOneBlock(const IORequest &req) {
  ensureBase();

  auto bs = blockSize();
  fs_off_t blockNum = req.offset / bs;

  if (headerLen != 0 && fileIV == 0) initHeader();

  MemBlock mb;

  bool ok;
  assert(bs >= 0);
  if (req.dataLen == (size_t)bs) {
    ok = blockWrite(req.data, bs, blockNum ^ fileIV);
  } else {
    ok = streamWrite(req.data, (int)req.dataLen, blockNum ^ fileIV);
  }

  if (ok) {
    IORequest nreq = req;

    if (headerLen != 0) {
      if (mb.data == NULL) {
        nreq.offset += headerLen;
      } else {
        // Partial block is stored at front of file.
        nreq.offset = 0;
        nreq.data = mb.data;
        nreq.dataLen = bs;
        base->truncate(req.offset + req.dataLen + headerLen);
      }
    }

    try {
      base->write(nreq);
    }
    catch (...) {
      ok = false;
    }
  } else {
    LOG(INFO) << "encodeBlock failed for block " << blockNum << ", size "
              << req.dataLen;
  }

  return ok;
}

bool CipherFileIO::blockWrite(byte *buf, size_t size, uint64_t _iv64) const {
  if (!fsConfig->reverseEncryption)
    return cipher->blockEncode((byte *)buf, size, _iv64);
  else
    return cipher->blockDecode((byte *)buf, size, _iv64);
}

bool CipherFileIO::streamWrite(byte *buf, size_t size, uint64_t _iv64) const {
  if (!fsConfig->reverseEncryption)
    return cipher->streamEncode(buf, size, _iv64);
  else
    return cipher->streamDecode(buf, size, _iv64);
}

bool CipherFileIO::blockRead(byte *buf, size_t size, uint64_t _iv64) const {
  if (fsConfig->reverseEncryption)
    return cipher->blockEncode(buf, size, _iv64);
  else if (_allowHoles) {
    // special case - leave all 0's alone
    for (decltype(size) i = 0; i < size; ++i)
      if (buf[i] != 0) return cipher->blockDecode(buf, size, _iv64);

    return true;
  } else
    return cipher->blockDecode(buf, size, _iv64);
}

bool CipherFileIO::streamRead(byte *buf, size_t size, uint64_t _iv64) const {
  if (fsConfig->reverseEncryption)
    return cipher->streamEncode(buf, size, _iv64);
  else
    return cipher->streamDecode(buf, size, _iv64);
}

void CipherFileIO::truncate(fs_off_t size) {
  ensureBase();

  rAssert(size >= 0);
  if (!isWritable()) {
    LOG(INFO) << "writeHeader failed to re-open for write";
    throw std::runtime_error("file not opened for writing");
  }

  if (headerLen == 0) {
    auto res = blockTruncate(size, base.get());
    if (res < 0) throw std::runtime_error("blockTruncate() failed");
  } else if (0 == fileIV) {
    initHeader();
  }

  // can't let BlockFileIO call base->truncate(), since it would be using
  // the wrong size..
  int res = blockTruncate(size, 0);
  if (res == 0)
    base->truncate(size + headerLen);
  else
    throw std::runtime_error("blockTruncate() failed");
}

bool CipherFileIO::isWritable() const {
  if (!base) return false;
  return base->isWritable();
}

void CipherFileIO::sync(bool a) {
  ensureBase();
  return base->sync(a);
}

}  // namespace encfs
