/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2003-2004, Valient Gough
 *
 * This library is free software; you can distribute it and/or modify it under
 * the terms of the GNU General Public License (GPL), as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GPL in the file COPYING for more
 * details.
 *
 */

#include "base/config.h"

#include "base/libc_support.h"
#include "base/logging.h"
#include "base/autosprintf.h"
#include "base/ConfigReader.h"
#include "base/Error.h"
#include "base/Interface.h"
#include "base/i18n.h"

#include "cipher/CipherV1.h"

#include "fs/EncfsFsIO.h"
// TODO: get rid of the following import
#include "fs/FileUtils.h"
#include "fs/FsIO.h"
#include "fs/PasswordReader.h"

#include "encfs/EncFS_Args.h"
#include "encfs/EncfsPasswordReader.h"
#include "encfs/PosixFsIO.h"
#include "encfs/RootPathPrependFs.h"
#include "encfs/encfs.h"

#include <getopt.h>

#include <iostream>
#include <string>
#include <sstream>
#include <thread>

#include <cassert>
#include <cerrno>
#include <clocale>
#include <cstdio>
#include <cstring>

#include <unistd.h>

#include <sys/param.h>
#include <sys/time.h>

// Fuse version >= 26 requires another argument to fuse_unmount, which we
// don't have.  So use the backward compatible call instead..
extern "C" void fuse_unmount_compat22(const char *mountpoint);
#define fuse_unmount fuse_unmount_compat22

using gnu::autosprintf;
using std::cerr;
using std::endl;
using std::ostringstream;
using std::shared_ptr;
using std::string;

using namespace encfs;

namespace encfs {

static int oldStderr = STDERR_FILENO;

}  // namespace encfs

static void usage(const char *name) {
  // xgroup(usage)
  cerr << autosprintf(_("Build: encfs version %s"), VERSION) << "\n\n"
      // xgroup(usage)
       << autosprintf(_("Usage: %s [options] rootDir mountPoint [-- [FUSE "
                        "Mount Options]]"),
                      name) << "\n\n"
      // xgroup(usage)
       << _("Common Options:\n"
            "  -H\t\t\t"
            "show optional FUSE Mount Options\n"
            "  -s\t\t\t"
            "disable multithreaded operation\n"
            "  -f\t\t\t"
            "run in foreground (don't spawn daemon).\n"
            "\t\t\tError messages will be sent to stderr\n"
            "\t\t\tinstead of syslog.\n")

      // xgroup(usage)
       << _("  -v, --verbose\t\t"
            "verbose: output encfs debug messages\n"
            "  -i, --idle=MINUTES\t"
            "Auto unmount after period of inactivity\n"
            "  --anykey\t\t"
            "Do not verify correct key is being used\n"
            "  --forcedecode\t\t"
            "decode data even if an error is detected\n"
            "\t\t\t(for filesystems using MAC block headers)\n")
       << _("  --public\t\t"
            "act as a typical multi-user filesystem\n"
            "\t\t\t(encfs must be run as root)\n") << _("  --reverse\t\t"
                                                        "reverse encryption\n")

      // xgroup(usage)
       << _("  --extpass=program\tUse external program for password prompt\n"
            "\n"
            "Example, to mount at ~/crypt with raw storage in ~/.crypt :\n"
            "    encfs ~/.crypt ~/crypt\n"
            "\n")
      // xgroup(usage)
       << _("For more information, see the man page encfs(1)") << "\n" << endl;
}

static void FuseUsage() {
  // xgroup(usage)
  cerr << _("encfs [options] rootDir mountPoint -- [FUSE Mount Options]\n"
            "valid FUSE Mount Options follow:\n") << endl;

  int argc = 2;
  const char *argv[] = {"...", "-h"};
  fuse_main(argc, const_cast<char **>(argv), (fuse_operations *)NULL, NULL);
}

