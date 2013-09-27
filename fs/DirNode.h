/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2003, Valient Gough
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

#ifndef _DirNode_incl_
#define _DirNode_incl_

#include <cstdint>

#include <map>
#include <memory>
#include <list>
#include <vector>
#include <string>

#include "base/Mutex.h"
#include "base/optional.h"

#include "cipher/CipherKey.h"

#include "fs/FileNode.h"
#include "fs/NameIO.h"
#include "fs/FSConfig.h"
#include "fs/FsIO.h"

namespace encfs {

class Cipher;
class RenameOp;
struct RenameEl;
class EncFS_Context;

class DirTraverse
{
public:
    DirTraverse();
    DirTraverse(Directory && dir_io,
                uint64_t iv,
	        const std::shared_ptr<NameIO> &naming);
    DirTraverse(DirTraverse && src);
    DirTraverse& operator=(DirTraverse && other);

    DirTraverse(const DirTraverse &src) = delete;
    DirTraverse &operator = (const DirTraverse &src) = delete;

    // returns FALSE to indicate an invalid DirTraverse (such as when
    // an invalid directory is requested for traversal)
    bool valid() const;

    // return next plaintext filename
    // If fileType is not 0, then it is used to return the filetype
    // (or 0 if unknown)
    std::string nextPlaintextName(FsFileType *fileType=0);

    // Return cipher name of next undecodable filename..
    // The opposite of nextPlaintextName(), as that skips undecodable names..
    std::string nextInvalid();
private:
    opt::optional<Directory> dir_io;
    // initialization vector to use.  Not very general purpose, but makes it
    // more efficient to support filename IV chaining..
    uint64_t iv;
    std::shared_ptr<NameIO> naming;
};
inline bool DirTraverse::valid() const { return (bool) dir_io; }

class DirNode
{
public:
    // sourceDir points to where raw files are stored
    DirNode(EncFS_Context *ctx,
            const std::string &sourceDir,
            const FSConfigPtr &config );
    ~DirNode();

    // return the path to the root directory
    std::string rootDirectory();

    // find files
    std::shared_ptr<FileNode> lookupNode( const char *plaintextName, 
	                      const char *requestor );

    /*
	Combined lookupNode + node->open() call.  If the open fails, then the
	node is not retained.  If the open succeeds, then the node is returned.
    */
    std::shared_ptr<FileNode> openNode( const char *plaintextName,
                                   const char *requestor,
                                   bool requestWrite, bool createNode,
                                   int *openResult );

    std::string cipherPath( const char *plaintextPath );
    std::string cipherPathWithoutRoot( const char *plaintextPath );
    std::string plainPath( const char *cipherPath );
    std::string plaintextParent(const std::string &path);

    // relative cipherPath is the same as cipherPath except that it doesn't
    // prepent the mount point.  That it, it doesn't return a fully qualified
    // name, just a relative path within the encrypted filesystem.
    std::string relativeCipherPath( const char *plaintextPath );

    /*
	Returns true if file names are dependent on the parent directory name.
	If a directory name is changed, then all the filenames must also be
	changed.
    */
    bool hasDirectoryNameDependency() const;

    // unlink the specified file
    int unlink( const char *plaintextName );

    // traverse directory
    DirTraverse openDir( const char *plainDirName );

    // uid and gid are used as the directory owner, only if not zero
    int mkdir( const char *plaintextPath );

    int rename( const char *fromPlaintext, const char *toPlaintext );

    int link( const char *from, const char *to );

    // returns idle time of filesystem in seconds
    int idleSeconds();

    std::shared_ptr<FsIO> get_fs() { return fs_io; }

protected:

    /*
	notify that a file is being renamed. 
	This renames the internal node, if any.  If the file is not open, then
	this call has no effect.
	Returns the FileNode if it was found.
    */
    std::shared_ptr<FileNode> renameNode( const char *from, const char *to );
    std::shared_ptr<FileNode> renameNode( const char *from, const char *to, 
	                             bool forwardMode );

    /*
	when directory IV chaining is enabled, a directory can't be renamed
	without renaming all its contents as well.  recursiveRename should be
	called after renaming the directory, passing in the plaintext from and
	to paths.
    */
    std::shared_ptr<RenameOp> newRenameOp( const char *from, const char *to );

private:

    friend class RenameOp;
    friend class DirTraverse;

    bool genRenameList( std::list<RenameEl> &list, const char *fromP,
                        const char *toP );
    
    std::shared_ptr<FileNode> findOrCreate( const char *plainName);

    Path appendToRoot(const std::string &path);

    Mutex mutex;

    EncFS_Context *ctx;

    // passed in as configuration
    FSConfigPtr fsConfig;
    Path rootDir;

    std::shared_ptr<NameIO> naming;
    std::shared_ptr<FsIO> fs_io;
};

}  // namespace encfs

#endif
