/*
 * Utility functions.
 * Copyright (C) 2001-2002 Darius Bacon
 */

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "idel_private.h"


/* Complain and terminate.  The error code is 128 to distinguish it
   from anything an idel program itself returns (which is in
   0..127). */
void
die (const char *message, ...)
{
  va_list args;

  fprintf (stderr, "%s: ", program_name);
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  fprintf(stderr, "\n");

  exit (128);
}


/* A fail-stop malloc(). */
void *
allot (size_t size)
{
  void *p = malloc (size);
  if (p == NULL && size != 0)
    die ("%s", strerror (errno));
  return p;
}


/* A fail-stop realloc(). */
void *
reallot (void *p, size_t size)
{
  void *q = realloc (p, size);
  if (q == NULL && size != 0)
    die ("%s", strerror (errno));
  return q;
}


/* Set (*result, *size) to a malloced block with the contents of `in',
   and its length.  Put a '\0' after the contents (not counted in the size). */
void
read_file (char **result, int *size, FILE *in)
{
  int r = 0;			/* bytes read so far */
  int a = 4096;			/* bytes allotted */
  char *buf = allot (a + 1);	/* allow extra byte for null terminator */

  for (;;)
    {
      int n = fread (buf + r, 1, a - r, in);
      r += n;
      if (r != a)
	break;
      a *= 2;
      buf = reallot (buf, a + 1);
    }

  if (ferror (in))
    die ("%s", strerror (errno));

  buf[r] = '\0';
  
  *result = buf;
  *size = r;
}


/* A fail-stop fopen(), treating a filename of "-" as stdin/stdout. */
FILE *
open_file (const char *filename, const char *mode)
{
  if (strcmp (filename, "-") == 0) 
    {
      assert (mode[0] == 'r' || mode[0] == 'w');
      return mode[0] == 'w' ? stdout : stdin;
    } 
  else
    {
      FILE *result = fopen (filename, mode);
      if (!result)
	die ("%s: %s", filename, strerror (errno));
      return result;
    }
}
