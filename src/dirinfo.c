/* dirinfo.c - Get directory information
 * Copyright (C) 2009, 2013 g10 Code GmbH
 *
 * This file is part of GPGME.
 *
 * GPGME is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * GPGME is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "gpgme.h"
#include "util.h"
#include "priv-io.h"
#include "debug.h"
#include "sema.h"
#include "sys-util.h"

DEFINE_STATIC_LOCK (dirinfo_lock);

/* Constants used internally to select the data.  */
enum
  {
    WANT_HOMEDIR,
    WANT_AGENT_SOCKET,
    WANT_GPGCONF_NAME,
    WANT_GPG_NAME,
    WANT_GPGSM_NAME,
    WANT_G13_NAME,
    WANT_UISRV_SOCKET,
    WANT_GPG_ONE_MODE
  };

/* Values retrieved via gpgconf and cached here.  */
static struct {
  int  valid;         /* Cached information is valid.  */
  int  disable_gpgconf;
  char *homedir;
  char *agent_socket;
  char *gpgconf_name;
  char *gpg_name;
  char *gpgsm_name;
  char *g13_name;
  char *uisrv_socket;
  int  gpg_one_mode;  /* System is in gpg1 mode.  */
} dirinfo;



/* Helper function to be used only by gpgme_set_global_flag.  */
void
_gpgme_dirinfo_disable_gpgconf (void)
{
  dirinfo.disable_gpgconf = 1;
}


/* Return the length of the directory part including the trailing
 * slash of NAME.  */
static size_t
dirname_len (const char *name)
{
  return _gpgme_get_basename (name) - name;
}


/* Parse the output of "gpgconf --list-dirs".  This function expects
   that DIRINFO_LOCK is held by the caller.  If COMPONENTS is set, the
   output of --list-components is expected. */
static void
parse_output (char *line, int components)
{
  char *value, *p;
  size_t n;

  value = strchr (line, ':');
  if (!value)
    return;
  *value++ = 0;
  if (components)
    {
      /* Skip the second field.  */
      value = strchr (value, ':');
      if (!value)
        return;
      *value++ = 0;
    }
  p = strchr (value, ':');
  if (p)
    *p = 0;
  if (_gpgme_decode_percent_string (value, &value, strlen (value)+1, 0))
    return;
  if (!*value)
    return;

  if (components)
    {
      if (!strcmp (line, "gpg") && !dirinfo.gpg_name)
        dirinfo.gpg_name = strdup (value);
      else if (!strcmp (line, "gpgsm") && !dirinfo.gpgsm_name)
        dirinfo.gpgsm_name = strdup (value);
      else if (!strcmp (line, "g13") && !dirinfo.g13_name)
        dirinfo.g13_name = strdup (value);
    }
  else
    {
      if (!strcmp (line, "homedir") && !dirinfo.homedir)
        dirinfo.homedir = strdup (value);
      else if (!strcmp (line, "agent-socket") && !dirinfo.agent_socket)
        {
          const char name[] = "S.uiserver";
          char *buffer;

          dirinfo.agent_socket = strdup (value);
          if (dirinfo.agent_socket)
            {
              n = dirname_len (dirinfo.agent_socket);
              buffer = malloc (n + strlen (name) + 1);
              if (buffer)
                {
                  strncpy (buffer, dirinfo.agent_socket, n);
                  strcpy (buffer + n, name);
                  dirinfo.uisrv_socket = buffer;
                }
            }
        }
    }
}


/* Read the directory information from gpgconf.  This function expects
   that DIRINFO_LOCK is held by the caller.  PGNAME is the name of the
   gpgconf binary. If COMPONENTS is set, not the directories bit the
   name of the componeNts are read. */
