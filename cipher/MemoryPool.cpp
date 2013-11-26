/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2003-2013, Valient Gough
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

#include "base/config.h"

#include "cipher/MemoryPool.h"

#include "base/logging.h"
#include "base/Error.h"

#ifdef HAVE_VALGRIND_MEMCHECK_H
#include <valgrind/memcheck.h>
#else
#define VALGRIND_MAKE_MEM_NOACCESS(a, b)
#define VALGRIND_MAKE_MEM_UNDEFINED(a, b)
#endif

#ifdef WITH_OPENSSL
#include <openssl/crypto.h>
#include <openssl/buffer.h>
#endif

#ifdef WITH_BOTAN
#include <botan/botan.h>
#include <botan/version.h>
#endif

#include <map>
#include <list>

#include <cstdlib>
#include <cstring>

// for our custom SecureMem implementation
// (only implemented for posix machines currently)
#ifndef WITH_BOTAN
#include <sys/mman.h>
#endif

namespace encfs {

#ifdef WITH_OPENSSL
static byte *allocBlock(int size) {
  byte *block = (byte *)OPENSSL_malloc(size);
  return block;
}

static void freeBlock(byte *block, int size) {
  OPENSSL_cleanse(block, size);
  OPENSSL_free(block);
}

#else

static byte *allocBlock(int size) {
  byte *block = new byte[size];
  return block;
}

unsigned char cleanse_ctr = 0;
static void freeBlock(byte *data, int len) {
  byte *p = data;
  size_t loop = len, ctr = cleanse_ctr;
  while (loop--) {
    *(p++) = (unsigned char)ctr;
    ctr += (17 + ((size_t)p & 0xF));
  }
  // Try to ensure the compiler doesn't optimize away the loop.
  p = (byte *)memchr(data, (unsigned char)ctr, len);
  if (p) ctr += (63 + (size_t)p);
  cleanse_ctr = (unsigned char)ctr;
  delete[] data;
}

#endif

void MemBlock::allocate(int size) {
  rAssert(size > 0);
  this->data = allocBlock(size);
  this->size = size;
}

MemBlock::~MemBlock() { freeBlock(data, size); }

#ifdef WITH_BOTAN
SecureMem::SecureMem(int len)
    : data_(len > 0 ? new Botan::SecureVector<unsigned char>(len) : nullptr) {
  rAssert(len >= 0);
}

void SecureMem::_kill_data() {
  if (data_) {
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1, 11, 0)
    data_->destroy();
#endif
    delete data_;
  }
}

SecureMem::~SecureMem() { _kill_data(); }

byte *SecureMem::data() const {
  return data_ ? const_cast<byte *>(data_->begin()) : nullptr;
}

int SecureMem::size() const { return data_ ? data_->size() : 0; }

SecureMem &SecureMem::operator=(const SecureMem &sm) {
  /* same poitner, no need to copy */
  if (&sm == this) return *this;

  /* first destroy our data */
  _kill_data();

  data_ = new Botan::SecureVector<unsigned char>(sm.size());
  memmove(this->data(), sm.data(), sm.size());

  return *this;
}

SecureMem::SecureMem(const SecureMem &sm) : data_(nullptr) { *this = sm; }

SecureMem &SecureMem::operator=(SecureMem &&sm) {
  /* same poitner, no need to move */
  if (&sm == this) return *this;

  /* first destroy our data */
  _kill_data();

  /* then take the pointer from the other one */
  data_ = sm.data_;
  sm.data_ = nullptr;

  return *this;
}

SecureMem::SecureMem(SecureMem &&sm) : data_(nullptr) { *this = std::move(sm); }

#else

void SecureMem::_kill_data() {
  if (size_) {
    freeBlock(data_, size_);
    munlock(data_, size_);

    data_ = NULL;
    size_ = 0;
  }
}

SecureMem::SecureMem(int len) {
  rAssert(len >= 0);
  data_ = len ? allocBlock(len) : nullptr;
  if (data_) {
    size_ = len;
    mlock(data_, size_);
  } else {
    size_ = 0;
  }
}

SecureMem::~SecureMem() { _kill_data(); }

SecureMem &SecureMem::operator=(const SecureMem &sm) {
  /* same poitner, no need to copy */
  if (&sm == this) return *this;

  /* first destroy our data */
  _kill_data();

  if (sm.data_) {
    rAssert(sm.size_);
    data_ = allocBlock(sm.size_);
    if (!data_) throw std::runtime_error("bad alloc");
    size_ = sm.size_;
    mlock(data_, size_);
    memmove(this->data(), sm.data_, size_);
  }

  return *this;
}

SecureMem::SecureMem(const SecureMem &sm) : data_(nullptr), size_(0) {
  *this = sm;
}

SecureMem &SecureMem::operator=(SecureMem &&sm) {
  /* same poitner, no need to copy */
  if (&sm == this) return *this;

  /* first destroy our data */
  _kill_data();

  if (sm.data_) {
    data_ = sm.data_;
    size_ = sm.size_;
    sm.data_ = NULL;
    sm.size_ = 0;
  }

  return *this;
}

SecureMem::SecureMem(SecureMem &&sm) : data_(nullptr), size_(0) {
  *this = std::move(sm);
}

#endif

bool operator==(const SecureMem &a, const SecureMem &b) {
  return (a.size() == b.size()) && (memcmp(a.data(), b.data(), a.size()) == 0);
}

}  // namespace encfs
