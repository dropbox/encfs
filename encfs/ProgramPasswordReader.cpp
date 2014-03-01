/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 * Author:   Rian Hunter <rian@alum.mit.edu>
 *
 *****************************************************************************
 * Copyright (c) 2004-2012, Valient Gough
 * Copyright (c) 2013, Dropbox, Inc.
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

// defines needed for RedHat 7.3...
#define _BSD_SOURCE  // pick up setenv on RH7.3

#include "encfs/ProgramPasswordReader.h"

#include "base/i18n.h"
#include "base/logging.h"

#include <string>

#include <fcntl.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

using std::string;

namespace encfs {

// environment variable names for values encfs stores in the environment when
// calling an external password program.
static const char ENCFS_ENV_ROOTDIR[] = "encfs_root";
static const char ENCFS_ENV_STDOUT[] = "encfs_stdout";
static const char ENCFS_ENV_STDERR[] = "encfs_stderr";

// Doesn't use SecureMem, since we don't know how much will be read.
// Besides, password is being produced by another program.
static std::string _readPassword(int FD) {
  SecureMem *buf = new SecureMem(1024);
  string result;

  while (1) {
    ssize_t rdSize = recv(FD, buf->data(), buf->size(), 0);

    if (rdSize > 0) {
      result.append((char *)buf->data(), rdSize);
    } else
      break;
  }

  // chop off trailing "\n" if present..
  // This is done so that we can use standard programs like ssh-askpass
  // without modification, as it returns trailing newline..
  if (!result.empty() && result[result.length() - 1] == '\n')
    result.resize(result.length() - 1);

  delete buf;
  return result;
}

SecureMem *ProgramPasswordReader::readPassword(size_t /*maxLen*/,
                                               bool /*newPass*/) {
  // have a child process run the command and get the result back to us.
  int fds[2], pid;
  int res;

  res = socketpair(PF_UNIX, SOCK_STREAM, 0, fds);
  if (res == -1) {
    perror(_("Internal error: socketpair() failed"));
    return NULL;
  }
  LOG(INFO) << "getUserKey: fds = " << fds[0] << ", " << fds[1];

  pid = fork();
  if (pid == -1) {
    perror(_("Internal error: fork() failed"));
    close(fds[0]);
    close(fds[1]);
    return NULL;
  }

  if (pid == 0) {
    const char *argv[4];
    argv[0] = "/bin/sh";
    argv[1] = "-c";
    argv[2] = passProg.c_str();
    argv[3] = 0;

    // child process.. run the command and send output to fds[0]
    close(fds[1]);  // we don't use the other half..

    // make a copy of stdout and stderr descriptors, and set an environment
    // variable telling where to find them, in case a child wants it..
    int stdOutCopy = dup(STDOUT_FILENO);
    int stdErrCopy = dup(STDERR_FILENO);
    // replace STDOUT with our socket, which we'll used to receive the
    // password..
    dup2(fds[0], STDOUT_FILENO);

    // ensure that STDOUT_FILENO and stdout/stderr are not closed on exec..
    fcntl(STDOUT_FILENO, F_SETFD, 0);  // don't close on exec..
    fcntl(stdOutCopy, F_SETFD, 0);
    fcntl(stdErrCopy, F_SETFD, 0);

    char tmpBuf[8];

    setenv(ENCFS_ENV_ROOTDIR, rootDir.c_str(), 1);

    snprintf(tmpBuf, sizeof(tmpBuf) - 1, "%i", stdOutCopy);
    setenv(ENCFS_ENV_STDOUT, tmpBuf, 1);

    snprintf(tmpBuf, sizeof(tmpBuf) - 1, "%i", stdErrCopy);
    setenv(ENCFS_ENV_STDERR, tmpBuf, 1);

    execvp(argv[0], (char * const *)argv);  // returns only on error..

    perror(_("Internal error: failed to exec program"));
    exit(1);
  }

  close(fds[0]);
  string password = _readPassword(fds[1]);
  close(fds[1]);

  waitpid(pid, NULL, 0);

  SecureMem *result = new SecureMem(password.length() + 1);
  if (result) strncpy((char *)result->data(), password.c_str(), result->size());
  password.assign(password.length(), '\0');

  return result;
}
}
