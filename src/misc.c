/* misc.c
 *
 * Miscellaneous functions.
 * (This used to be "handledb.c")
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id$
 */

#include "config.h"
#include <stdio.h>
#include <sys/param.h>		/* For MAXPATHLEN */
#include <sys/types.h>		/* For stat() */
#include <sys/stat.h>		/* For stat() */
#include <ctype.h>		/* For isprint() and friends */
#include <errno.h>		/* For errno. Duh. */
#include <syslog.h>		/* For syslog() */

#if STDC_HEADERS
# include <string.h>		/* For memcpy() et al. */
# include <stdarg.h>		/* Variable-length argument lists */
#else	/* STDC_HEADERS */
# ifndef HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif	/* HAVE_STRCHR */
# ifndef HAVE_MEMCPY
#  define memcpy(d,s,n)		bcopy ((s), (d), (n))
#  define memmove(d,s,n)	bcopy ((s), (d), (n))
# endif	/* HAVE_MEMCPY */
#endif	/* STDC_HEADERS */

/* XXX - Should this go in the "else" clause, above? */
#if HAVE_STRINGS_H
#  include <strings.h>		/* For bzero() */
#endif	/* HAVE_STRINGS_H */

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "coldsync.h"
#include "pconn/pconn.h"
#include "pdb.h"
#include "conduit.h"

static int stat_err;		/* Return value from last stat() or
				 * lstat(), used by (l)exists() and is_*(),
				 * below.
				 */
static struct stat statbuf;	/* Results of last stat() or lstat(), used
				 * by (l)exists() and is_*(), below.
				 */

/* XXX - Warn(), Error(), and Perror() are very similar. They should be
 * reimplemented as stubs that call some other, common function.
 */

/* Warn
 * Print a warning message to either stderr or syslog, as appropriate.
 * Takes printf()-like arguments.
 * Returns a negative value if unsuccessful.
 */
int
Warn(const char *format, ...)
{
	int err = 0;
	va_list ap;

	va_start(ap, format);

	fprintf(stderr, _("Warning: "));
	err = vfprintf(stderr, format, ap);
	fprintf(stderr, "\n");

	/* Log with syslog, if necessary */
	if (global_opts.use_syslog)
	{
		/* It'd be nice to use vsyslog() here, but I'm pretty sure
		 * it's not portable.
		 */
		char msgbuf[256];	/* Error messages shouldn't be long */

		err = vsnprintf(msgbuf, sizeof(msgbuf),
				format, ap);
		syslog(LOG_WARNING, msgbuf);
	}

	return err;
}

/* Error
 * Print an error message to either stderr or syslog, as appropriate.
 * Takes printf()-like arguments.
 * Returns a negative value if unsuccessful.
 */
int
Error(const char *format, ...)
{
	int err = 0;
	va_list ap;

	va_start(ap, format);

	fprintf(stderr, _("Error: "));
	err = vfprintf(stderr, format, ap);
	fprintf(stderr, "\n");

	/* Log with syslog, if necessary */
	if (global_opts.use_syslog)
	{
		/* It'd be nice to use vsyslog() here, but I'm pretty sure
		 * it's not portable.
		 */
		char msgbuf[256];	/* Error messages shouldn't be long */

		err = vsnprintf(msgbuf, sizeof(msgbuf),
				format, ap);
		syslog(LOG_ERR, msgbuf);
	}

	return err;
}

/* Perror
 * Prints 'str', a colon, and the error message that corresponds to 'errno'
 * to either stderr or syslog, as appropriate.
 */
void
Perror(const char *str)
{
	perror(str);

	/* Log with syslog, if necessary */
	if (global_opts.use_syslog)
	{
		/* It'd be nice to use vsyslog() here, but I'm pretty sure
		 * it's not portable.
		 */
		char msgbuf[256];	/* Error messages shouldn't be long */

		snprintf(msgbuf, sizeof(msgbuf), "%s: %s",
			 str, strerror(errno));
		syslog(LOG_ERR, msgbuf);
	}
}

/* Verbose
 * Prints a message if the overall verbosity is >= 'level'.
 * Takes printf()-like arguments.
 * Returns a negative value if unsuccessful.
 */
int
Verbose(const int level, const char *format, ...)
{
	int err = 0;
	va_list ap;

	if (global_opts.verbosity < level)
		return 0;		/* Don't print this message */

	va_start(ap, format);

	err = vfprintf(stderr, format, ap);
	fprintf(stderr, "\n");

	/* Log with syslog, if necessary */
	if (global_opts.use_syslog)
	{
		/* It'd be nice to use vsyslog() here, but I'm pretty sure
		 * it's not portable.
		 */
		char msgbuf[256];	/* Error messages shouldn't be long */

		err = vsnprintf(msgbuf, sizeof(msgbuf),
				format, ap);
		syslog(LOG_INFO, msgbuf);
	}

	return err;
}

