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

#include "fs/MACFileIO.h"

#include "base/i18n.h"
#include "base/logging.h"
#include "base/Error.h"

#include "cipher/MemoryPool.h"

#include "fs/FileUtils.h"
#include "fs/fsconfig.pb.h"

#include <memory>

#include <cstring>

using std::shared_ptr;

namespace encfs {

//
// Version 1.0 worked on blocks of size (blockSize + headerSize).
//   That is, it took [blockSize] worth of user data and added headers.
// Version 2.0 takes [blockSize - headerSize] worth of user data and writes
//   [blockSize] bytes.  That way the size going into the crypto engine is
//   valid from what was selected based on the crypto module allowed ranges!
// Version 2.1 allows per-block rand bytes to be used without enabling MAC.
//
// The information about MACFileIO currently does not make its way into the
// configuration file, so there is no easy way to make this backward
// compatible, except at a high level by checking a revision number for the
// filesystem...
//
static Interface MACFileIO_iface = makeInterface("FileIO/MAC", 2, 1, 0);

int dataBlockSize(const FSConfigPtr &cfg) {
  return cfg->config->block_size() - cfg->config->block_mac_bytes() -
         cfg->config->block_mac_rand_bytes();
}

MACFileIO::MACFileIO(const shared_ptr<FileIO> &_base, const FSConfigPtr &cfg)
    : BlockFileIO(dataBlockSize(cfg), cfg),
      base(_base),
      cipher(cfg->cipher),
      macBytes(cfg->config->block_mac_bytes()),
      randBytes(cfg->config->block_mac_rand_bytes()),
      warnOnly(cfg->opts->forceDecode) {
  rAssert(macBytes >= 0 && macBytes <= 8);
  rAssert(randBytes >= 0);
  LOG(INFO) << "fs block size = " << cfg->config->block_size()
            << ", macBytes = " << cfg->config->block_mac_bytes()
            << ", randBytes = " << cfg->config->block_mac_rand_bytes();
}

MACFileIO::~MACFileIO() {}

Interface MACFileIO::interface() const { return MACFileIO_iface; }

inline static fs_off_t roundUpDivide(fs_off_t numerator, int denominator) {
  // integer arithmetic always rounds down, so we can round up by adding
  // enough so that any value other then a multiple of denominator gets
  // rouned to the next highest value.
  return (numerator + denominator - 1) / denominator;
}

// Convert from a location in the raw file to a location when MAC headers are
// interleved with the data.
// So, if the filesystem stores/encrypts [blockSize] bytes per block, then
//  [blockSize - headerSize] of those bytes will contain user-supplied data,
//  and the rest ([headerSize]) will contain the MAC header for this block.
// Example, offset points to second block (of user-data)
//   offset = blockSize - headerSize
//   ... blockNum = 1
//   ... partialBlock = 0
//   ... adjLoc = 1 * blockSize
static fs_off_t locWithHeader(fs_off_t offset, int blockSize, int headerSize) {
  fs_off_t blockNum = roundUpDivide(offset, blockSize - headerSize);
  return offset + blockNum * headerSize;
}

// convert from a given location in the stream containing headers, and return a
// location in the user-data stream (which doesn't contain MAC headers)..
// The output value will always be less then the input value, because the
// headers are stored at the beginning of the block, so even the first data is
// offset by the size of the header.
static fs_off_t locWithoutHeader(fs_off_t offset, int blockSize,
                                 int headerSize) {
  fs_off_t blockNum = roundUpDivide(offset, blockSize);
  return offset - blockNum * headerSize;
}

FsFileAttrs MACFileIO::wrapAttrs(int blockSize, int macBytes, int randBytes,
                                 FsFileAttrs attrs) {

  if (attrs.type == FsFileType::REGULAR) {
    // have to adjust size field..
    int headerSize = macBytes + randBytes;
    int bs = blockSize + headerSize;
    attrs.size = locWithoutHeader(attrs.size, bs, headerSize);
  }

  return std::move(attrs);
}

FsFileAttrs MACFileIO::wrapAttrs(const FSConfigPtr &cfg, FsFileAttrs attrs) {
  return wrapAttrs(dataBlockSize(cfg), cfg->config->block_mac_bytes(),
                   cfg->config->block_mac_rand_bytes(), std::move(attrs));
}

FsFileAttrs MACFileIO::get_attrs() const {
  return wrapAttrs(blockSize(), macBytes, randBytes, base->get_attrs());
}

ssize_t MACFileIO::readOneBlock(const IORequest &req) const {
  assert(blockSize() >= 0);
  assert(req.offset >= 0);
  assert(!(req.offset % blockSize()));
  rAssert(req.dataLen <= (size_t)blockSize());

  int headerSize = macBytes + randBytes;

  int bs = blockSize() + headerSize;
  MemBlock mb;
  mb.allocate(bs);

  IORequest tmp;
  tmp.offset = locWithHeader(req.offset, bs, headerSize);
  tmp.data = mb.data;
  tmp.dataLen = headerSize + req.dataLen;

  // get the data from the base FileIO layer
  ssize_t readSize = base->read(tmp);

  // don't store zeros if configured for zero-block pass-through
  bool skipBlock = true;
  if (_allowHoles) {
    for (int i = 0; i < readSize; ++i)
      if (tmp.data[i] != 0) {
        skipBlock = false;
        break;
      }
  } else if (macBytes > 0)
    skipBlock = false;

  if (readSize > headerSize) {
    if (!skipBlock) {
      // At this point the data has been decoded.  So, compute the MAC of
      // the block and check against the checksum stored in the header..
      uint64_t mac = cipher->MAC_64(tmp.data + macBytes, readSize - macBytes);

      for (int i = 0; i < macBytes; ++i, mac >>= 8) {
        int test = mac & 0xff;
        int stored = tmp.data[i];
        if (test != stored) {
          // uh oh..
          long blockNum = req.offset / bs;
          LOG(WARNING) << "MAC comparison failure in block " << blockNum;
          if (!warnOnly) {
            throw Error(_("MAC comparison failure, refusing to read"));
          }
          break;
        }
      }
    }

    // now copy the data to the output buffer
    readSize -= headerSize;
    memcpy(req.data, tmp.data + headerSize, readSize);
  } else {
    LOG(INFO) << "readSize " << readSize << " at offset " << req.offset;
    if (readSize > 0) readSize = 0;
  }

  return readSize;
}

bool MACFileIO::writeOneBlock(const IORequest &req) {
  int headerSize = macBytes + randBytes;

  int bs = blockSize() + headerSize;

  // we have the unencrypted data, so we need to attach a header to it.
  MemBlock mb;
  mb.allocate(bs);

  IORequest newReq;
  newReq.offset = locWithHeader(req.offset, bs, headerSize);
  newReq.data = mb.data;
  newReq.dataLen = headerSize + req.dataLen;

  memset(newReq.data, 0, headerSize);
  memcpy(newReq.data + headerSize, req.data, req.dataLen);
  if (randBytes > 0) {
    if (!cipher->pseudoRandomize(newReq.data + macBytes, randBytes))
      return false;
  }

  if (macBytes > 0) {
    // compute the mac (which includes the random data) and fill it in
    uint64_t mac =
        cipher->MAC_64(newReq.data + macBytes, req.dataLen + randBytes);

    for (int i = 0; i < macBytes; ++i) {
      newReq.data[i] = mac & 0xff;
      mac >>= 8;
    }
  }

  // now, we can let the next level have it..
  try {
    base->write(newReq);
    return true;
  }
  catch (...) {
    return false;
  }
}

void MACFileIO::truncate(fs_off_t size) {
  int headerSize = macBytes + randBytes;
  int bs = blockSize() + headerSize;

  int res = blockTruncate(size, 0);

  if (res == 0)
    base->truncate(locWithHeader(size, bs, headerSize));
  else
    throw std::runtime_error("error calling blockTruncate()");
}

bool MACFileIO::isWritable() const { return base->isWritable(); }

void MACFileIO::sync(bool datasync) { return base->sync(datasync); }

}  // namespace encfs
