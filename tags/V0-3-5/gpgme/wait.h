/* wait.h - Definitions for the wait queue interface.
 *	Copyright (C) 2000 Werner Koch (dd9jn)
 *      Copyright (C) 2001, 2002 g10 Code GmbH
 *
 * This file is part of GPGME.
 *
 * GPGME is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GPGME is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef WAIT_H
#define WAIT_H

#include "gpgme.h"

void _gpgme_remove_proc_from_wait_queue (int pid);

GpgmeError _gpgme_register_pipe_handler (void *opaque,
					 int (*handler) (void*, int, int),
					 void *handler_value,
					 int pid, int fd, int inbound);

#endif	/* WAIT_H */