/* mkfname
 * Concatenate all of the arguments, and return a pointer to the resulting
 * string. Since the intended purpose of this is to create file pathnames,
 * the returned string is NUL-terminated, and does not exceed MAXPATHLEN
 * characters in length.
 * The last argument must be NULL.
 * This function simply concatenates strings. It does not try to separate
 * pathname elements with "/", so the caller needs to supply them.
 *
 * mkfname() returns a pointer to its own storage, so a) the caller need
 * not free() it, and b) the caller should make a private copy of the
 * returned string.
 */
const char *
mkfname(const char *first, ...)
{
	va_list ap;
	static char buf[MAXPATHLEN+1];	/* Result will be placed here */
	const char *str;		/* Current string */
	int len;

	MISC_TRACE(7)
		fprintf(stderr, "Inside mkfname()\n");

	va_start(ap, first);
	str = first;
	MISC_TRACE(8)
		fprintf(stderr, "First str == %p [%s]\n", str, str);
	len = 0;
	while (str != NULL)
	{
		MISC_TRACE(7)
			fprintf(stderr, "Appending \"%s\"\n",
				(str == NULL ? "NULL" : str));

		/* Append this string to 'buf' */
		for (; *str != '\0'; str++)
		{
			buf[len] = *str;
			len++;
			if (len >= MAXPATHLEN)
				goto done;
		}
		str = va_arg(ap, const char *);
				/* Advance to the next argument */
		MISC_TRACE(8)
			fprintf(stderr, "Now str == 0x%08lx [%s]\n",
				(long) str, str);
	}
  done:
	va_end(ap);

	buf[len] = '\0';

	return buf;
}

/* mkpdbname
 * Append the name of `dbinfo' to the directory `dirname', escaping any
 * weird characters so that the result can be used as a pathname.
 *
 * If `add_suffix' is true, appends ".pdb" or ".prc", as appropriate, to
 * the filename.
 *
 * Returns a pointer to the resulting filename.
 */
const char *
mkpdbname(const char *dirname,
	  const struct dlp_dbinfo *dbinfo,
	  Bool add_suffix)
{
	const char *retval;
	static char namebuf[(DLPCMD_DBNAME_LEN * 3) + 1];
				/* Buffer to hold the converted name (with
				 * all of the weird characters escaped).
				 * Since an escaped character is 3
				 * characters long, this buffer need only
				 * be 3 times the max. length of the
				 * database name (plus one for the \0).
				 */
	int i;
	char *nptr;

	MISC_TRACE(3)
		fprintf(stderr, "Inside mkpdbname(\"%s\",\"%s\")\n",
			dirname, dbinfo->name);
	/* If there are any weird characters in the database's name, escape
	 * them before trying to create a file by that name. Any "weird"
	 * characters are replaced by "%HH", where HH is the ASCII value
	 * (hex) of the weird character.
	 */
	nptr = namebuf;
	for (i = 0; i < DLPCMD_DBNAME_LEN; i++)
	{
		if (dbinfo->name[i] == '\0')
			break;

		/* Is this a weird character? */
		if ((!isprint((int) dbinfo->name[i])) ||
		    (dbinfo->name[i] == '/') ||	/* '/' is a weird character */
		    (dbinfo->name[i] == '%'))	/* The escape character
						 * needs to be escaped.
						 */
		{
			/* Escape it */
			sprintf(nptr, "%%%02X",
				(unsigned char) dbinfo->name[i]);
			nptr += 3;
		} else {
			/* Just a regular character */
			*nptr = dbinfo->name[i];
			nptr++;
		}
	}
	*nptr = '\0';

	retval = mkfname(dirname,
			 "/",
			 namebuf,
			 (add_suffix ?
			  (DBINFO_ISRSRC(dbinfo) ? ".prc" : ".pdb")
			  : ""),
			 NULL);

	MISC_TRACE(3)
		fprintf(stderr, "mkpdbname:    -> \"%s\"\n", retval);

	return retval;
}

/* mkbakfname
 * Given a database, construct the standard backup filename for it, and
 * return a pointer to it.
 * The caller need not free the string. OTOH, if ve wants to do anything
 * with it later, ve needs to make a copy.
 * This isn't a method in GenericConduit because it's generic enough that
 * other conduits might want to make use of it.
 */
const char *
mkbakfname(const struct dlp_dbinfo *dbinfo)
{
	return mkpdbname(backupdir, dbinfo, True);
}