static void
read_gpgconf_dirs (const char *pgmname, int components)
{
  char linebuf[1024] = {0};
  int linelen = 0;
  char * argv[3];
  int rp[2];
  struct spawn_fd_item_s cfd[] = { {-1, 1 /* STDOUT_FILENO */, -1, 0},
				   {-1, -1} };
  int status;
  int nread;
  char *mark = NULL;

  argv[0] = (char *)pgmname;
  argv[1] = components? "--list-components" : "--list-dirs";
  argv[2] = NULL;

  if (_gpgme_io_pipe (rp, 1) < 0)
    return;

  cfd[0].fd = rp[1];

  status = _gpgme_io_spawn (pgmname, argv, IOSPAWN_FLAG_DETACHED,
                            cfd, NULL, NULL, NULL);
  if (status < 0)
    {
      _gpgme_io_close (rp[0]);
      _gpgme_io_close (rp[1]);
      return;
    }

  do
    {
      nread = _gpgme_io_read (rp[0],
                              linebuf + linelen,
                              sizeof linebuf - linelen - 1);
      if (nread > 0)
	{
          char *line;
          const char *lastmark = NULL;
          size_t nused;

	  linelen += nread;
	  linebuf[linelen] = '\0';

	  for (line=linebuf; (mark = strchr (line, '\n')); line = mark+1 )
	    {
              lastmark = mark;
	      if (mark > line && mark[-1] == '\r')
		mark[-1] = '\0';
              else
                mark[0] = '\0';

              parse_output (line, components);
	    }

          nused = lastmark? (lastmark + 1 - linebuf) : 0;
          memmove (linebuf, linebuf + nused, linelen - nused);
          linelen -= nused;
	}
    }
  while (nread > 0 && linelen < sizeof linebuf - 1);

  _gpgme_io_close (rp[0]);
}


static const char *
get_gpgconf_item (int what)
{
  const char *result = NULL;

  LOCK (dirinfo_lock);
  if (!dirinfo.valid)
    {
      char *pgmname;

      pgmname = dirinfo.disable_gpgconf? NULL : _gpgme_get_gpgconf_path ();
      if (pgmname && access (pgmname, F_OK))
        {
          _gpgme_debug (DEBUG_INIT,
                        "gpgme-dinfo: gpgconf='%s' [not installed]\n", pgmname);
          free (pgmname);
          pgmname = NULL; /* Not available.  */
        }
      else
        _gpgme_debug (DEBUG_INIT, "gpgme-dinfo: gpgconf='%s'\n",
                      pgmname? pgmname : "[null]");
      if (!pgmname)
        {
          /* Probably gpgconf is not installed.  Assume we are using
             GnuPG-1.  */
          dirinfo.gpg_one_mode = 1;
          pgmname = _gpgme_get_gpg_path ();
          if (pgmname)
            dirinfo.gpg_name = pgmname;
        }
      else
        {
          dirinfo.gpg_one_mode = 0;
          read_gpgconf_dirs (pgmname, 0);
          read_gpgconf_dirs (pgmname, 1);
          dirinfo.gpgconf_name = pgmname;
        }
      /* Even if the reading of the directories failed (e.g. due to an
         too old version gpgconf or no gpgconf at all), we need to
         mark the entries as valid so that we won't try over and over
         to read them.  Note further that we are not able to change
         the read values later because they are practically statically
         allocated.  */
      dirinfo.valid = 1;
      if (dirinfo.gpg_name)
        _gpgme_debug (DEBUG_INIT, "gpgme-dinfo:     gpg='%s'\n",
                      dirinfo.gpg_name);
      if (dirinfo.g13_name)
        _gpgme_debug (DEBUG_INIT, "gpgme-dinfo:     g13='%s'\n",
                      dirinfo.g13_name);
      if (dirinfo.gpgsm_name)
        _gpgme_debug (DEBUG_INIT, "gpgme-dinfo:   gpgsm='%s'\n",
                      dirinfo.gpgsm_name);
      if (dirinfo.homedir)
        _gpgme_debug (DEBUG_INIT, "gpgme-dinfo: homedir='%s'\n",
                      dirinfo.homedir);
      if (dirinfo.agent_socket)
        _gpgme_debug (DEBUG_INIT, "gpgme-dinfo:   agent='%s'\n",
                      dirinfo.agent_socket);
      if (dirinfo.uisrv_socket)
        _gpgme_debug (DEBUG_INIT, "gpgme-dinfo:   uisrv='%s'\n",
                      dirinfo.uisrv_socket);
    }
  switch (what)
    {
    case WANT_HOMEDIR: result = dirinfo.homedir; break;
    case WANT_AGENT_SOCKET: result = dirinfo.agent_socket; break;
    case WANT_GPGCONF_NAME: result = dirinfo.gpgconf_name; break;
    case WANT_GPG_NAME:   result = dirinfo.gpg_name; break;
    case WANT_GPGSM_NAME: result = dirinfo.gpgsm_name; break;
    case WANT_G13_NAME:   result = dirinfo.g13_name; break;
    case WANT_UISRV_SOCKET:  result = dirinfo.uisrv_socket; break;
    case WANT_GPG_ONE_MODE: result = dirinfo.gpg_one_mode? "1":NULL; break;
    }
  UNLOCK (dirinfo_lock);
  return result;
}


