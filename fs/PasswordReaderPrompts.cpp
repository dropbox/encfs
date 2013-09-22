/*****************************************************************************
 * Author:   Rian Hunter <rian@alum.mit.edu>
 *
 *****************************************************************************
 * Copyright (c) 2013, Rian Hunter
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

#include "fs/PasswordReaderPrompts.h"

namespace encfs {

SecureMem *PasswordReaderPrompt::readPassword()
{
  SecureMem *buf = new SecureMem(MaxPassBuf);
  SecureMem *buf2 = new SecureMem(MaxPassBuf);

  do
  {
    // xgroup(common)
    char *res1 = readpassphrase(_("New Encfs Password: "), 
        (char *)buf->data(), buf->size()-1, RPP_ECHO_OFF);
    // xgroup(common)
    char *res2 = readpassphrase(_("Verify Encfs Password: "), 
        (char *)buf2->data(), buf2->size()-1, RPP_ECHO_OFF);

    if(res1 && res2
       && !strncmp((char*)buf->data(), (char*)buf2->data(), MaxPassBuf))
    {
      break; 
    } else
    {
      // xgroup(common) -- probably not common, but group with the others
      cerr << _("Passwords did not match, please try again\n");
    }
  } while(1);

  delete buf2;
  return buf;
}

}