/* mkinstfname
 * Similar to mkbakfname(), but constructs a filename in the install
 * directory (~/.palm/install by default).
 */
const char *
mkinstfname(const struct dlp_dbinfo *dbinfo)
{
	return mkpdbname(installdir, dbinfo, True);
}

/* mkarchfname
 * Similar to mkbakfname(), but constructs a filename in the archive
 * directory (~/.palm/archive by default).
 */
const char *
mkarchfname(const struct dlp_dbinfo *dbinfo)
{
	return mkpdbname(archivedir, dbinfo, False);
}

/* hex2int
 * Quick and dirty utility function to convert a hex digit to its numerical
 * equivalent.
 */
static int
hex2int(const char hex)
{
	switch (hex)
	{
	    case '0':
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	    case '8':
	    case '9':
		return (int) hex - '0';
	    case 'a':
	    case 'b':
	    case 'c':
	    case 'd':
	    case 'e':
	    case 'f':
		return (int) hex - 'a' + 0xa;
	    case 'A':
	    case 'B':
	    case 'C':
	    case 'D':
	    case 'E':
	    case 'F':
		return (int) hex - 'A' + 0xa;
	    default:
		return -1;
	}
}

/* fname2dbname
 * Convert a file name to a database name: strip off the trailing ".prc" or
 * ".pdb", if any, and convert any %HH sequences back to their
 * corresponding values.
 * Returns a string with the database name, or NULL if the pathname does
 * not correspond to a database name.
 */
const char *
fname2dbname(const char *fname)
{
	int i;
	static char dbname[DLPCMD_DBNAME_LEN+1];
	const char *baseptr;		/* Pointer to last '/' in pathname */
	const char *dotptr;		/* Pointer to extension */

	MISC_TRACE(3)
		fprintf(stderr, "fname2dbname: Unescaping \"%s\"\n",
			fname);

	/* Clear the database name, just 'cos. */
	bzero((void *) dbname, DLPCMD_DBNAME_LEN+1);

	/* Find the beginning of the filename. This is either the character
	 * just after the last '/', or the beginning of the string.
	 */
	baseptr = strrchr(fname, '/');
	if (baseptr == NULL)
		/* No directories, just a plain filename */
		baseptr = fname;
	else
		baseptr++;

	/* Find the extension (last '.' after the '/') */
	dotptr = strrchr(baseptr, '.');
	if (dotptr == NULL)
		/* No dot. But there really should be a ".prc" or ".pdb"
		 * extension, so this isn't a database name.
		 */
		return NULL;

	/* See if the extension is ".prc" or ".pdb" */
	if ((strcasecmp(dotptr, ".prc") != 0) &&
	    (strcasecmp(dotptr, ".pdb") != 0))
		/* Bad extension. It's not a database name */
		return NULL;

	/* Copy the base name, removing escapes, to 'dbname' */
	for (i = 0; i < DLPCMD_DBNAME_LEN; i++)
	{
		if ((baseptr == dotptr) ||
		    (*baseptr == '\0'))
			break;	/* End of name */
				/* We should never get to the end of the
				 * string, mind.
				 */

		if (*baseptr == '%')
		{
			int first;	/* First hex digit */
			int second;	/* Second hex digit */

			/* Escaped character */

			if ((first = hex2int(baseptr[1])) < 0)
				/* Not a valid hex digit */
				return NULL;
			if ((second = hex2int(baseptr[2])) < 0)
				/* Not a valid hex digit */
				return NULL;

			dbname[i] = (first << 4) | second;
			baseptr += 3;

			continue;
		}

		/* Just a normal character */
		dbname[i] = *baseptr;
		baseptr++;
	}

	/* Return 'dbname' */
	MISC_TRACE(3)
		fprintf(stderr, "fname2dbname: Returning  \"%s\"\n",
			dbname);
	return dbname;
}

/* The next few functions cache their results in 'stat_err' and 'statbuf'
 * (see above).
 */

/* exists
 * Returns True iff 'fname' exists. If 'fname' is NULL, uses the results of
 * the previous call to exists(), lexists(), or is_*().
 */
const Bool
exists(const char *fname)
{
	/* stat() the file, if it was given */
	if (fname != NULL)
		stat_err = stat(fname, &statbuf);

	return stat_err < 0 ? False : True;
}

/* lexists
 * Returns True iff 'fname' exists. If 'fname' is NULL, uses the results of
 * the previous call to exists(), lexists(), or is_*().
 * This differs from exists() in that it uses lstat(), so the file may be a
 * symlink.
 */
