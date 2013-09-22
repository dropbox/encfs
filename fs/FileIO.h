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

#ifndef _FileIO_incl_
#define _FileIO_incl_

#include <inttypes.h>

#include <system_error>

#include "base/Interface.h"
#include "base/types.h"
#include "fs/encfs.h"
#include "fs/fstypes.h"

namespace encfs {

const std::error_category &errno_category() noexcept;

struct IORequest
{
    fs_off_t offset;

    // amount of bytes to read/write.
    byte *data;
    size_t dataLen;

    IORequest(fs_off_t offset_, byte *data_, size_t len_)
    : offset(offset_)
    , data(data_)
    , dataLen(len_)
    {}

    IORequest() : IORequest(0, nullptr, 0)
    {}
};

class FileIO
{
public:
    virtual ~FileIO() =0;

    virtual Interface interface() const =0;

    // default implementation returns 1, meaning this is not block oriented.
    virtual int blockSize() const; 

    virtual void setFileName(const char *fileName) =0;
    virtual const char *getFileName() const =0;

    // Not sure about this -- it is specific to CipherFileIO, but the
    // alternative methods of exposing this interface aren't much nicer..
    virtual bool setIV( uint64_t iv );

    // open file for specified mode.  There is no corresponding close, so a
    // file is open until the FileIO interface is destroyed.
    virtual int open( int flags ) =0;

    // get filesystem attributes for a file
    virtual int getAttr( FsFileAttrs & ) const =0;
    virtual fs_off_t getSize() const =0;

    virtual ssize_t read( const IORequest &req ) const =0;
    virtual bool write( const IORequest &req ) =0;

    virtual int truncate( fs_off_t size ) =0;

    virtual bool isWritable() const =0;
};

}  // namespace encfs

#endif