#define PUSHARG(ARG)                        \
  do {                                      \
    rAssert(out->fuseArgc < MaxFuseArgs);   \
    out->fuseArgv[out->fuseArgc++] = (ARG); \
  } while (0)

static string slashTerminate(const string &src) {
  string result = src;
  if (result[result.length() - 1] != '/') result.append("/");
  return result;
}

static std::string makeAbsolute(std::string a) {
  if (a[0] == '/') return a;
  char buf[PATH_MAX];
  std::string cwd = getcwd(buf, sizeof(buf));
  return cwd + '/' + a;
}

static bool processArgs(int argc, char *argv[],
                        const shared_ptr<EncFS_Args> &out) {
  // set defaults
  out->isDaemon = true;
  out->isThreaded = true;
  out->isVerbose = false;
  out->idleTimeout = 0;
  out->fuseArgc = 0;
  out->opts->checkKey = true;
  out->opts->forceDecode = false;
  out->isPublic = false;
  out->useStdin = false;
  out->opts->annotate = false;
  out->opts->reverseEncryption = false;

  bool useDefaultFlags = true;

  // pass executable name through
  out->fuseArgv[0] = strdup_x(
      lastPathElement(out->opts->fs_io, makeAbsolute(argv[0])).c_str());
  ++out->fuseArgc;

  // leave a space for mount point, as FUSE expects the mount point before
  // any flags
  out->fuseArgv[1] = NULL;
  ++out->fuseArgc;

  // TODO: can flags be internationalized?
  static struct option long_options[] = {
      {"fuse-debug", 0, 0, 'd'},   // Fuse debug mode
      {"forcedecode", 0, 0, 'D'},  // force decode
      // {"foreground", 0, 0, 'f'}, // foreground mode (no daemon)
      {"fuse-help", 0, 0, 'H'},         // fuse_mount usage
      {"idle", 1, 0, 'i'},              // idle timeout
      {"anykey", 0, 0, 'k'},            // skip key checks
      {"no-default-flags", 0, 0, 'N'},  // don't use default fuse flags
      {"ondemand", 0, 0, 'm'},          // mount on-demand
      {"delaymount", 0, 0, 'M'},        // delay initial mount until use
      {"public", 0, 0, 'P'},            // public mode
      {"extpass", 1, 0, 'p'},           // external password program
      // {"single-thread", 0, 0, 's'}, // single-threaded mode
      {"stdinpass", 0, 0, 'S'},  // read password from stdin
      {"annotate", 0, 0, 513},   // Print annotation lines to stderr
      {"verbose", 0, 0, 'v'},    // verbose mode
      {"version", 0, 0, 'V'},    // version
      {"reverse", 0, 0, 'r'},    // reverse encryption
      {"standard", 0, 0, '1'},   // standard configuration
      {"paranoia", 0, 0, '2'},   // standard configuration
      {0, 0, 0, 0}};

  while (1) {
    int option_index = 0;

    // 's' : single-threaded mode
    // 'f' : foreground mode
    // 'v' : verbose mode (same as --verbose)
    // 'd' : fuse debug mode (same as --fusedebug)
    // 'i' : idle-timeout, takes argument
    // 'm' : mount-on-demand
    // 'S' : password from stdin
    // 'o' : arguments meant for fuse
    int res =
        getopt_long(argc, argv, "HsSfvVdmi:o:", long_options, &option_index);

    if (res == -1) break;

    switch (res) {
      case '1':
        out->opts->configMode = ConfigMode::Standard;
        break;
      case '2':
        out->opts->configMode = ConfigMode::Paranoia;
        break;
      case 's':
        out->isThreaded = false;
        break;
      case 'S':
        out->useStdin = true;
        break;
      case 513:
        out->opts->annotate = true;
        break;
      case 'f':
        out->isDaemon = false;
        // this option was added in fuse 2.x
        PUSHARG("-f");
        break;
      case 'v':
        out->isVerbose = true;
        break;
      case 'd':
        PUSHARG("-d");
        break;
      case 'i':
        out->idleTimeout = strtol(optarg, (char **)NULL, 10);
        break;
      case 'k':
        out->opts->checkKey = false;
        break;
      case 'D':
        out->opts->forceDecode = true;
        break;
      case 'r':
        out->opts->reverseEncryption = true;
        break;
      case 'm':
        out->mountOnDemand = true;
        break;
      case 'M':
        out->opts->delayMount = true;
        break;
      case 'N':
        useDefaultFlags = false;
        break;
      case 'o':
        PUSHARG("-o");
        PUSHARG(optarg);
        break;
      case 'p':
        out->passwordProgram.assign(optarg);
        break;
      case 'P':
        if (geteuid() != 0)
          LOG(WARNING) << "option '--public' ignored for non-root user";
        else {
          out->isPublic = true;
          // add 'allow_other' option
          // add 'default_permissions' option (default)
          PUSHARG("-o");
          PUSHARG("allow_other");
        }
        break;
      case 'V':
        // xgroup(usage)
        cerr << autosprintf(_("encfs version %s"), VERSION) << endl;
        exit(EXIT_SUCCESS);
        break;
      case 'H':
        FuseUsage();
        exit(EXIT_SUCCESS);
        break;
      case '?':
        // invalid options..
        break;
      case ':':
        // missing parameter for option..
        break;
      default:
        LOG(WARNING) << "getopt error: " << res;
        break;
    }
  }

  if (!out->isThreaded) PUSHARG("-s");

  if (useDefaultFlags) {
    PUSHARG("-o");
    PUSHARG("use_ino");
    PUSHARG("-o");
    PUSHARG("default_permissions");
  }

  // we should have at least 2 arguments left over - the source directory and
  // the mount point.
  if (optind + 2 <= argc) {
    out->opts->rootDir = slashTerminate(argv[optind++]);
    out->mountPoint = argv[optind++];
  } else {
    // no mount point specified
    LOG(LERROR) << "Missing one or more arguments, aborting.";
    return false;
  }

  // If there are still extra unparsed arguments, pass them onto FUSE..
  if (optind < argc) {
    rAssert(out->fuseArgc < MaxFuseArgs);

    while (optind < argc) {
      rAssert(out->fuseArgc < MaxFuseArgs);
      out->fuseArgv[out->fuseArgc++] = argv[optind];
      ++optind;
    }
  }

  // sanity check
  if (out->isDaemon) {
    try {
      // NB: check if the paths are well formed */
      out->opts->fs_io->pathFromString(out->mountPoint);
      out->opts->fs_io->pathFromString(out->opts->rootDir);
    }
    catch (const Error &err) {
      cerr <<
          // xgroup(usage)
          _("When specifying daemon mode, you must use absolute paths "
            "(beginning with '/')") << endl;
      return false;
    }
  }

  // the raw directory may not be a subdirectory of the mount point.
  {
    string testMountPoint = slashTerminate(out->mountPoint);
    string testRootDir = out->opts->rootDir.substr(0, testMountPoint.length());

    if (testMountPoint == testRootDir) {
      cerr <<
          // xgroup(usage)
          _("The raw directory may not be a subdirectory of the "
            "mount point.") << endl;
      return false;
    }
  }

  if (out->opts->delayMount && !out->mountOnDemand) {
    cerr <<
        // xgroup(usage)
        _("You must use mount-on-demand with delay-mount") << endl;
    return false;
  }

  if (out->mountOnDemand && out->passwordProgram.empty()) {
    cerr <<
        // xgroup(usage)
        _("Must set password program when using mount-on-demand") << endl;
    return false;
  }

  // check that the directories exist, or that we can create them..
  if (!isDirectory(out->opts->fs_io, out->opts->rootDir.c_str()) &&
      !userAllowMkdir(out->opts->fs_io, out->opts->annotate ? 1 : 0,
                      out->opts->rootDir.c_str(), 0700)) {
    LOG(WARNING) << "Unable to locate root directory, aborting.";
    return false;
  }
  if (!isDirectory(out->opts->fs_io, out->mountPoint.c_str()) &&
      !userAllowMkdir(out->opts->fs_io, out->opts->annotate ? 2 : 0,
                      out->mountPoint.c_str(), 0700)) {
    LOG(WARNING) << "Unable to locate mount point, aborting.";
    return false;
  }

  // fill in mount path for fuse
  out->fuseArgv[1] = out->mountPoint.c_str();

  return true;
}

