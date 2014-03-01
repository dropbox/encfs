/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 * Author:   Rian Hunter <rian@alum.mit.edu>
 *
 *****************************************************************************
 * Copyright (c) 2003-2007, Valient Gough
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

#ifndef _base_xattr_incl_
#define _base_xattr_incl_

#include "base/config.h"

#ifdef HAVE_ATTR_XATTR_H
#include <attr/xattr.h>
#define HAVE_XATTR
#elif defined(HAVE_SYS_XATTR_H)
#include <sys/xattr.h>
#define HAVE_XATTR
#endif

#endif
