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

#ifndef _RawFileIO_incl_
#define _RawFileIO_incl_

#include "fs/FileIO.h"

#include <string>

namespace encfs {

class RawFileIO : public FileIO
{
public:
    RawFileIO();
    RawFileIO( const std::string &fileName );
    virtual ~RawFileIO();

    virtual Interface interface() const override;

    virtual void setFileName( const char *fileName ) override;
    virtual const char *getFileName() const override;

    virtual int open( int flags ) override;
    
    virtual int getAttr( FsFileAttrs &stbuf ) const override;
    virtual fs_off_t getSize() const override;

    virtual ssize_t read( const IORequest & req ) const override;
    virtual bool write( const IORequest &req ) override;

    virtual int truncate( fs_off_t size ) override;

    virtual bool isWritable() const override;
protected:

    std::string name;

    mutable bool knownSize;
    mutable off_t fileSize;

    int fd;
    int oldfd;
    bool canWrite;
};

}  // namespace encfs

#endif