static void idleMonitor(EncFSFuseContext *);

void *encfs_init(fuse_conn_info *conn) {
  auto ctx = get_global_encfs_fuse_context();

  // set fuse connection options
  conn->async_read = true;

  // if an idle timeout is specified, then setup a thread to monitor the
  // filesystem.
  if (ctx->getArgs()->idleTimeout > 0) {
    LOG(INFO) << "starting idle monitoring thread";
    ctx->setRunning(true);

    try {
      ctx->monitorThread = std::make_shared<std::thread>(idleMonitor, ctx);
    }
    catch (...) {
      LOG(LERROR) << "error starting idle monitor thread";
    }
  }

  if (ctx->getArgs()->isDaemon && oldStderr >= 0) {
    LOG(INFO) << "Closing stderr";
    close(oldStderr);
    oldStderr = -1;
  }

  return (void *)ctx;
}

void encfs_destroy(void *_ctx) {
  auto ctx = static_cast<EncFSFuseContext *>(_ctx);
  if (ctx->getArgs()->idleTimeout > 0) {
    ctx->setRunning(false);

    LOG(INFO) << "waking up monitoring thread";
    ctx->wakeupCond.notify_one();
    LOG(INFO) << "joining with idle monitoring thread";
    ctx->monitorThread->join();
    LOG(INFO) << "join done";
  }
}

