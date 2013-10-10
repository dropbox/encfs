/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2003-2004, Valient Gough
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

#include "fs/FileNode.h"

#include "fs/Context.h"
#include "fs/CipherFileIO.h"
#include "fs/DirNode.h"
#include "fs/FileIO.h"
#include "fs/FileUtils.h"
#include "fs/FsIO.h"
#include "fs/MACFileIO.h"
#include "fs/fsconfig.pb.h"

#include "base/Error.h"
#include "base/Mutex.h"
#include "cipher/MemoryPool.h"

#include <glog/logging.h>

#include <memory>

#include <cerrno>
#include <cstring>

using std::string;
using std::shared_ptr;

namespace encfs {

/*
   TODO: locking at the FileNode level is inefficient, since this precludes
   multiple concurrent IO operations within the same file.

   There is no reason why simultainous reads cannot be satisfied, or why one
   read has to wait for the decoding of the previous read before it can be
   sent to the IO subsystem!

   -> a "Read-Write" Lock can increase parallelism.
*/

#define SSIZET_MAX ((ssize_t) (~((uintmax_t) 0) >> ((sizeof(uintmax_t) * 8) - (sizeof(ssize_t) * 8 - 1))))

FileNode::FileNode(const shared_ptr<EncFS_Context> &ctx,
                   const FSConfigPtr &cfg,
                   Path plaintextName_,
                   Path cipherName_)
  : fsConfig(cfg)
  , _pname(plaintextName_)
  , _cname(cipherName_)
  , _ctx(ctx)
{}

FileNode::~FileNode()
{
  this->_ctx->eraseNode( _pname.c_str() );
  // FileNode mutex should be locked before the destructor is called
  _pname.zero();
  _cname.zero();
  cipher_io = nullptr;
  io = nullptr;
}

const Path &FileNode::cipherName() const
{
  return _cname;
}

const Path &FileNode::plaintextName() const
{
  return _pname;
}

static bool setIV(const shared_ptr<FileIO> &io,
                  const shared_ptr<CipherFileIO> &cipher_io,
                  uint64_t iv)
{
  bool do_set_iv = false;
  try
  {
    FsFileAttrs attrs = io->get_attrs();
    do_set_iv = attrs.type == FsFileType::REGULAR;
  } catch ( const Error &err )
  {
    do_set_iv = true;
  }

  if(do_set_iv)
    return cipher_io->setIV( iv );
  else
    return true;
}

bool FileNode::setName( opt::optional<Path> plaintextName_,
                        opt::optional<Path> cipherName_,
                        uint64_t iv, bool setIVFirst )
{
  auto oldPName = _pname;

  //Lock _lock( mutex );
  VLOG(1) << "calling setIV on " << _cname;
  if(setIVFirst)
  {
    if(fsConfig->config->external_iv() && !setIV(io, cipher_io, iv))
      return false;

    // now change the name..
    if(plaintextName_)
      this->_pname = *plaintextName_;
    if(cipherName_)
      this->_cname = *cipherName_;
  } else
  {
    auto oldCName = _cname;

    if(plaintextName_)
      this->_pname = *plaintextName_;
    if(cipherName_)
      this->_cname = *cipherName_;

    if(fsConfig->config->external_iv() && !setIV(io, cipher_io, iv))
    {
      _pname = oldPName;
      _cname = oldCName;
      return false;
    }
  }

  if (plaintextName_) this->_ctx->renameNode( oldPName.c_str(), _pname.c_str() );

  return true;
}

int FileNode::open(bool requestWrite, bool create)
{
  // This method will re-open the file for writing
  // if it was opened previously without write access

  Lock _lock( mutex );

  // if we've already opened the file in the right
  // access mode
  if(io && (io->isWritable() || !requestWrite)) return 0;

  // NB: it would be great to check that _cname matches the
  //     file name of the currently opened file from the kernel's POV.
  //     unfortunately there doesn't seem to exist a method of
  //     converting a file descriptor to a file name

  auto fs_io = fsConfig->opts->fs_io;
  std::unique_ptr<FileIO> rawfile;
  const int res = withExceptionCatcher( (int) std::errc::io_error,
                                        bindMethod( fs_io, &FsIO::openfile ),
                                        &rawfile,
                                        fs_io->pathFromString( _cname ),
                                        requestWrite, create );
  if(res < 0) return res;

  assert( rawfile );

  if(cipher_io)
  {
    // the file was already opened, just reset the base of cipher_io
    cipher_io->setBase( std::move( rawfile ) );
  } else
  {
    // chain RawFileIO & CipherFileIO
    io = cipher_io = std::make_shared<CipherFileIO>( std::move( rawfile ), fsConfig );

    if(fsConfig->config->block_mac_bytes() || fsConfig->config->block_mac_rand_bytes())
    {
      io = std::make_shared<MACFileIO>( io, fsConfig );
    }
  }

  return 0;
}

int FileNode::getAttr(FsFileAttrs &stbuf) const
{
  Lock _lock( mutex );

  return withExceptionCatcher( (int) std::errc::io_error,
                               bindMethod( io, &FileIO::get_attrs ),
                               &stbuf );
}

fs_off_t FileNode::getSize() const
{
  FsFileAttrs toret;
  int res = getAttr( toret );
  if(res < 0) return res;
  return toret.size;
}

ssize_t FileNode::read( fs_off_t offset, byte *data, size_t size ) const
{
  /* handle invalid input */
  if (size > (size_t) SSIZET_MAX) return -EDOM;

  IORequest req;
  req.offset = offset;
  req.dataLen = size;
  req.data = data;

  Lock _lock( mutex );
  size_t amount_read = 0;
  const int res = withExceptionCatcher( (int) std::errc::io_error,
                                        bindMethod( io, &FileIO::read ),
                                        &amount_read, req );
  if(res < 0) return res;

  assert( amount_read < SSIZET_MAX );
  return (ssize_t) amount_read;
}

bool FileNode::write(fs_off_t offset, byte *data, size_t size)
{
  VLOG(1) << "FileNode::write offset " << offset
    << ", data size " << size;

  IORequest req;
  req.offset = offset;
  req.dataLen = size;
  req.data = data;

  Lock _lock( mutex );

  const int res = withExceptionCatcherNoRet( (int) std::errc::io_error,
                                             bindMethod( io, &FileIO::write ),
                                             req );
  return !res;
}

int FileNode::truncate( fs_off_t size )
{
  /* ensure file is open since this can be called even
     if the file hasn't been opened */
  const bool requestWrite = true;
  const bool createFile = false;
  int ret = open( requestWrite, createFile );
  if (ret) return ret;

  Lock _lock( mutex );

  return withExceptionCatcherNoRet( (int) std::errc::io_error,
                                    bindMethod( io, &FileIO::truncate ),
                                    size );
}

int FileNode::sync(bool datasync)
{
  Lock _lock( mutex );

  return withExceptionCatcherNoRet( (int) std::errc::io_error,
                                    bindMethod( io, &FileIO::sync ),
                                    datasync );
}

// no-op for now (should close a dup'd FD)
void FileNode::flush() {
}

}  // namespace encfs