/* Return the default home directory.   Returns NULL if not known.  */
const char *
_gpgme_get_default_homedir (void)
{
  return get_gpgconf_item (WANT_HOMEDIR);
}

/* Return the default gpg-agent socket name.  Returns NULL if not known.  */
const char *
_gpgme_get_default_agent_socket (void)
{
  return get_gpgconf_item (WANT_AGENT_SOCKET);
}

/* Return the default gpg file name.  Returns NULL if not known.  */
const char *
_gpgme_get_default_gpg_name (void)
{
  return get_gpgconf_item (WANT_GPG_NAME);
}

/* Return the default gpgsm file name.  Returns NULL if not known.  */
const char *
_gpgme_get_default_gpgsm_name (void)
{
  return get_gpgconf_item (WANT_GPGSM_NAME);
}

/* Return the default g13 file name.  Returns NULL if not known.  */
const char *
_gpgme_get_default_g13_name (void)
{
  return get_gpgconf_item (WANT_G13_NAME);
}

/* Return the default gpgconf file name.  Returns NULL if not known.  */
const char *
_gpgme_get_default_gpgconf_name (void)
{
  return get_gpgconf_item (WANT_GPGCONF_NAME);
}

/* Return the default UI-server socket name.  Returns NULL if not
   known.  */
const char *
_gpgme_get_default_uisrv_socket (void)
{
  return get_gpgconf_item (WANT_UISRV_SOCKET);
}

/* Return true if we are in GnuPG-1 mode - ie. no gpgconf and agent
   being optional.  */
int
_gpgme_in_gpg_one_mode (void)
{
  return !!get_gpgconf_item (WANT_GPG_ONE_MODE);
}



/* Helper function to return the basename of the passed filename.  */
const char *
_gpgme_get_basename (const char *name)
{
  const char *s;

  if (!name || !*name)
    return name;
  for (s = name + strlen (name) -1; s >= name; s--)
    if (*s == '/'
#ifdef HAVE_W32_SYSTEM
        || *s == '\\' || *s == ':'
#endif
        )
      return s+1;
  return name;
}


/* Return default values for various directories and file names.  */
const char *
gpgme_get_dirinfo (const char *what)
{
  if (!what)
    return NULL;
  else if (!strcmp (what, "homedir"))
    return get_gpgconf_item (WANT_HOMEDIR);
  else if (!strcmp (what, "agent-socket"))
    return get_gpgconf_item (WANT_AGENT_SOCKET);
  else if (!strcmp (what, "uiserver-socket"))
    return get_gpgconf_item (WANT_UISRV_SOCKET);
  else if (!strcmp (what, "gpgconf-name"))
    return get_gpgconf_item (WANT_GPGCONF_NAME);
  else if (!strcmp (what, "gpg-name"))
    return get_gpgconf_item (WANT_GPG_NAME);
  else if (!strcmp (what, "gpgsm-name"))
    return get_gpgconf_item (WANT_GPGSM_NAME);
  else if (!strcmp (what, "g13-name"))
    return get_gpgconf_item (WANT_G13_NAME);
  else
    return NULL;
}
