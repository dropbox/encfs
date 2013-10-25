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

#ifndef _NameIO_incl_
#define _NameIO_incl_

#include <cstdint>

#include <list>
#include <memory>
#include <string>

#include "base/Interface.h"

namespace encfs {

class CipherV1;

typedef std::list<std::string> NameIOPath;

class NameIO {
 public:
  typedef std::shared_ptr<NameIO>(*Constructor)(
      const Interface &iface, const std::shared_ptr<CipherV1> &cipher);

  struct Algorithm {
    std::string name;
    std::string description;
    Interface iface;
    bool needsStreamMode;
  };

  typedef std::list<Algorithm> AlgorithmList;
  static AlgorithmList GetAlgorithmList(bool includeHidden = false);

  static std::shared_ptr<NameIO> New(const Interface &iface,
                                     const std::shared_ptr<CipherV1> &cipher);
  static std::shared_ptr<NameIO> New(const std::string &name,
                                     const std::shared_ptr<CipherV1> &cipher);

  static bool Register(const char *name, const char *description,
                       const Interface &iface, Constructor constructor,
                       bool needsStreamMode, bool hidden = false);

  NameIO();
  virtual ~NameIO();

  virtual Interface interface() const = 0;

  void setChainedNameIV(bool enable);
  bool getChainedNameIV() const;
  void setReverseEncryption(bool enable);
  bool getReverseEncryption() const;

  NameIOPath encodePath(const NameIOPath &plaintextPath) const;
  NameIOPath decodePath(const NameIOPath &encodedPath) const;

  NameIOPath encodePath(const NameIOPath &plaintextPath, uint64_t *iv) const;
  NameIOPath decodePath(const NameIOPath &encodedPath, uint64_t *iv) const;

  virtual int maxEncodedNameLen(int plaintextNameLen) const = 0;
  virtual int maxDecodedNameLen(int encodedNameLen) const = 0;

  std::string encodeName(const std::string &plaintextName) const;
  std::string decodeName(const std::string &encodedName) const;

 protected:
  // Encode & decode methods implemented by derived classes.
  virtual std::string encodeName(const std::string &name,
                                 uint64_t *iv) const = 0;
  virtual std::string decodeName(const std::string &name,
                                 uint64_t *iv) const = 0;

 private:
  NameIOPath recodePath(const NameIOPath &path,
                        int (NameIO::*codingLen)(int) const,
                        std::string (NameIO::*codingFunc)(const std::string &,
                                                          uint64_t *) const,
                        uint64_t *iv) const;

  NameIOPath _encodePath(const NameIOPath &plaintextPath, uint64_t *iv) const;
  NameIOPath _decodePath(const NameIOPath &encodedPath, uint64_t *iv) const;

  bool chainedNameIV;
  bool reverseEncryption;
};

}  // namespace encfs

#endif
