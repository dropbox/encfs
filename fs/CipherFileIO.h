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

#ifndef _CipherFileIO_incl_
#define _CipherFileIO_incl_

#include "cipher/CipherKey.h"
#include "fs/BlockFileIO.h"
#include "fs/FileUtils.h"

#include <inttypes.h>

namespace encfs {

class CipherV1;

/*
    Implement the FileIO interface encrypting data in blocks. 
    
    Uses BlockFileIO to handle the block scatter / gather issues.
*/
class CipherFileIO : public BlockFileIO
{
public:
    CipherFileIO( const shared_ptr<FileIO> &base, 
                  const FSConfigPtr &cfg);
    virtual ~CipherFileIO();

    virtual Interface interface() const override;

    virtual void setFileName( const char *fileName ) override;
    virtual const char *getFileName() const override;
    virtual bool setIV( uint64_t iv ) override;

    virtual int open( int flags ) override;

    virtual int getAttr( FsFileAttrs &stbuf ) const override;
    virtual fs_off_t getSize() const override;

    // NOTE: if truncate is used to extend the file, the extended plaintext is
    // not 0.  The extended ciphertext may be 0, resulting in non-zero
    // plaintext.
    virtual int truncate( fs_off_t size ) override;

    virtual bool isWritable() const override;

private:
    virtual ssize_t readOneBlock( const IORequest &req ) const override;
    virtual bool writeOneBlock( const IORequest &req ) override;

    void initHeader();
    bool writeHeader();
    bool blockRead( byte *buf, size_t size, 
	             uint64_t iv64 ) const;
    bool streamRead( byte *buf, size_t size, 
	             uint64_t iv64 ) const;
    bool blockWrite( byte *buf, size_t size, 
	             uint64_t iv64 ) const;
    bool streamWrite( byte *buf, size_t size, 
	             uint64_t iv64 ) const;

    fs_off_t adjustedSize(fs_off_t size) const;

    shared_ptr<FileIO> base;

    FSConfigPtr fsConfig;

    // if haveHeader is true, then we have a transparent file header which
    int headerLen;

    bool perFileIV;
    uint64_t externalIV;
    uint64_t fileIV;
    int lastFlags;

    shared_ptr<CipherV1> cipher;
};

}  // namespace encfs

#endif
