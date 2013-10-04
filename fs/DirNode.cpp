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

#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include <iostream>
#include <memory>

#include <glog/logging.h>

#include "base/Error.h"
#include "base/Mutex.h"
#include "base/optional.h"
#include "fs/CipherFileIO.h"
#include "fs/Context.h"
#include "fs/FileUtils.h"
#include "fs/MACFileIO.h"
#include "fs/fsconfig.pb.h"

#include "fs/DirNode.h"

using std::list;
using std::string;
using std::shared_ptr;

namespace encfs {

DirTraverse::DirTraverse()
{
}

DirTraverse::DirTraverse(Directory && _dir_io,
                         uint64_t _iv, const shared_ptr<NameIO> &_naming)
    : dir_io( std::move(_dir_io ) )
    , iv( _iv )
    , naming( _naming )
{
}

DirTraverse::DirTraverse(DirTraverse && dt)
{
  *this = std::move(dt);
}

DirTraverse& DirTraverse::operator=(DirTraverse && other)
{
  this->dir_io = std::move(other.dir_io);
  this->iv = other.iv;
  this->naming = other.naming;
  return *this;
}

std::string DirTraverse::nextPlaintextName(FsFileType *fileType, fs_file_id_t *inode)
{
  opt::optional<FsDirEnt> dirent;
  while((dirent = dir_io->readdir()))
  {
    if (fileType && dirent->type) *fileType = *dirent->type;
    if (inode) *inode = dirent->file_id;
    try
    {
      uint64_t localIv = iv;
      return naming->decodePath( dirent->name.c_str(), &localIv );
    } catch ( Error &ex )
    {
      // .. .problem decoding, ignore it and continue on to next name..
      VLOG(1) << "error decoding filename " << dirent->name
              << " : " << ex.what();
    }
  }

  return string();
}

std::string DirTraverse::nextInvalid()
{
  opt::optional<FsDirEnt> dirent;
  while( !(dirent = dir_io->readdir()) )
  {
    try
    {
      uint64_t localIv = iv;
      naming->decodePath( dirent->name.c_str(), &localIv );
      continue;
    } catch( Error &ex )
    {
      return dirent->name;
    }
  }

  return string();
}

struct RenameEl
{
  // ciphertext names
  string oldCName;
  string newCName; // intermediate name (not final cname)

  // plaintext names
  string oldPName;
  string newPName;

  bool isDirectory;
};

class RenameOp
{
private:
  DirNode *dn;
  shared_ptr< list<RenameEl> > renameList;
  list<RenameEl>::const_iterator last;

public:
  RenameOp( DirNode *_dn, const shared_ptr< list<RenameEl> > &_renameList )
    : dn(_dn), renameList(_renameList)
  {
    last = renameList->begin();
  }

  RenameOp(const RenameOp &src)
    : dn(src.dn)
      , renameList(src.renameList)
      , last(src.last)
  {
  }

  ~RenameOp();

  operator bool () const
  {
    return (bool) renameList;
  }

