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

#include <glog/logging.h>

#include "fs/encfs.h"

#include "base/Error.h"
#include "base/Mutex.h"
#include "base/optional.h"
#include "base/shared_ptr.h"
#include "fs/Context.h"
#include "fs/DirNode.h"
#include "fs/FileUtils.h"
#include "fs/fsconfig.pb.h"

using std::list;
using std::string;

/* TODO: change this to std::nullptr_t on systems that support it */
typedef decltype(nullptr) my_nullptr_t;

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

std::string DirTraverse::nextPlaintextName(FsFileType *fileType, fs_posix_ino_t *inode)
{
  optional<FsDirEnt> dirent;
  while(!(dirent = dir_io->readdir()))
  {
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
  optional<FsDirEnt> dirent;
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
        old_mtime = dn->fs_io->get_mtime( oldCNamePath );
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
        dn->fs_io->set_mtime( newCNamePath, old_mtime );
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

DirNode::DirNode(EncFS_Context *_ctx,
                 const string &sourceDir,
                 const FSConfigPtr &_config)
  : mutex()
  , ctx( _ctx )
  , rootDir( fsConfig->opts->fs_io->pathFromString( sourceDir ) )
  , fsConfig( _config )
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

string DirNode::relativeCipherPath(const char *plaintextPath)
{
  try
  {
    if(plaintextPath[0] != '/')
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

  try
  {
    auto dir_io = fs_io->opendir( cyName );
    uint64_t iv = 0;
    // if we're using chained IV mode, then compute the IV at this
    // directory level..
    if( naming->getChainedNameIV() )
      naming->encodePath( plaintextPath, &iv );
    return DirTraverse( std::move( dir_io ), iv, naming );
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

  optional<Directory> dir_io;
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
    optional<FsDirEnt> dir_ent;
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

      ren.isDirectory = dir_ent->type == nullopt
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
    return shared_ptr<RenameOp>( new RenameOp(this, renameList) );
}


int DirNode::mkdir(const char *plaintextPath, fs_posix_mode_t mode,
                       fs_posix_uid_t uid, fs_posix_gid_t gid)
{
  auto cyName = appendToRoot( naming->encodePath( plaintextPath ) );

  VLOG(1) << "mkdir on " << cyName;

  try
  {
    try
    {
      fs_io->mkdir( cyName, mode, uid, gid );
      return 0;
    } catch ( const std::system_error & err )
    {
      if(err.code().category() == std::generic_category()) return -err.code().value();
      throw;
    }
  } catch ( const Error & err )
  {
    LOG(WARNING) << "mkdir error on " << cyName
                 << " mode " << mode << ": " << err.what();
    return -(int) std::errc::io_error;
  }
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
      old_mtime = fs_io->get_mtime( fromCName );
    } catch (const Error &err)
    {
      LOG(WARNING) << "get_mtime error: " << err.what();
      preserve_mtime = false;
    }

    renameNode( fromPlaintext, toPlaintext );
    try
    {
      fs_io->rename( fromCName, toCName );
      if(preserve_mtime)
      {
        try
        {
          fs_io->set_mtime( toCName, old_mtime );
        } catch (const Error &err)
        {
          LOG(WARNING) << "set_mtime error: " << err.what();
        }
      }
    } catch (const Error &err)
    {
      // undo
      res = -(int) std::errc::io_error;
      renameNode( toPlaintext, fromPlaintext, false );

      if(renameOp)
        renameOp->undo();
    }
  } catch( Error &err )
  {
    // exception from renameNode, just show the error and continue..
    LOG(ERROR) << "rename err: " << err.what();
    res = -(int) std::errc::io_error;
  }

  return res;
}

int DirNode::link(const char *from, const char *to)
{
  Lock _lock( mutex );

  auto fromCName = appendToRoot( naming->encodePath( from ) );
  auto toCName = appendToRoot( naming->encodePath( to ) );

  VLOG(1) << "link " << fromCName << " -> " << toCName;

  int res = -(int) std::errc::operation_not_permitted;
  if(fsConfig->config->external_iv())
  {
    VLOG(1) << "hard links not supported with external IV chaining!";
  } else
  {
    try
    {
      try
      {
        fs_io->link( fromCName, toCName );
        res = 0;
      } catch ( const std::system_error &err )
      {
        if (err.code().category() == std::generic_category()) res = -err.code().value();
        else throw;
      }
    } catch (const Error &err )
    {
      res = -(int) std::errc::io_error;
    }
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

    if(node->setName( to, cname.c_str(), newIV, forwardMode ))
    {
      if(ctx)
        ctx->renameNode( from, to );
    } else
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
  if(ctx)
    node = ctx->lookupNode( plainName );

  if(!node)
  {
    uint64_t iv = 0;
    string cipherName = naming->encodePath( plainName, &iv );
    node.reset( new FileNode( this, fsConfig,
          plainName, 
          appendToRoot( cipherName ).c_str()) );

    if(fsConfig->config->external_iv())
      node->setName(0, 0, iv);

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

/*
    Similar to lookupNode, except that we also call open() and only return a
    node on sucess..  This is done in one step to avoid any race conditions
    with the stored state of the file.
*/
shared_ptr<FileNode>
DirNode::openNode( const char *plainName, const char * requestor, int flags,
                   int *result )
{
  (void)requestor;
  rAssert( result != NULL );
  Lock _lock( mutex );

  shared_ptr<FileNode> node = findOrCreate( plainName );

  if(node && (*result = node->open( flags )) >= 0)
    return node;
  else
    return shared_ptr<FileNode>();
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
    try
    {
      try
      {
        fs_io->unlink( fullName );
      } catch ( const std::system_error &err )
      {
        if(err.code().category() == std::generic_category()) res = -err.code().value();
        throw;
      }
    } catch ( const Error &err )
    {
      res = -(int) std::errc::io_error;
    }
  }

  return res;
}

}  // namespace encfs