int main(int argc, char *argv[]) {
  cerr << "\n\n";
  cerr << "====== WARNING ======= WARNING ======== WARNING ========\n";
  cerr << "NOTE: this version of Encfs comes from SVN mainline and is\n"
          "an unreleased 2.x BETA. It is known to have issues!\n";
  cerr << "               USE AT YOUR OWN RISK!\n";
  cerr << "Stable releases are available from the Encfs website, or look\n"
          "for the 1.x branch in SVN for the stable 1.x series.";
  cerr << "\n\n";

#ifdef LOCALEDIR
  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);
#endif

  // anything that comes from the user should be considered tainted until
  // we've processed it and only allowed through what we support.
  shared_ptr<EncFS_Args> encfsArgs(new EncFS_Args);
  for (int i = 0; i < MaxFuseArgs; ++i)
    encfsArgs->fuseArgv[i] = NULL;  // libfuse expects null args..

  // TODO: is this better down earlier in this method
  // or in EncFS_Args
  encfsArgs->opts->fs_io = std::make_shared<PosixFsIO>();

  if (argc == 1 || !processArgs(argc, argv, encfsArgs)) {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  encfsArgs->opts->passwordReader = std::make_shared<EncfsPasswordReader>(
      encfsArgs->useStdin, encfsArgs->passwordProgram,
      encfsArgs->opts->rootDir);

  if (encfsArgs->isVerbose) FLAGS_minloglevel = 0;

  LOG(INFO) << "Root directory: " << encfsArgs->opts->rootDir;
  LOG(INFO) << "Fuse arguments: " << encfsArgs->toString();

  fuse_operations encfs_oper;
  // in case this code is compiled against a newer FUSE library and new
  // members have been added to fuse_operations, make sure they get set to
  // 0..
  memset(&encfs_oper, 0, sizeof(fuse_operations));

  encfs_oper.getattr = encfs_getattr;
  encfs_oper.readlink = encfs_readlink;
  encfs_oper.getdir = encfs_getdir;  // deprecated for readdir
  encfs_oper.mknod = encfs_mknod;
  encfs_oper.mkdir = encfs_mkdir;
  encfs_oper.unlink = encfs_unlink;
  encfs_oper.rmdir = encfs_rmdir;
  encfs_oper.symlink = encfs_symlink;
  encfs_oper.rename = encfs_rename;
  encfs_oper.link = encfs_link;
  encfs_oper.chmod = encfs_chmod;
  encfs_oper.chown = encfs_chown;
  encfs_oper.truncate = encfs_truncate;
  encfs_oper.open = encfs_open;
  encfs_oper.read = encfs_read;
  encfs_oper.write = encfs_write;
  encfs_oper.statfs = encfs_statfs;
  encfs_oper.flush = encfs_flush;
  encfs_oper.release = encfs_release;
  encfs_oper.fsync = encfs_fsync;
  encfs_oper.setxattr = encfs_setxattr;
  encfs_oper.getxattr = encfs_getxattr;
  encfs_oper.listxattr = encfs_listxattr;
  encfs_oper.removexattr = encfs_removexattr;
  // encfs_oper.opendir = encfs_opendir;
  // encfs_oper.readdir = encfs_readdir;
  // encfs_oper.releasedir = encfs_releasedir;
  // encfs_oper.fsyncdir = encfs_fsyncdir;
  encfs_oper.init = encfs_init;
  encfs_oper.destroy = encfs_destroy;
  // encfs_oper.access = encfs_access;
  // encfs_oper.create = encfs_create;
  encfs_oper.ftruncate = encfs_ftruncate;
  encfs_oper.fgetattr = encfs_fgetattr;
  // encfs_oper.lock = encfs_lock;
  encfs_oper.utimens = encfs_utimens;
// encfs_oper.bmap = encfs_bmap;

#if (__FreeBSD__ >= 10)
// encfs_oper.setvolname
// encfs_oper.exchange
// encfs_oper.getxtimes
// encfs_oper.setbkuptime
// encfs_oper.setchgtime
// encfs_oper.setcrtime
// encfs_oper.chflags
// encfs_oper.setattr_x
// encfs_oper.fsetattr_x
#endif

  CipherV1::init(encfsArgs->isThreaded);

  int returnCode = EXIT_FAILURE;
  try {
    auto encryptedFS = std::make_shared<EncfsFsIO>();

    encryptedFS->initFS(encfsArgs->opts, opt::nullopt);

    // wrap encryptedFS around a file system that adds
    // a root path as a prefix to each path
    auto oldRoot = encryptedFS->pathFromString("/");
    auto newRoot = encryptedFS->pathFromString(encfsArgs->opts->rootDir);
    auto wrappedEncryptedFS = std::make_shared<RootPathPrependFs>(
        std::move(encryptedFS), std::move(oldRoot), std::move(newRoot));
    // turn off delayMount, as our prior call to initFS has already
    // respected any delay, and we want future calls to actually mount.
    encfsArgs->opts->delayMount = false;

    // resources will get freed after block is exited
    // TODO: separate fuse args from encrypted fs opts
    // TODO: because the fuse layer can delete and recreate backend file systems
    //       but is still file system agnostic, we should pass a FsIO "Factory"
    //       object to it
    EncFSFuseContext ctx(encfsArgs, encfsArgs->opts,
                         std::move(wrappedEncryptedFS));

    if (encfsArgs->isThreaded == false && encfsArgs->idleTimeout > 0) {
      // xgroup(usage)
      cerr << _("Note: requested single-threaded mode, but an idle\n"
                "timeout was specified.  The filesystem will operate\n"
                "single-threaded, but threads will still be used to\n"
                "implement idle checking.") << endl;
    }

    // reset umask now, since we don't want it to interfere with the
    // pass-thru calls..
    umask(0);

    if (encfsArgs->isDaemon) {
      // switch to logging just warning and error messages via syslog
      FLAGS_minloglevel = 1;
      FLAGS_logtostderr = 0;

      // keep around a pointer just in case we end up needing it to
      // report a fatal condition later (fuse_main exits unexpectedly)...
      oldStderr = dup(STDERR_FILENO);
    }

    {
      time_t startTime, endTime;

      if (encfsArgs->opts->annotate) cerr << "$STATUS$ fuse_main_start" << endl;

      // FIXME: workaround for fuse_main returning an error on normal
      // exit.  Only print information if fuse_main returned
      // immediately..
      time(&startTime);

      // fuse_main returns an error code in newer versions of fuse..
      int res = fuse_main(encfsArgs->fuseArgc,
                          const_cast<char **>(encfsArgs->fuseArgv), &encfs_oper,
                          (void *)&ctx);

      time(&endTime);

      if (encfsArgs->opts->annotate) cerr << "$STATUS$ fuse_main_end" << endl;

      if (res == 0) returnCode = EXIT_SUCCESS;

      if (res != 0 && encfsArgs->isDaemon && (oldStderr >= 0) &&
          (endTime - startTime <= 1)) {
        // the users will not have seen any message from fuse, so say a
        // few words in libfuse's memory..
        FILE *out = fdopen(oldStderr, "a");
        // xgroup(usage)
        fprintf(out, _("fuse failed.  Common problems:\n"
                       " - fuse kernel module not installed (modprobe fuse)\n"
                       " - invalid options -- see usage message\n"));
        fclose(out);
      }
    }
  }
  catch (std::exception &ex) {
    LOG(LERROR) << "Internal error: Caught exception from main loop: "
                << ex.what();
  } /* catch(...)
   {
     LOG(LERROR) << "Internal error: Caught unexpected exception";
     }*/

  CipherV1::shutdown(encfsArgs->isThreaded);

  return returnCode;
}

