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

#include <cstring>

#include <iostream>

#include "base/i18n.h"

#include "cipher/readpassphrase.h"

#include "encfs/PromptPasswordReader.h"

using std::cerr;

namespace encfs {

SecureMem *PromptPasswordReader::readPassword(size_t maxLen, bool newPass) {
  SecureMem *buf = new SecureMem(maxLen);

  if (newPass) {
    SecureMem *buf2 = new SecureMem(maxLen);

    do {
      // xgroup(common)
      char *res1 =
          readpassphrase(_("New Encfs Password: "), (char *)buf->data(),
                         buf->size() - 1, RPP_ECHO_OFF);
      // xgroup(common)
      char *res2 =
          readpassphrase(_("Verify Encfs Password: "), (char *)buf2->data(),
                         buf2->size() - 1, RPP_ECHO_OFF);

      if (res1 && res2 &&
          !strncmp((char *)buf->data(), (char *)buf2->data(), maxLen)) {
        break;
      } else {
        // xgroup(common) -- probably not common, but group with the others
        cerr << _("Passwords did not match, please try again\n");
      }
    } while (1);

    delete buf2;
  } else {
    // xgroup(common)
    char *res = readpassphrase(_("EncFS Password: "), (char *)buf->data(),
                               buf->size() - 1, RPP_ECHO_OFF);
    if (!res) {
      delete buf;
      buf = NULL;
    }
  }

  return buf;
}
}