  bool apply();
  void undo();
};

RenameOp::~RenameOp()
{
  if(renameList)
  {
    // got a bunch of decoded filenames sitting in memory..  do a little
    // cleanup before leaving..
    list<RenameEl>::iterator it;
    for(it = renameList->begin(); it != renameList->end(); ++it)
    {
      it->oldPName.assign( it->oldPName.size(), ' ' );
      it->newPName.assign( it->newPName.size(), ' ' );
    }
  }
}

bool RenameOp::apply()
{
  try
  {
    while(last != renameList->end())
    {
      // backing store rename.
      VLOG(2) << "renaming " << last->oldCName << "-> " << last->newCName;
      auto oldCNamePath = dn->fs_io->pathFromString( last->oldCName );
      auto newCNamePath = dn->fs_io->pathFromString( last->newCName );

      fs_time_t old_mtime;
      bool preserve_mtime;
      try
      {
        old_mtime = dn->fs_io->get_attrs( oldCNamePath ).mtime;
        preserve_mtime = true;
      } catch (...)
      {
        preserve_mtime = false;
      }

      // internal node rename..
      dn->renameNode( last->oldPName.c_str(), last->newPName.c_str() );

      // rename on disk..
      try
      {
        dn->fs_io->rename( oldCNamePath, newCNamePath );
      } catch ( const std::system_error & err )
      {
        LOG(WARNING) << "Error renaming " << last->oldCName << ": " <<
          err.code().message();
        dn->renameNode( last->newPName.c_str(), 
            last->oldPName.c_str(), false );
        return false;
      }

      if(preserve_mtime)
      {
        dn->fs_io->set_times( newCNamePath, opt::nullopt, old_mtime );
      }

      ++last;
    }

    return true;
  } catch( Error &err )
  {
    LOG(WARNING) << "caught error in rename application: " << err.what();
    return false;
  }
}

void RenameOp::undo()
{
  VLOG(1) << "in undoRename";

  if(last == renameList->begin())
  {
    VLOG(1) << "nothing to undo";
    return; // nothing to undo
  }

  // list has to be processed backwards, otherwise we may rename
  // directories and directory contents in the wrong order!
  int undoCount = 0;
  int errorCount = 0;
  list<RenameEl>::const_iterator it = last;

  while(it != renameList->begin())
  {
    --it;

    VLOG(1) << "undo: renaming " << it->newCName << " -> " << it->oldCName;

    auto newCNamePath = dn->fs_io->pathFromString( it->newCName );
    auto oldCNamePath = dn->fs_io->pathFromString( it->oldCName );

    try
    {
      dn->fs_io->rename( newCNamePath, oldCNamePath );
    } catch (const std::system_error &err )
    {
      // ignore system errors
      LOG(WARNING) << "error in rename und: " << err.code().message();
    }
    try
    {
      dn->renameNode( it->newPName.c_str(), 
                      it->oldPName.c_str(), false );
    } catch( Error &err )
    {
      if(++errorCount == 1)
        LOG(WARNING) << "error in rename und: " << err.what();
      // continue on anyway...
    }
    ++undoCount;
  };

  LOG(WARNING) << "Undo rename count: " << undoCount;
}

DirNode::DirNode(std::shared_ptr<EncFS_Context> _ctx,
                 const string &sourceDir,
                 const FSConfigPtr &_config)
  : mutex()
  , ctx( _ctx )
  , fsConfig( _config )
  , rootDir( fsConfig->opts->fs_io->pathFromString( sourceDir ) )
  , naming( fsConfig->nameCoding )
  , fs_io( fsConfig->opts->fs_io )
{
}

DirNode::~DirNode()
{
}

bool DirNode::hasDirectoryNameDependency() const
{
  return naming ? naming->getChainedNameIV() : false;
}

string DirNode::rootDirectory()
{
  // don't update last access here, otherwise 'du' would cause lastAccess to
  // be reset.
  return rootDir;
}

Path DirNode::appendToRoot(const string &path)
{
  return fs_io->pathFromString( (const std::string &) rootDir + '/' + path);
}

string DirNode::cipherPath(const char *plaintextPath)
{
  return appendToRoot( naming->encodePath( plaintextPath ) );
}

string DirNode::cipherPathWithoutRoot(const char *plaintextPath)
{
  return naming->encodePath( plaintextPath );
}

string DirNode::plaintextParent(const string &path)
{
  return parentDirectory( fs_io, path );
}

static bool startswith(const std::string &a, const std::string &b)
{
  return !strncmp( a.c_str(), b.c_str(), b.length() );
}

string DirNode::plainPath(const char *cipherPath_)
{
  try
  {
    if(startswith(cipherPath_, (const std::string &) rootDir + '/'))
    {
      return naming->decodePath( cipherPath_ + ((const std::string &) rootDir).length() + 1);
    } else
    {
      if(cipherPath_[0] == '+')
      {
        // decode as fully qualified path
        return string("/") + naming->decodeName( cipherPath_+1,
            strlen(cipherPath_+1) );
      } else
      {
        return naming->decodePath( cipherPath_ );
      }
    }

  } catch( Error &err )
  {
    LOG(ERROR) << "decode err: " << err.what();
    return string();
  }
}

PosixSymlinkData DirNode::decryptLinkPath(PosixSymlinkData in)
{
  auto buf = plainPath( in.c_str() );
  return PosixSymlinkData( std::move( buf ) );
}

string DirNode::relativeCipherPath(const char *plaintextPath)
{
  try
  {
    if(plaintextPath[0] == '/')
    {
      // mark with '+' to indicate special decoding..
      return string("+") + naming->encodeName(plaintextPath+1,
          strlen(plaintextPath+1));
    } else
    {
      return naming->encodePath( plaintextPath );
    }
  } catch( Error &err )
  {
    LOG(ERROR) << "encode err: " << err.what();
    return string();
  }
}

DirTraverse DirNode::openDir(const char *plaintextPath)
{
  auto cyName = appendToRoot( naming->encodePath( plaintextPath ) );
  //rDebug("openDir on %s", cyName.c_str() );

  opt::optional<Directory> dir_io;
  const int res = withExceptionCatcher( (int) std::errc::io_error,
                                        bindMethod( fs_io, &FsIO::opendir ),
                                        &dir_io, cyName );
  if(res < 0) return DirTraverse();

  assert( dir_io );

  try
  {
    uint64_t iv = 0;
    // if we're using chained IV mode, then compute the IV at this
    // directory level..
    if( naming->getChainedNameIV() )
      naming->encodePath( plaintextPath, &iv );
    return DirTraverse( std::move( *dir_io ), iv, naming );
  } catch( Error &err )
  {
    LOG(ERROR) << "encode err: " << err.what();
    return DirTraverse();
  }
}

bool DirNode::genRenameList(list<RenameEl> &renameList, 
    const char *fromP, const char *toP)
{
  uint64_t fromIV = 0, toIV = 0;

  // compute the IV for both paths
  string fromCPart = naming->encodePath( fromP, &fromIV );
  string toCPart = naming->encodePath( toP, &toIV );

  // where the files live before the rename..
  auto sourcePath = appendToRoot( fromCPart );

  // ok..... we wish it was so simple.. should almost never happen
  if(fromIV == toIV)
    return true;

  // generate the real destination path, where we expect to find the files..
  VLOG(1) << "opendir " << sourcePath;

  opt::optional<Directory> dir_io;
  try
  {
    dir_io = fs_io->opendir( sourcePath );
  } catch (const Error &err)
  {
    LOG(WARNING) << "opendir(" << sourcePath << ") failed: " << err.what();
    return false;
  }

  while(true)
  {
    opt::optional<FsDirEnt> dir_ent;
    try
    {
      dir_ent = dir_io->readdir();
    } catch (const Error &err)
    {
      LOG(WARNING) << "readdir(" << sourcePath << ") failed: " << err.what();
      return false;
    }

    if (!dir_ent) {
      break;
    }

    // decode the name using the oldIV
    uint64_t localIV = fromIV;
    string plainName;

    try
    {
      plainName = naming->decodePath( dir_ent->name.c_str(), &localIV );
    } catch( Error &ex )
    {
      // if filename can't be decoded, then ignore it..
      continue;
    }

    // any error in the following will trigger a rename failure.
    try
    {
      // re-encode using the new IV..
      localIV = toIV;
      string newName = naming->encodePath( plainName.c_str(), &localIV );

      // store rename information..
      auto oldFull = sourcePath.join( dir_ent->name );
      auto newFull = sourcePath.join( newName );

      RenameEl ren;
      ren.oldCName = oldFull;
      ren.newCName = newFull;
      ren.oldPName = fs_io->pathFromString( (string) fromP ).join( plainName );
      ren.newPName = fs_io->pathFromString( (string) toP ).join( plainName );

      ren.isDirectory = dir_ent->type == opt::nullopt
        ? isDirectory( fs_io, oldFull.c_str() )
        : *dir_ent->type == FsFileType::DIRECTORY;

      if(ren.isDirectory)
      {
        // recurse..  We want to add subdirectory elements before the
        // parent, as that is the logical rename order..
        if(!genRenameList( renameList,
            ren.oldPName.c_str(),
            ren.newPName.c_str() ))
        {
          return false;
        }
      }

      VLOG(1) << "adding file " << oldFull << " to rename list";
      renameList.push_back( ren );

    } catch( Error &err )
    {
      // We can't convert this name, because we don't have a valid IV for
      // it (or perhaps a valid key).. It will be inaccessible..
      LOG(WARNING) << "Aborting rename: error on file " << 
          sourcePath.join( dir_ent->name ) << ":" << err.what();

      // abort.. Err on the side of safety and disallow rename, rather
      // then loosing files..
      return false;
    }
  }

  return true;
}


/*
    A bit of a pain.. If a directory is renamed in a filesystem with
    directory initialization vector chaining, then we have to recursively
    rename every descendent of this directory, as all initialization vectors
    will have changed..

    Returns a list of renamed items on success, a null list on failure.
*/
shared_ptr<RenameOp>
DirNode::newRenameOp(const char *fromP, const char *toP)
{
  // Do the rename in two stages to avoid chasing our tail
  // Undo everything if we encounter an error!
  shared_ptr< list<RenameEl> > renameList(new list<RenameEl>);
  if(!genRenameList( *renameList.get(), fromP, toP ))
  {
    LOG(WARNING) << "Error during generation of recursive rename list";
    return shared_ptr<RenameOp>();
  } else
    return std::make_shared<RenameOp>( this, renameList );
}

int DirNode::posix_mkdir(const char *plaintextPath, fs_posix_mode_t mode)
{
  auto cyName = appendToRoot( naming->encodePath( plaintextPath ) );

  VLOG(1) << "mkdir on " << cyName;

  return withExceptionCatcher( (int) std::errc::io_error,
                               bindMethod( fs_io, &FsIO::posix_mkdir ),
                               std::move( cyName ), mode );
}

int
DirNode::rename(const char *fromPlaintext, const char *toPlaintext)
{
  Lock _lock( mutex );

  auto fromCName = appendToRoot( naming->encodePath( fromPlaintext ) );
  auto toCName = appendToRoot( naming->encodePath( toPlaintext ) );

  VLOG(1) << "rename " << fromCName << " -> " << toCName;

  shared_ptr<FileNode> toNode = findOrCreate( toPlaintext );

  shared_ptr<RenameOp> renameOp;
  if(hasDirectoryNameDependency() && isDirectory( fs_io, fromCName.c_str() ))
  {
    VLOG(1) << "recursive rename begin";
    renameOp = newRenameOp( fromPlaintext, toPlaintext );

    if(!renameOp || !renameOp->apply())
    {
      if(renameOp)
        renameOp->undo();

      LOG(WARNING) << "rename aborted";
      return -(int) std::errc::permission_denied;
    }
    VLOG(1) << "recursive rename end";
  }

  int res = 0;
  try
  {
    fs_time_t old_mtime;
    bool preserve_mtime = true;
    try
    {
      old_mtime = fs_io->get_attrs( fromCName ).mtime;
    } catch ( const std::exception &err )
    {
      LOG(WARNING) << "get_mtime error: " << err.what();
      preserve_mtime = false;
    }

    renameNode( fromPlaintext, toPlaintext );
    res = withExceptionCatcher((int) std::errc::io_error,
                               bindMethod( fs_io, &FsIO::rename ),
                               fromCName, toCName );
    if(res < 0)
    {
      // undo
      renameNode( toPlaintext, fromPlaintext, false );

      if(renameOp)
        renameOp->undo();
    } else if(preserve_mtime)
    {
      withExceptionCatcher(1, bindMethod( fs_io, &FsIO::set_times ),
                           toCName, opt::nullopt, old_mtime );
    }
  } catch( Error &err )
  {
    // exception from renameNode, just show the error and continue..
    LOG(ERROR) << "rename err: " << err.what();
    res = -(int) std::errc::io_error;
  }

  return res;
}

int DirNode::posix_link(const char *from, const char *to)
{
  Lock _lock( mutex );

  auto fromCName = appendToRoot( naming->encodePath( from ) );
  auto toCName = appendToRoot( naming->encodePath( to ) );

  VLOG(1) << "link " << fromCName << " -> " << toCName;

  int res = 0;
  if(fsConfig->config->external_iv())
  {
    VLOG(1) << "hard links not supported with external IV chaining!";
    res = -(int) std::errc::operation_not_permitted;
  } else
  {
    res = withExceptionCatcher( (int) std::errc::io_error,
                                bindMethod( fs_io, &FsIO::posix_link ),
                                fromCName, toCName );
  }

  return res;
}

/*
    The node is keyed by filename, so a rename means the internal node names
    must be changed.
*/
shared_ptr<FileNode> DirNode::renameNode(const char *from, const char *to)
{
  return renameNode( from, to, true );
}

shared_ptr<FileNode> DirNode::renameNode(const char *from, const char *to, 
    bool forwardMode)
{
  shared_ptr<FileNode> node = findOrCreate( from );

  if(node)
  {
    uint64_t newIV = 0;
    auto cname = appendToRoot( naming->encodePath( to, &newIV ) );

    VLOG(1) << "renaming internal node " << node->cipherName() 
      << " -> " << cname.c_str();

    if(!node->setName( to, cname.c_str(), newIV, forwardMode ))
    {
      // rename error! - put it back 
      LOG(ERROR) << "renameNode failed";
      throw Error("Internal node name change failed!");
    }
  }

  return node;
}

shared_ptr<FileNode> DirNode::findOrCreate(const char *plainName)
{
  shared_ptr<FileNode> node;
  if(ctx) node = ctx->lookupNode( plainName );

  if(!node)
  {
    uint64_t iv = 0;
    string cipherName = naming->encodePath( plainName, &iv );
    node = std::make_shared<FileNode>( ctx, fsConfig,
                                       plainName,
                                       appendToRoot( cipherName ).c_str() );

    // add weak reference to node
    ctx->trackNode( plainName, node );

    if(fsConfig->config->external_iv()) node->setName(0, 0, iv);

    VLOG(1) << "created FileNode for " << node->cipherName();
  }

  return node;
}

shared_ptr<FileNode>
DirNode::lookupNode( const char *plainName, const char * requestor )
{
  (void)requestor;
  Lock _lock( mutex );

  shared_ptr<FileNode> node = findOrCreate( plainName );

  return node;
}

shared_ptr<FileNode>
DirNode::_openNode( const char *plainName, const char */*requestor*/,
                    bool requestWrite, bool createFile, int *result )
{
  shared_ptr<FileNode> node = findOrCreate( plainName );
  if(node) *result = node->open( requestWrite, createFile );
  else *result = -(int) std::errc::io_error;

  if(*result < 0) node = nullptr;

  return node;
}

/*
    Similar to lookupNode, except that we also call open() and only return a
    node on sucess..  This is done in one step to avoid any race conditions
    with the stored state of the file.
*/
shared_ptr<FileNode>
DirNode::openNode( const char *plainName, const char * requestor,
                   bool requestWrite, bool createFile, int *result )
{
  (void)requestor;
  rAssert( result != NULL );
  Lock _lock( mutex );

  return _openNode( plainName, requestor, requestWrite, createFile, result );
}

int DirNode::unlink( const char *plaintextName )
{
  string cyName = naming->encodePath( plaintextName );
  VLOG(1) << "unlink " << cyName;

  Lock _lock( mutex );

  int res = 0;
  if(ctx && ctx->lookupNode( plaintextName ))
  {
    // If FUSE is running with "hard_remove" option where it doesn't
    // hide open files for us, then we can't allow an unlink of an open
    // file..
    LOG(WARNING) << "Refusing to unlink open file: "
      << cyName << ", hard_remove option is probably in effect";
    res = -(int) std::errc::device_or_resource_busy;
  } else
  {
    auto fullName = appendToRoot( cyName );
    res = withExceptionCatcher( (int) std::errc::io_error,
                                bindMethod( fs_io, &FsIO::unlink ),
                                fullName );
  }

  return res;
}

int DirNode::mkdir( const char *plaintextName )
{
  Lock _lock( mutex );
  auto fullName = appendToRoot( naming->encodePath( plaintextName ) );
  return withExceptionCatcher( (int) std::errc::io_error,
                               bindMethod( fs_io, &FsIO::mkdir ), fullName );
}

int DirNode::get_attrs(FsFileAttrs *attrs, const char *plaintextName)
{
  Lock _lock( mutex );

  auto cyName = appendToRoot( naming->encodePath( plaintextName ) );
  VLOG(1) << "unlink " << cyName;

  int res = withExceptionCatcher( (int) std::errc::io_error,
                                  bindMethod( fs_io, &FsIO::get_attrs ),
                                  attrs, cyName );
  if(res < 0) return res;

  // TODO: make sure this wrap code is similar to how FileIO is created
  // in FileNode
  *attrs = CipherFileIO::wrapAttrs( fsConfig, std::move( *attrs ) );

  if(fsConfig->config->block_mac_bytes() || fsConfig->config->block_mac_rand_bytes())
  {
    *attrs = MACFileIO::wrapAttrs( fsConfig, std::move( *attrs ) );
  }

  if(attrs->type == FsFileType::POSIX_LINK)
  {
    opt::optional<PosixSymlinkData> buf;
    res = withExceptionCatcher( (int) std::errc::io_error,
                                bindMethod( this, &DirNode::_posix_readlink ),
                                &buf, cyName );
    if(res < 0) return res;
    assert( buf );
    attrs->size = buf->size();
  }

  return res;
}

PosixSymlinkData DirNode::_posix_readlink(const std::string &cyPath)
{
  auto link_buf = fs_io->posix_readlink( fs_io->pathFromString( cyPath ) );
  // decrypt link buf
  return decryptLinkPath( std::move( link_buf ) );
}

int DirNode::posix_readlink(PosixSymlinkData *buf, const char *plaintextName)
{
  Lock _lock( mutex );

  auto cyName = appendToRoot( naming->encodePath( plaintextName ) );
  return withExceptionCatcher( (int) std::errc::io_error,
                               bindMethod( this, &DirNode::_posix_readlink ),
                               buf, cyName );
}

int DirNode::posix_symlink(const char *path, const char *data)
{
  // allow fully qualified names in symbolic links.
  string toCName = cipherPath( path );
  string fromCName = relativeCipherPath( data );

  VLOG(1) << "symlink " << fromCName << " -> " << toCName;

  return withExceptionCatcher( (int) std::errc::io_error,
                               bindMethod( fs_io, &FsIO::posix_symlink ),
                               fs_io->pathFromString( std::move( toCName ) ),
                               PosixSymlinkData( std::move( fromCName ) ) );
}

Path DirNode::pathFromString(const std::string &string)
{
  return fs_io->pathFromString( string );
}

int DirNode::posix_create( shared_ptr<FileNode> *fnode,
                           const char *plainName, fs_posix_mode_t mode )
{
  rAssert( fnode );
  Lock _lock( mutex );

  // first call posix_create to clear node and whatever else
  auto cyName = appendToRoot( naming->encodePath( plainName ) );
  int ret = withExceptionCatcher( (int) std::errc::io_error,
                                  bindMethod( fs_io, &FsIO::posix_create ),
                                  cyName, mode );
  if(ret < 0) return ret;

  // then open node as usual, this is fine since the fs is locked during this
  const bool requestWrite = true;
  const bool createFile = true;
  *fnode = _openNode( plainName, "posix_create", requestWrite, createFile, &ret );
  if (!*fnode) return ret;
  return 0;
}

int DirNode::posix_mknod(const char *plaintextName,
                         fs_posix_mode_t mode, fs_posix_dev_t dev)
{
  Lock _lock( mutex );

  auto fullName = appendToRoot( naming->encodePath( plaintextName ) );
  return withExceptionCatcher( (int) std::errc::io_error,
                               bindMethod( fs_io, &FsIO::posix_mknod ),
                               fullName, mode, dev );
}

int DirNode::posix_setfsgid( fs_posix_gid_t *oldgid, fs_posix_gid_t newgid)
{
  Lock _lock( mutex );

  return withExceptionCatcher( (int) std::errc::io_error,
                               bindMethod( fs_io, &FsIO::posix_setfsgid ),
                               oldgid, newgid );

}

int DirNode::posix_setfsuid( fs_posix_uid_t *olduid, fs_posix_uid_t newuid)
{
  Lock _lock( mutex );
  return withExceptionCatcher( (int) std::errc::io_error,
                               bindMethod( fs_io, &FsIO::posix_setfsuid ),
                               olduid, newuid );
}

int DirNode::rmdir( const char *plaintextName )
{
  Lock _lock( mutex );
  auto fullName = appendToRoot( naming->encodePath( plaintextName ) );
  return withExceptionCatcher( (int) std::errc::io_error,
                               bindMethod( fs_io, &FsIO::rmdir ),
                               fullName );
}

int DirNode::set_times( const char *plaintextName,
                        opt::optional<fs_time_t> atime,
                        opt::optional<fs_time_t> mtime )
{
  Lock _lock( mutex );
  auto fullName = appendToRoot( naming->encodePath( plaintextName ) );
  return withExceptionCatcher( (int) std::errc::io_error,
                               bindMethod( fs_io, &FsIO::set_times ),
                               fullName, std::move( atime ), std::move( mtime ) );

}

int DirNode::posix_chmod( const char *plaintextName, fs_posix_mode_t mode )
{
  Lock _lock( mutex );

  auto fullName = appendToRoot( naming->encodePath( plaintextName ) );
  return withExceptionCatcher( (int) std::errc::io_error,
                               bindMethod( fs_io, &FsIO::posix_chmod ),
                               fullName, mode );

}

int DirNode::posix_chown( const char *plaintextName, fs_posix_uid_t uid, fs_posix_gid_t gid )
{
  Lock _lock( mutex );

  auto fullName = appendToRoot( naming->encodePath( plaintextName ) );
  return withExceptionCatcher( (int) std::errc::io_error,
                               bindMethod( fs_io, &FsIO::posix_chown ),
                               fullName, uid, gid );

}

int DirNode::posix_setxattr( const char *plaintextName, bool follow, std::string name,
                             size_t offset, std::vector<byte> buf, PosixSetxattrFlags flags )
{
  Lock _lock( mutex );

  auto fullName = appendToRoot( naming->encodePath( plaintextName ) );
  return withExceptionCatcher( (int) std::errc::io_error,
                               bindMethod( fs_io, &FsIO::posix_setxattr ),
                               std::move( fullName ), follow, std::move( name ),
                               offset, std::move( buf ), std::move( flags ) );
}

int DirNode::posix_getxattr( opt::optional<std::vector<byte>> *ret,
                             const char *plaintextName, bool follow, std::string name,
                             size_t offset, size_t amt )
{
  Lock _lock( mutex );

  auto fullName = appendToRoot( naming->encodePath( plaintextName ) );
  return withExceptionCatcher( (int) std::errc::io_error,
                               bindMethod( fs_io, &FsIO::posix_getxattr ),
                               ret,
                               std::move( fullName ), follow, std::move( name ),
                               offset, amt );
}

int DirNode::posix_listxattr( opt::optional<PosixXattrList> *ret,
                              const char *plaintextName, bool follow )
{
  Lock _lock( mutex );

  auto fullName = appendToRoot( naming->encodePath( plaintextName ) );
  return withExceptionCatcher( (int) std::errc::io_error,
                               bindMethod( fs_io, &FsIO::posix_listxattr ),
                               ret,
                               std::move( fullName ), follow );
}

int DirNode::posix_removexattr( const char *plaintextName, bool follow, std::string name )
{
  Lock _lock( mutex );

  auto fullName = appendToRoot( naming->encodePath( plaintextName ) );
  return withExceptionCatcher( (int) std::errc::io_error,
                               bindMethod( fs_io, &FsIO::posix_removexattr ),
                               std::move( fullName ), follow, std::move( name ) );
}


}  // namespace encfs