/*
    Idle monitoring thread.  This is only used when idle monitoring is enabled.
    It will cause the filesystem to be automatically unmounted (causing us to
    commit suicide) if the filesystem stays idle too long.  Idle time is only
    checked if there are no open files, as I don't want to risk problems by
    having the filesystem unmounted from underneath open files!
*/
const int ActivityCheckInterval = 10;

bool unmountFS(EncFSFuseContext *ctx) {
  auto arg = ctx->getArgs();
  LOG(INFO) << "Detaching filesystem " << arg->mountPoint
            << " due to inactivity";

  if (arg->mountOnDemand) {
    ctx->unmountFS();
    return false;
  } else {
    fuse_unmount(arg->mountPoint.c_str());
    return true;
  }
}

static void idleMonitor(EncFSFuseContext *ctx) {
  auto arg = ctx->getArgs();

  const int timeoutCycles = 60 * arg->idleTimeout / ActivityCheckInterval;
  int idleCycles = 0;

  std::unique_lock<std::mutex> lock(ctx->wakeupMutex);

  while (ctx->isRunning()) {
    int usage = ctx->getAndResetUsageCounter();

    if (usage == 0 && ctx->isMounted())
      idleCycles += 1;
    else
      idleCycles = 0;

    if (idleCycles >= timeoutCycles) {
      int openCount = ctx->openFileCount();
      if (openCount == 0 && unmountFS(ctx)) {
        ctx->wakeupCond.wait(lock);
        break;
      }

      LOG(INFO) << "num open files: " << openCount;
    }

    LOG(INFO) << "idle cycle count: " << idleCycles << ", timeout after "
              << timeoutCycles;

    ctx->wakeupCond.wait_for(lock, std::chrono::seconds(ActivityCheckInterval));
  }

  LOG(INFO) << "Idle monitoring thread exiting";
}
