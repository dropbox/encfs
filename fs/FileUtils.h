/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2004-2012, Valient Gough
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

#ifndef _FileUtils_incl_
#define _FileUtils_incl_

#include <string>

#include "base/Interface.h"
#include "cipher/CipherKey.h"
#include "fs/FSConfig.h"
#include "fs/FsIO.h"
#include "fs/PasswordReader.h"

namespace encfs {

// true if path is a directory
bool isDirectory(const std::shared_ptr<FsIO> &fs_io, const char *fileName);
bool isDirectory(const std::shared_ptr<FsIO> &fs_io, const Path &path);

// true if the path points to an existing node (of any type)
bool fileExists(const std::shared_ptr<FsIO> &fs_io, const char *fileName);

// pointer to just after the last '/'
std::string lastPathElement(const std::shared_ptr<FsIO> &fs_io, std::string);

std::string parentDirectory(const std::shared_ptr<FsIO> &fs_io,
                            const std::string &path);

// ask the user for permission to create the directory.  If they say ok, then
// do it and return true.
bool userAllowMkdir(const std::shared_ptr<FsIO> &fs_io, const char *dirPath,
                    fs_posix_mode_t mode);
bool userAllowMkdir(const std::shared_ptr<FsIO> &fs_io, int promptno,
                    const char *dirPath, fs_posix_mode_t mode);

class CipherV1;
class DirNode;

struct EncFS_Root {
  std::shared_ptr<CipherV1> cipher;
  CipherKey volumeKey;
  std::shared_ptr<DirNode> root;

  EncFS_Root();
  ~EncFS_Root();
};

typedef std::shared_ptr<EncFS_Root> RootPtr;

enum class ConfigMode {
  Prompt,
  Standard,
  Paranoia
};

struct EncFS_Opts {
  std::string rootDir;
  bool createIfNotFound;  // create filesystem if not found
  bool delayMount;        // delay initial mount

  bool checkKey;     // check crypto key decoding
  bool forceDecode;  // force decode on MAC block failures

  bool annotate;

  bool reverseEncryption;  // Reverse encryption

  ConfigMode configMode;

  std::shared_ptr<FsIO> fs_io;
  std::shared_ptr<PasswordReader> passwordReader;

  EncFS_Opts()
      : createIfNotFound(true),
        delayMount(false),
        checkKey(true),
        forceDecode(false),
        annotate(false),
        reverseEncryption(false),
        configMode(ConfigMode::Prompt) {}
};

void write_config(std::shared_ptr<encfs::FsIO> fs_io,
                  const Path &encrypted_folder_path, const EncfsConfig &cfg);

/*
    Save the configuration.  Saves back as the same configuration type as was
    read from.
 */
bool saveConfig(const std::shared_ptr<FsIO> &fs_io, const std::string &rootdir,
                const EncfsConfig &config);

class EncFS_Context;

bool verify_password(const EncfsConfig &cfg, const SecureMem &password);

RootPtr initFS(const std::shared_ptr<EncFS_Context> &ctx,
               const std::shared_ptr<EncFS_Opts> &opts,
               opt::optional<EncfsConfig> oCfg = opt::nullopt,
               bool throw_exception_on_bad_password = false);

EncfsConfig create_config_interactively(
    const std::shared_ptr<PasswordReader> &);
EncfsConfig create_paranoid_config(const SecureMem &secure_password,
                                   bool use_case_insensitive_encoding = false);

class ConfigurationFileDoesNotExist : public std::runtime_error {
 public:
  ConfigurationFileDoesNotExist()
      : std::runtime_error("Configuration file does not exist") {}
};
class ConfigurationFileIsCorrupted : public std::runtime_error {
 public:
  ConfigurationFileIsCorrupted()
      : std::runtime_error("Configuration file is corrupted") {}
};
class BadPassword : public std::runtime_error {
 public:
  BadPassword() : std::runtime_error("Password is incorrect") {}
};

EncfsConfig read_config(std::shared_ptr<encfs::FsIO> fs_io,
                        const Path &encrypted_folder_path);

void showFSInfo(const EncfsConfig &config);

/*
    Read existing config file.  Looks for any supported configuration version.
 */
ConfigType readConfig(const std::shared_ptr<FsIO> &fs_io,
                      const std::string &rootDir, EncfsConfig &config,
                      bool throw_exception = false);

bool readV4Config(const std::shared_ptr<FsIO> &fs_io, const char *configFile,
                  EncfsConfig &config, struct ConfigInfo *);

bool readV5Config(const std::shared_ptr<FsIO> &fs_io, const char *configFile,
                  EncfsConfig &config, struct ConfigInfo *);

bool readV6Config(const std::shared_ptr<FsIO> &fs_io, const char *configFile,
                  EncfsConfig &config, struct ConfigInfo *);

bool readProtoConfig(const std::shared_ptr<FsIO> &fs_io, const char *configFile,
                     EncfsConfig &config, struct ConfigInfo *);

}  // namespace encfs

#endif