const Bool
lexists(const char *fname)
{
	/* lstat() the file, if it was given */
	if (fname != NULL)
		stat_err = lstat(fname, &statbuf);

	return stat_err < 0 ? False : True;
}

/* is_file
 * Returns True iff 'fname' exists and is a plain file. If 'fname' is NULL,
 * uses the results of the previous call to exists(), lexists(), or is_*().
 */
const Bool
is_file(const char *fname)
{
	/* stat() the file, if it was given */
	if (fname != NULL)
		stat_err = stat(fname, &statbuf);

	/* See if the file exists */
	if (stat_err < 0)
		return False;		/* Nope */

	/* See if the file is a plain file */
	return S_ISREG(statbuf.st_mode) ? True : False;
}

/* is_directory
 * Returns True iff 'fname' exists and is a directory. If 'fname' is NULL,
 * uses the results of the previous call to exists(), lexists(), or is_*().
 */
const Bool
is_directory(const char *fname)
{
	/* stat() the file, if it was given */
	if (fname != NULL)
		stat_err = stat(fname, &statbuf);

	/* See if the file exists */
	if (stat_err < 0)
		return False;		/* Nope */

	/* See if the file is a directory */
	return S_ISDIR(statbuf.st_mode) ? True : False;
}

/* is_database_name
 * Returns True iff 'fname' is a valid name for a Palm database, i.e., if
 * it ends in ".pdb", ".prc", or ".pqa".
 */
const Bool
is_database_name(const char *fname)
{
	const char *lastdot;		/* Pointer to last dot in file name */

	if (fname == NULL)
		/* Trivial case */
		return False;

	/* Find the last dot in the file name */
	lastdot = strrchr(fname, '.');

	if (lastdot == NULL)
		/* No dot, hence no extension, hence it's not a valid
		 * database name.
		 */
		return False;

	/* Test for the various legal extensions */
	if (strcasecmp(lastdot, ".pdb") == 0)
		return True;
	if (strcasecmp(lastdot, ".prc") == 0)
		return True;
	if (strcasecmp(lastdot, ".pqa") == 0)
		return True;

	return False;		/* None of the above */
}

/* Bool3str
 * Return the printed representation of a Bool3.
 */
const char *
Bool3str(const Bool3 var)
{
	switch (var)
	{
	    case False3:
		return "False";
	    case True3:
		return "True";
	    case Undefined:
		return "Undefined";
	}
	return "*** UNEXPECTED VALUE ***";
}

/* Stone-knives-and-bearskins memory leak detection:
 * See the comments in "config.h.in". These functions are just wrappers
 * around various memory-management functions. They print their arguments
 * and return values. This enables an external program to read the output
 * of the program and try to find memory leaks.
 */
#if WITH_LEAK_DETECTION
   /* Undefine the memory-management functions that were defined in
    * "config.h", so that we can use them for real.
    */
#  undef malloc
#  undef calloc
#  undef realloc
#  undef strdup
#  undef free

void *
my_malloc(const int size, const char *file, const int line)
{
	extern void *malloc(const int size);
	void *retval;

	retval = malloc(size);
	fprintf(stderr, "MEM: %s: %d: malloc(%d) returns 0x%08lx\n",
		file, line, size, (unsigned long) retval);
	return retval;
}

void *
my_calloc(const int num, const int size, const char *file, const int line)
{
	extern void *calloc(const int num, const int size);
	void *retval;

	retval = calloc(num, size);
	fprintf(stderr, "MEM: %s: %d: calloc(%d, %d) returns 0x%08lx\n",
		file, line, num, size, (unsigned long) retval);
	return retval;
}

void *
my_realloc(const void *ptr,
	   const int size,
	   const char *file,
	   const int line)
{
	extern void *realloc(const void *ptr, const int size);
	void *retval;

	retval = realloc(ptr, size);
	fprintf(stderr, "MEM: %s: %d: realloc(0x%08lx, %d) returns 0x%08lx\n",
		file, line, (unsigned long) ptr, size, (unsigned long) retval);
	return retval;
}

char *
my_strdup(const char *str, const char *file, const int line)
{
	extern char *strdup(const char *str);
	char *retval;

	retval = strdup(str);
	fprintf(stderr, "MEM: %s: %d: strdup(%s) returns 0x%08lx\n",
		file, line, str, (unsigned long) retval);
	return retval;
}

void
my_free(void *ptr, const char *file, const int line)
{
	extern void free(void *ptr);

	fprintf(stderr, "MEM: %s: %d: free(0x%08lx)\n",
		file, line, (unsigned long) ptr);
	free(ptr);
}
#endif	/* WITH_LEAK_DETECTION */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
