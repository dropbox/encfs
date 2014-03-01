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

#include <stdexcept>

#include "fs/BlockFileIO.h"

#include "base/i18n.h"
#include "base/logging.h"
#include "base/Error.h"

#include "cipher/MemoryPool.h"

#include "fs/fsconfig.pb.h"

#include <cstring>

namespace encfs {

template <typename Type>
inline Type min(Type A, Type B) {
  return (B < A) ? B : A;
}

static void clearCache(IORequest &req, int blockSize) {
  memset(req.data, 0, blockSize);
  req.dataLen = 0;
}

BlockFileIO::BlockFileIO(int blockSize, const FSConfigPtr &cfg)
    : _blockSize(blockSize), _allowHoles(cfg->config->allow_holes()) {
  rAssert(_blockSize > 1);
  _cache.data = new byte[blockSize];
}

BlockFileIO::~BlockFileIO() {
  clearCache(_cache, _blockSize);
  delete[] _cache.data;
}

ssize_t BlockFileIO::cacheReadOneBlock(const IORequest &req) const {
  // we can satisfy the request even if _cache.dataLen is too short, because
  // we always request a full block during reads..
  if ((req.offset == _cache.offset) && (_cache.dataLen != 0)) {
    // satisfy request from cache
    auto len = req.dataLen;
    if (_cache.dataLen < len) len = _cache.dataLen;
    memcpy(req.data, _cache.data, len);
    return len;
  } else {
    if (_cache.dataLen > 0) clearCache(_cache, _blockSize);

    // cache results of read -- issue reads for full blocks
    IORequest tmp;
    tmp.offset = req.offset;
    tmp.data = _cache.data;
    tmp.dataLen = _blockSize;

    ssize_t result = readOneBlock(tmp);
    if (result > 0) {
      _cache.offset = req.offset;
      _cache.dataLen = result;  // the amount we really have
      if ((size_t)result > req.dataLen)
        result = req.dataLen;  // only as much as requested
      memcpy(req.data, _cache.data, result);
    }
    return result;
  }
}

bool BlockFileIO::cacheWriteOneBlock(const IORequest &req) {
  // cache results of write (before pass-thru, because it may be modified
  // in-place)
  memcpy(_cache.data, req.data, req.dataLen);
  _cache.offset = req.offset;
  _cache.dataLen = req.dataLen;
  bool ok = writeOneBlock(req);
  if (!ok) clearCache(_cache, _blockSize);
  return ok;
}

size_t BlockFileIO::read(const IORequest &req) const {
  rAssert(_blockSize != 0);

  int partialOffset = req.offset % _blockSize;
  fs_off_t blockNum = req.offset / _blockSize;

  if (partialOffset == 0 && req.dataLen <= (size_t)_blockSize) {
    // read completely within a single block -- can be handled as-is by
    // readOneBloc().
    return cacheReadOneBlock(req);
  } else {
    ssize_t result = 0;
    auto size = req.dataLen;

    // if the request is larger then a block, then request each block
    // individually
    MemBlock mb;         // in case we need to allocate a temporary block..
    IORequest blockReq;  // for requests we may need to make
    blockReq.dataLen = _blockSize;
    blockReq.data = NULL;

    auto out = req.data;
    while (size) {
      blockReq.offset = blockNum * _blockSize;

      // if we're reading a full block, then read directly into the
      // result buffer instead of using a temporary
      if (partialOffset == 0 && size >= (decltype(size))_blockSize)
        blockReq.data = out;
      else {
        if (!mb.data) mb.allocate(_blockSize);
        blockReq.data = mb.data;
      }

      ssize_t readSize = cacheReadOneBlock(blockReq);
      if (readSize <= partialOffset) break;  // didn't get enough bytes

      int cpySize = min((size_t)(readSize - partialOffset), size);
      rAssert(cpySize <= readSize);

      // if we read to a temporary buffer, then move the data
      if (blockReq.data != out)
        memcpy(out, blockReq.data + partialOffset, cpySize);

      result += cpySize;
      size -= cpySize;
      out += cpySize;
      ++blockNum;
      partialOffset = 0;

      if (readSize < _blockSize) break;
    }

    return result;
  }
}

void BlockFileIO::write(const IORequest &req) {
  if (req.offset < 0) {
    throw std::invalid_argument("bad req argument");
  }
  rAssert(_blockSize != 0);

  auto fileSize = get_attrs().size;
  assert(fileSize >= 0);

  // where write request begins
  fs_off_t blockNum = req.offset / _blockSize;
  fs_off_t partialOffset = req.offset % _blockSize;

  // last block of file (for testing write overlaps with file boundary)
  fs_off_t lastFileBlock = fileSize / _blockSize;
  fs_off_t lastBlockSize = fileSize % _blockSize;

  fs_off_t lastNonEmptyBlock = lastFileBlock;
  if (lastBlockSize == 0) --lastNonEmptyBlock;

  if (req.offset > fileSize) {
    // extend file first to fill hole with 0's..
    const bool forceWrite = false;
    padFile(fileSize, req.offset, forceWrite);
  }

  // check against edge cases where we can just let the base class handle the
  // request as-is..
  if (partialOffset == 0 && req.dataLen <= (size_t)_blockSize) {
    // if writing a full block.. pretty safe..
    if (req.dataLen == (size_t)_blockSize) {
      const bool wrote = cacheWriteOneBlock(req);
      if (!wrote) throw std::runtime_error("couldn't write block");
      return;
    }

    // if writing a partial block, but at least as much as what is
    // already there..
    if (blockNum == lastFileBlock && req.dataLen >= (size_t)lastBlockSize) {
      const bool wrote = cacheWriteOneBlock(req);
      if (!wrote) throw std::runtime_error("couldn't write block");
      return;
    }
  }

  // have to merge data with existing block(s)..
  MemBlock mb;

  IORequest blockReq;
  blockReq.data = NULL;
  blockReq.dataLen = _blockSize;

  auto size = req.dataLen;
  auto inPtr = req.data;
  while (size) {
    blockReq.offset = blockNum * _blockSize;
    int toCopy = min((size_t)(_blockSize - partialOffset), size);

    // if writing an entire block, or writing a partial block that requires
    // no merging with existing data..
    if ((toCopy == _blockSize) ||
        (partialOffset == 0 && blockReq.offset + toCopy >= fileSize)) {
      // write directly from buffer
      blockReq.data = inPtr;
      blockReq.dataLen = toCopy;
    } else {
      // need a temporary buffer, since we have to either merge or pad
      // the data.
      if (!mb.data) mb.allocate(_blockSize);
      memset(mb.data, 0, _blockSize);
      blockReq.data = mb.data;

      if (blockNum > lastNonEmptyBlock) {
        // just pad..
        blockReq.dataLen = toCopy + partialOffset;
      } else {
        // have to merge with existing block data..
        blockReq.dataLen = _blockSize;
        blockReq.dataLen = cacheReadOneBlock(blockReq);

        // extend data if necessary..
        if (partialOffset + toCopy > (fs_off_t)blockReq.dataLen)
          blockReq.dataLen = partialOffset + toCopy;
      }
      // merge in the data to be written..
      memcpy((unsigned char *)blockReq.data + partialOffset, inPtr, toCopy);
    }

    // Finally, write the damn thing!
    if (!cacheWriteOneBlock(blockReq)) {
      // TODO: partial write... we should probably change
      //       write() API
      throw std::runtime_error("couldn't write block");
    }

    // prepare to start all over with the next block..
    size -= toCopy;
    inPtr += toCopy;
    ++blockNum;
    partialOffset = 0;
  }

  return;
}

int BlockFileIO::blockSize() const { return _blockSize; }

void BlockFileIO::padFile(fs_off_t oldSize, fs_off_t newSize, bool forceWrite) {
  fs_off_t oldLastBlock = oldSize / _blockSize;
  fs_off_t newLastBlock = newSize / _blockSize;
  int lastBlockSize = newSize % _blockSize;

  IORequest req;
  MemBlock mb;

  if (oldLastBlock == newLastBlock) {
    // when the real write occurs, it will have to read in the existing
    // data and pad it anyway, so we won't do it here (unless we're
    // forced).
    if (forceWrite) {
      mb.allocate(_blockSize);
      req.data = mb.data;

      req.offset = oldLastBlock * _blockSize;
      req.dataLen = oldSize % _blockSize;
      int outSize = newSize % _blockSize;  // outSize > req.dataLen

      if (outSize) {
        memset(mb.data, 0, outSize);
        cacheReadOneBlock(req);
        req.dataLen = outSize;
        cacheWriteOneBlock(req);
      }
    } else
      LOG(INFO) << "optimization: not padding last block";
  } else {
    mb.allocate(_blockSize);
    req.data = mb.data;

    // 1. extend the first block to full length
    // 2. write the middle empty blocks
    // 3. write the last block

    req.offset = oldLastBlock * _blockSize;
    req.dataLen = oldSize % _blockSize;

    // 1. req.dataLen == 0, iff oldSize was already a multiple of blocksize
    if (req.dataLen != 0) {
      LOG(INFO) << "padding block " << oldLastBlock;
      memset(mb.data, 0, _blockSize);
      cacheReadOneBlock(req);
      req.dataLen = _blockSize;  // expand to full block size
      cacheWriteOneBlock(req);
      ++oldLastBlock;
    }

    // 2, pad zero blocks unless holes are allowed
    if (!_allowHoles) {
      for (; oldLastBlock != newLastBlock; ++oldLastBlock) {
        LOG(INFO) << "padding block " << oldLastBlock;
        req.offset = oldLastBlock * _blockSize;
        req.dataLen = _blockSize;
        memset(mb.data, 0, req.dataLen);
        cacheWriteOneBlock(req);
      }
    }

    // 3. only necessary if write is forced and block is non 0 length
    if (forceWrite && lastBlockSize) {
      req.offset = newLastBlock * _blockSize;
      req.dataLen = lastBlockSize;
      memset(mb.data, 0, req.dataLen);
      cacheWriteOneBlock(req);
    }
  }
}

int BlockFileIO::blockTruncate(fs_off_t size, FileIO *base) {
  rAssert(size >= 0);

  int partialBlock = size % _blockSize;
  int res = 0;

  auto oldSize = get_attrs().size;
  /* NB: originally this function dealt with a truncate method
     that didn't throw exceptions. to ease the transition process
     we use this helper */
  auto truncate = wrapWithExceptionCatcher((int)std::errc::io_error,
                                           bindMethod(base, &FileIO::truncate));

  if (size > oldSize) {
    // truncate can be used to extend a file as well.  truncate man page
    // states that it will pad with 0's.
    // do the truncate so that the underlying filesystem can allocate
    // the space, and then we'll fill it in padFile..
    if (base) truncate(size);

    const bool forceWrite = true;
    padFile(oldSize, size, forceWrite);
  } else if (size == oldSize) {
    // the easiest case, but least likely....
  } else if (partialBlock) {
    // partial block after truncate.  Need to read in the block being
    // truncated before the truncate.  Then write it back out afterwards,
    // since the encoding will change..
    fs_off_t blockNum = size / _blockSize;
    MemBlock mb;
    mb.allocate(_blockSize);

    IORequest req;
    req.offset = blockNum * _blockSize;
    req.dataLen = _blockSize;
    req.data = mb.data;

    ssize_t rdSz = cacheReadOneBlock(req);

    // do the truncate
    if (base) res = truncate(size);

    // write back out partial block
    req.dataLen = partialBlock;
    bool wrRes = cacheWriteOneBlock(req);

    if ((rdSz < 0) || (!wrRes)) {
      LOG(LERROR) << "truncate failure: read size " << rdSz
                  << ", partial block of " << partialBlock;
    }
  } else {
    // truncating on a block bounday.  No need to re-encode the last
    // block..
    if (base) res = truncate(size);
  }

  return res;
}

}  // namespace encfs
