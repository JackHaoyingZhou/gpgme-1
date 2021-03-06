/* gpgme.c - GnuPG Made Easy.
   Copyright (C) 2014 g10 Code GmbH

   This file is part of GPGME.

   GPGME is free software; you can redistribute it and/or modify it
   under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of
   the License, or (at your option) any later version.

   GPGME is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef MEM_H
#define MEM_H

#include "gpgme.h"

void *_gpgme_malloc (size_t);
void *_gpgme_calloc (size_t, size_t);
void *_gpgme_realloc (void *, size_t);
void _gpgme_free (void *);
char *_gpgme_strdup (const char *);
void _gpgme_set_global_malloc_hooks (gpgme_malloc_hooks_t);

#endif
