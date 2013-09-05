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

#include "fs/encfs.h"

#include <cstdio>
#include <cstddef>
#include <cstdlib>

#include <cstring>

#include "base/Error.h"
#include "base/Mutex.h"
#include "fs/Context.h"
#include "fs/DirNode.h"
#include "fs/FileUtils.h"
#include "fs/fsconfig.pb.h"

#include <glog/logging.h>

#include <iostream>

using std::list;
using std::string;

/* TODO: change this to std::nullptr_t on systems that support it */
typedef decltype(nullptr) my_nullptr_t;

namespace encfs {

static bool endswith(const std::string & haystack,
                     const std::string & needle) {
  if (needle.length() > haystack.length()) {
    return false;
  }

  return !haystack.compare(haystack.length() - needle.length(),
                           needle.length(), needle);
}

/* this little class allows us to get C++ RAII with C based data structures */
template <class U, class V, void (*func)(U, V)>
class CFreer {
private:
  const U & to_free;
  const V & user_data;
public:
  CFreer(const U & to_free, const V & user_data)
    : to_free( to_free )
    , user_data( user_data )
  {}
  CFreer(const CFreer &) = delete;
  ~CFreer()
  {
    func( to_free, user_data );
  }
};

static void c_closedir(fs_dir_handle_t handle, shared_ptr<FsIO> fs_io)
{
  if(!handle)
  {
    return;
  }
  const FsError ret = fs_io->closedir( handle );
  if(isError( ret ))
  {
    /* this is really bad */
    abort();
  }
}

static void c_free_string(char *str, my_nullptr_t ptr)
{
  assert( !ptr );
  free( str );
}

class CStringFreer : public CFreer<char *, my_nullptr_t, c_free_string>
{
public:
  CStringFreer(char *const & to_free)
    : CFreer( to_free, nullptr )
  {}
};

DirTraverse::DirTraverse(const shared_ptr<FsIO> _fs_io, fs_dir_handle_t _dir,
                         uint64_t _iv, const shared_ptr<NameIO> &_naming)
    : dir( _dir )
    , iv( _iv )
    , naming( _naming )
    , fs_io( _fs_io )
{
}

DirTraverse::DirTraverse(const DirTraverse &src)
    : dir( src.dir )
    , iv( src.iv )
    , naming( src.naming )
    , fs_io( src.fs_io )
{
}

DirTraverse &DirTraverse::operator = (const DirTraverse &src)
{
  dir = src.dir;
  iv = src.iv;
  naming = src.naming;
  fs_io = src.fs_io;

  return *this;
}

DirTraverse::~DirTraverse()
{
  if(dir)
  {
    const FsError ret = fs_io->closedir( dir );
    if(isError( ret )) {
      /* closing directories can't fail */
      abort();
    }
    dir = (fs_dir_handle_t) 0;
  }

  iv = 0;
  naming.reset();
  fs_io.reset();
}

std::string DirTraverse::nextPlaintextName(FsFileType *fileType, fs_posix_ino_t *inode)
{
  char *name;
  while(!isError( fs_io->readdir( dir, &name, fileType, inode ) ) && name)
  {
    CStringFreer free_name(name);
    try
    {
      uint64_t localIv = iv;
      return naming->decodePath( name, &localIv );
    } catch ( Error &ex )
    {
      // .. .problem decoding, ignore it and continue on to next name..
      VLOG(1) << "error decoding filename " << name 
              << " : " << ex.what();
    }
  }

  return string();
}

std::string DirTraverse::nextInvalid()
{
  char *name;
  FsFileType fileType;
  fs_posix_ino_t inode;
  while( !isError( fs_io->readdir( dir, &name, &fileType, &inode ) ) && name )
  {
    CStringFreer free_name( name );
    try
    {
      uint64_t localIv = iv;
      naming->decodePath( name, &localIv );
      continue;
    } catch( Error &ex )
    {
      return string( name );
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

      fs_time_t old_mtime;
      bool preserve_mtime = !isError( dn->fs_io->get_mtime( last->oldCName.c_str(), &old_mtime ) );

      // internal node rename..
      dn->renameNode( last->oldPName.c_str(), last->newPName.c_str() );

      // rename on disk..
      const FsError res_rename =
        dn->fs_io->rename( last->oldCName.c_str(),
                           last->newCName.c_str() );
      if(isError( res_rename ))
      {
        LOG(WARNING) << "Error renaming " << last->oldCName << ": " <<
          fsErrorString(res_rename);
        dn->renameNode( last->newPName.c_str(), 
            last->oldPName.c_str(), false );
        return false;
      }

      if(preserve_mtime)
      {
        dn->fs_io->set_mtime( last->newCName.c_str(), old_mtime );
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

    dn->fs_io->rename( it->newCName.c_str(), it->oldCName.c_str() );
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
{
  Lock _lock( mutex );

  ctx = _ctx;
  rootDir = sourceDir;
  fsConfig = _config;

  naming = fsConfig->nameCoding;

  fs_io = fsConfig->opts->fs_io;

  // make sure rootDir ends in a path separator
  // so that we can form a path by appending the rest..
  if(!endswith(rootDir, fs_io->path_sep())) {
    rootDir.append(fs_io->path_sep());
  }
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
  // chop off trailing path separator from root dir.
  return rootDir.substr( 0, rootDir.length() - strlen( fs_io->path_sep() ) );
}

string DirNode::cipherPath(const char *plaintextPath)
{
  return rootDir + naming->encodePath( plaintextPath );
}

string DirNode::cipherPathWithoutRoot(const char *plaintextPath)
{
  return naming->encodePath( plaintextPath );
}

string DirNode::plainPath(const char *cipherPath_)
{
  /* this method only works on posix file systems */
  if (strcmp(fs_io->path_sep(), "/")) {
    throw std::runtime_error("Non Posix File system");
  }

  try
  {
    /* this method only works on posix file systems */
    assert(!strcmp(fs_io->path_sep(), "/"));
    if(!strncmp( cipherPath_, rootDir.c_str(),
                 rootDir.length() ))
    {
      return naming->decodePath( cipherPath_ + rootDir.length() );
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
  /* this method only works on posix file systems */
  if (strcmp(fs_io->path_sep(), "/")) {
    throw std::runtime_error("Non Posix File system");
  }

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
  string cyName = rootDir + naming->encodePath( plaintextPath );
  //rDebug("openDir on %s", cyName.c_str() );

  fs_dir_handle_t dp;
  const FsError res_opendir = fs_io->opendir( cyName.c_str(), &dp );
  if(isError( res_opendir ))
  {
    VLOG(1) << "opendir error " << fsErrorString(res_opendir);
    return DirTraverse( fs_io, 0, 0, shared_ptr<NameIO>() );
  } else
  {
    uint64_t iv = 0;
    // if we're using chained IV mode, then compute the IV at this
    // directory level..
    try
    {
      if( naming->getChainedNameIV() )
        naming->encodePath( plaintextPath, &iv );
    } catch( Error &err )
    {
      LOG(ERROR) << "encode err: " << err.what();
    }
    return DirTraverse( fs_io, dp, iv, naming );
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
  string sourcePath = rootDir + fromCPart;

  // ok..... we wish it was so simple.. should almost never happen
  if(fromIV == toIV)
    return true;

  // generate the real destination path, where we expect to find the files..
  VLOG(1) << "opendir " << sourcePath;
  fs_dir_handle_t dir = 0;
  const FsError res_opendir = fs_io->opendir( sourcePath.c_str(), &dir );
  CFreer<fs_dir_handle_t, shared_ptr<FsIO>, c_closedir > free_dir( dir, fs_io );

  if(isError( res_opendir ))
    return false;

  char *name;
  FsFileType file_type;
  fs_posix_ino_t inode;
  while(!isError( fs_io->readdir( dir, &name, &file_type, &inode ) ) && name)
  {
    CStringFreer free_name( name );
    // decode the name using the oldIV
    uint64_t localIV = fromIV;
    string plainName;

    try
    {
      plainName = naming->decodePath( name, &localIV );
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
      string oldFull = sourcePath + fs_io->path_sep() + name;
      string newFull = sourcePath + fs_io->path_sep() + newName;

      RenameEl ren;
      ren.oldCName = oldFull;
      ren.newCName = newFull;
      ren.oldPName = string(fromP) + fs_io->path_sep() + plainName;
      ren.newPName = string(toP) + fs_io->path_sep() + plainName;

      ren.isDirectory = file_type == FsFileType::UNKNOWN
        ? isDirectory( fs_io, oldFull.c_str() )
        : file_type == FsFileType::DIRECTORY;

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
          fromCPart.append(fs_io->path_sep()).append(name) << ":" << err.what();

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


FsError DirNode::mkdir(const char *plaintextPath, fs_posix_mode_t mode,
                       fs_posix_uid_t uid, fs_posix_gid_t gid)
{
  string cyName = rootDir + naming->encodePath( plaintextPath );
  rAssert( !cyName.empty() );

  VLOG(1) << "mkdir on " << cyName;

  const FsError res = fs_io->mkdir( cyName.c_str(), mode, uid, gid );

  if(isError( res ))
  {
    LOG(WARNING) << "mkdir error on " << cyName
      << " mode " << mode << ": " << fsErrorString(res);
  }

  return res;
}

FsError
DirNode::rename(const char *fromPlaintext, const char *toPlaintext)
{
  Lock _lock( mutex );

  string fromCName = rootDir + naming->encodePath( fromPlaintext );
  string toCName = rootDir + naming->encodePath( toPlaintext );
  rAssert( !fromCName.empty() );
  rAssert( !toCName.empty() );

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
      return FsError::ACCESS;
    }
    VLOG(1) << "recursive rename end";
  }

  FsError res = FsError::NONE;
  try
  {
    fs_time_t old_mtime;
    bool preserve_mtime = !isError( fs_io->get_mtime(fromCName.c_str(), &old_mtime) );

    renameNode( fromPlaintext, toPlaintext );
    res = fs_io->rename( fromCName.c_str(), toCName.c_str() );

    if(isError( res ))
    {
      // undo
      renameNode( toPlaintext, fromPlaintext, false );

      if(renameOp)
        renameOp->undo();
    } else if(preserve_mtime)
    {
      fs_io->set_mtime( toCName.c_str(), old_mtime );
    }
  } catch( Error &err )
  {
    // exception from renameNode, just show the error and continue..
    LOG(ERROR) << "rename err: " << err.what();
    res = FsError::IO;
  }

  if(isError( res ))
  {
    VLOG(1) << "rename failed: " << fsErrorString( res );
  }

  return res;
}

FsError DirNode::link(const char *from, const char *to)
{
  Lock _lock( mutex );

  string fromCName = rootDir + naming->encodePath( from );
  string toCName = rootDir + naming->encodePath( to );

  rAssert( !fromCName.empty() );
  rAssert( !toCName.empty() );

  VLOG(1) << "link " << fromCName << " -> " << toCName;

  FsError res = FsError::ACCESS;
  if(fsConfig->config->external_iv())
  {
    VLOG(1) << "hard links not supported with external IV chaining!";
  } else
  {
    res = fs_io->link( fromCName.c_str(), toCName.c_str() );
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
    string cname = rootDir + naming->encodePath( to, &newIV );

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
          (rootDir + cipherName).c_str()) );

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

FsError DirNode::unlink( const char *plaintextName )
{
  string cyName = naming->encodePath( plaintextName );
  VLOG(1) << "unlink " << cyName;

  Lock _lock( mutex );

  FsError res = FsError::NONE;
  if(ctx && ctx->lookupNode( plaintextName ))
  {
    // If FUSE is running with "hard_remove" option where it doesn't
    // hide open files for us, then we can't allow an unlink of an open
    // file..
    LOG(WARNING) << "Refusing to unlink open file: "
      << cyName << ", hard_remove option is probably in effect";
    res = FsError::BUSY;
  } else
  {
    string fullName = rootDir + cyName;
    res = fs_io->unlink( fullName.c_str() );
    if(isError( res ))
    {
      VLOG(1) << "unlink error: " << fsErrorString(res);
    }
  }

  return res;
}

}  // namespace encfs

