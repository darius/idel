/*
 * Object format encoder.
 * Copyright (C) 2001-2002 Darius Bacon
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "idel_private.h"


OW *
ow_make (void)
{
  struct OW *ow = allot (sizeof *ow);

  ow->buffer = allot (512 * sizeof ow->buffer[0]);
  ow->ptr = ow->top = ow->buffer + 512;

  return ow;
}


void 
ow_unmake (OW *ow)
{
  free (ow->buffer);
  free (ow);
}


/* Expand ow's buffer so we can push more without overflowing it. */
void
ow_grow (OW *ow)
{
  /* TODO: use allot & memcpy instead of reallot & memmove? */

  int allotted = ow->top - ow->ptr;
  int allotted_new = allotted * 2;
  i8 *o_new = reallot (ow->buffer, allotted_new * sizeof ow->buffer[0]);
  i8 *t_new = o_new + allotted_new;
  i8 *p_new = t_new - allotted;
  
  memmove (p_new, o_new, allotted * sizeof ow->buffer[0]);
  
  ow->buffer = o_new, ow->top = t_new, ow->ptr = p_new;
}


void 
push_u8 (OW *ow, u8 u)
{
  if (ow->ptr == ow->buffer)
    ow_grow (ow);
  *--(ow->ptr) = u;
}


void
push_u32 (OW *ow, u32 u)
{
  push_u8 (ow, u & 0x7f);
  u >>= 7;
  for (; u != 0; u >>= 7)
    push_u8 (ow, 0x80 | (u & 0x7f));
}


int
u32_encoded_size (u32 u)
{
  int size = 1;
  u >>= 7;
  for (; u != 0; u >>= 7)
    ++size;
  return size;
}


void
push_tag (OW *ow, u32 u)
{
  push_u32 (ow, u);
}


void
push_i32 (OW *ow, i32 i)
{
  i32 endpoint = ASR (i, word_bits - 1);

  push_u8 (ow, i & 0x7f);
  for (;;)
    {
      i32 j = ASR (i, 7);
      /* We need to be careful that the high-order byte has
	 the correct sign bit, thus this complicated test: */
      if (j == endpoint && ((i ^ (i << 1)) & 0x80) == 0)
	break;
      push_u8 (ow, 0x80 | (j & 0x7f));
      i = j;
    }
}


int
i32_encoded_size (i32 i)
{
  i32 endpoint = ASR (i, word_bits - 1);
  int size = 1;

  for (;;)
    {
      i32 j = ASR (i, 7);
      /* We need to be careful that the high-order byte has
	 the correct sign bit, thus this complicated test: */
      if (j == endpoint && ((i ^ (i << 1)) & 0x80) == 0)
	break;
      ++size;
      i = j;
    }

  return size;
}


static void
push_string (OW *ow, const char *s)
{
  int i = strlen (s) - 1;
  for (; 0 <= i; --i)
    push_u8 (ow, s[i]);
}


void
push_header (OW *ow, const char *comment)
{
  push_u32 (ow, minor_version);
  push_u32 (ow, major_version);
  push_u8 (ow, 't');
  push_u8 (ow, 'o');
  push_u8 (ow, 'r');
  push_u8 (ow, 'F');

  if (comment != NULL)
    {
      push_u8 (ow, '\n');
      push_string (ow, comment);
      push_u8 (ow, '#');
    }
}


/* Return a mark depending on ow's current amount pushed. */
int
start_subfile (OW *ow)
{
  int mark = ow->top - ow->ptr;
  return mark;
}

/* Push the length of the subfile from the current position up to the
   marked position. */
void
end_subfile (OW *ow, int mark)
{
  push_u32 (ow, (ow->top - ow->ptr) - mark);
}


/* Write the contents of `ow' to `fp'. */
void
ow_write (FILE *fp, OW *ow)
{
  int size = ow->top - ow->ptr;
  if ((size_t)size != fwrite (ow->ptr, sizeof ow->ptr[0], size, fp))
    die ("Couldn't write image: %s", strerror (errno));
}
