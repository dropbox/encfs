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

#ifndef _MACFileIO_incl_
#define _MACFileIO_incl_

#include "cipher/CipherV1.h"
#include "fs/BlockFileIO.h"

namespace encfs {

class MACFileIO : public BlockFileIO
{
public:
    /*
	If warnOnlyMode is enabled, then a MAC comparison failure will only
	result in a warning message from encfs -- the garbled data will still
	be made available..
    */
    MACFileIO( const shared_ptr<FileIO> &base,
               const FSConfigPtr &cfg );
    MACFileIO();
    virtual ~MACFileIO();

    virtual Interface interface() const override;

    virtual void setFileName( const char *fileName ) override;
    virtual const char *getFileName() const override;
    virtual bool setIV( uint64_t iv ) override;

    virtual int open( int flags ) override;
    virtual int getAttr( FsFileAttrs &stbuf ) const override;
    virtual fs_off_t getSize() const override;

    virtual int truncate( fs_off_t size ) override;

    virtual bool isWritable() const override;

private:
    virtual ssize_t readOneBlock( const IORequest &req ) const override;
    virtual bool writeOneBlock( const IORequest &req );

    shared_ptr<FileIO> base;
    shared_ptr<CipherV1> cipher;
    int macBytes;
    int randBytes;
    bool warnOnly;
};

}  // namespace encfs

#endif

