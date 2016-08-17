/*
 * Object format decoder.
 * Copyright (C) 2001-2002 Darius Bacon
 */

#include <stdlib.h>
#include <string.h>

#include "idel_private.h"


/* Return an OR holding the string `input' of length `size'.
   (The result refers to `input', not a copy of it.) */
static OR *
or_make (char *input, int size)
{
  OR *or = allot (sizeof *or);
  or->buffer = (i8 *) input;
  or->ptr = or->buffer;
  or->limit = or->buffer + size;
  return or;
}


/* Return an OR holding the contents of `in'. */
OR *
read_program (FILE *in)
{
  char *input;
  int size;
  read_file (&input, &size, in);
  return or_make (input, size);
}


u8
pop_u8 (OR *or)
{
  if (or->limit <= or->ptr)
    die ("Overran limit");
  return *(or->ptr)++;
}


u32
pop_u32 (OR *or)
{
  u8 b = pop_u8 (or);
  u32 i = b & 0x7f;
  while (0 != (b & 0x80))
    {
      b = pop_u8 (or);
      i = (i << 7) | (b & 0x7f);
    }
  return i;
}


u32
pop_tag (OR *or)
{
  return pop_u32 (or);
}


static i32
sign_extend(i32 x, unsigned bits)
{
  return ASR (x << bits, bits);
}


i32
pop_i32 (OR *or)
{
  u8 b = pop_u8 (or);
  i32 i = sign_extend (b & 0x7f, word_bits - 7);

  while (0 != (b & 0x80))
    {
      b = pop_u8 (or);
      i = (i << 7) | (b & 0x7f);
    }
  return i;
}


/* Pop a length limit and reset `or' to only read up to that length.
   Return a tag we can restore the old limit from. */
i8 *
start_subfile_in (OR *or)
{
  u32 u = pop_u32 (or);
  i8 *old_limit = or->limit;
  or->limit = or->ptr + u;
  return old_limit;
}

/* Pre: old_limit was the value of the most recent nested call to
   start_subfile_in(or).
   Restore the old read-limit. */
void
end_subfile_in (OR *or, i8 *old_limit)
{
  or->limit = old_limit;
}


static void
skip_line (OR *or)
{
  while (pop_u8 (or) != '\n')
    ;
}


void
pop_header (OR *or)
{
  u8 w = pop_u8 (or);
  if (w == '#')
    {
      skip_line (or);
      w = pop_u8 (or);
    }

  {
    u8 x = pop_u8 (or);
    u8 y = pop_u8 (or);
    u8 z = pop_u8 (or);
    if (!(w == 'F' && x == 'r' && y == 'o' && z == 't'))
      die ("Bad magic");
    {
      i32 major = pop_u32 (or);	/* major version */
      i32 minor = pop_u32 (or);	/* minor version */
      
      if (major != major_version ||
	  (major == 0 && minor != minor_version))
	die ("Incompatible Idel version");
    }
  }
}
