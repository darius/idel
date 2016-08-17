/*
 * Idel assembler.
 * Copyright (C) 2001-2002 Darius Bacon
 */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "idel_private.h"


/* Forward refs */

static void syntax_error (const char *complaint, ...);


/* Misc */

static char *
string_dup (const char *s)
{
  size_t ss = strlen (s);
  char *t = allot (ss + 1);
  memcpy (t, s, ss);
  t[ss] = '\0';
  return t;
}


static char *
string_cat (const char *s, const char *t)
{
  size_t ss = strlen (s);
  size_t ts = strlen (t);
  char *u = allot (ss + ts + 1);
  memcpy (u, s, ss);
  memcpy (u + ss, t, ts);
  u[ss + ts] = '\0';
  return u;
}


/* Source locations */

typedef struct Place {
  int line;
  int column;
  const char *filename;
} Place;


static void
print_place (Place place)
{
  if (place.filename && place.filename[0] != '\0')
    fprintf (stderr, "%s:", place.filename);
  fprintf (stderr, "%d.%d: ", place.line, place.column);
}


/* This has a hard limit but it doesn't especially matter if you run into it. */
static char place_filenames[16384];
static int filename_ptr = 0;

static const char *
add_place_filename (const char *filename)
{
  char *next_slot = place_filenames + filename_ptr;
  int fs = strlen (filename) + 1;
  if ((int) sizeof place_filenames < filename_ptr + fs)
    return "(Too many files)";
  memcpy (next_slot, filename, fs);
  filename_ptr += fs;
  return next_slot;
}


static Place the_place = { 0, 0, NULL };


/* The code buffer */

/* We're using the archaic traditional segment names here: */
typedef enum { 
  text_segment, 
  data_segment, 
  bss_segment,
  absolute
} Segment;

typedef struct Insn {
  u8 opcode;
  i32 operand;
  Segment segment;
  const struct Symbol *symbol;
  Place place;
} Insn;

static int text_allotted;
static Insn *text;
static int text_ptr;

static void
setup_insns (void)
{
  text_allotted = 1024;
  text = allot (text_allotted * sizeof text[0]);
  text_ptr = 0;
}


static void
gen_insn (u8 opcode, i32 operand, Segment segment, const struct Symbol *e)
{
  if (text_allotted <= text_ptr)
    {
      text_allotted *= 2;
      text = reallot (text, text_allotted * sizeof text[0]);
    }
  {
    Insn *insn = &text[text_ptr++];
    insn->opcode = opcode;
    insn->operand = operand;
    insn->segment = segment;
    insn->symbol = e;
    insn->place = the_place;
  }
}


static void
gen2 (u8 opcode, i32 operand, Segment segment)
{
  gen_insn (opcode, operand, segment, NULL);
}


static void 
gen (i32 opcode)
{
  gen_insn (opcode, 0, absolute, NULL);
}


/* The data buffer */

static int data_allotted;	/* Inv: == a positive multiple of word_size */
static int data_ptr;		/* Inv: >= 0 and <= data_allotted */
static i8 *data;		/* An allotted buffer of size data_alloted.
				   All bytes at data_ptr and up (big-endian)
				   are 0. */

static int bss_ptr;		/* Inv: >= 0 */
static int bss_start;		/* Inv: >= 0 and a multiple of word_size */


static void
setup_data_space (void)
{
  data_allotted = 64 * word_size;
  data_ptr = 0;
  data = allot (data_allotted * sizeof data[0]);
  memset (data, 0, data_allotted * sizeof data[0]);

  bss_ptr = 0;
}


static void
ensure_space_for_data (void)
{
  if (data_allotted <= data_ptr)
    {
      int new_a = data_allotted * 2;
      data = reallot (data, new_a);
      memset (data + data_allotted, 0, data_allotted);
      data_allotted = new_a;
    }
}


static void
emit_byte (int value)
{
  if (value < -128 || 255 < value)
    syntax_error ("Byte literal out of range");
  ensure_space_for_data ();
  data [endianness ^ data_ptr++] = value;
}


static void
emit_int (int value)
{
  data_ptr = word_align (data_ptr);
  ensure_space_for_data ();
  *(i32 *)(data + data_ptr) = value;
  data_ptr += word_size;
}


/* The dictionary */

enum { unbound = -1 };		/* must be distinct from all opcodes */

typedef struct Symbol {

  /* Print-name */
  const char *name;			

  /* Can be a primitive, CALL, LOCAL, PUSH, or unbound. */
  int opcode;

  /* Depending on type of symbol:
     defn: -1 if not yet defined, else value of current_index at define time.
     local var: index of variable (starting from 0 for outermost)
     constant: constant value
     primitive: N/A */
  int index;

  /* What segment the index value is relative to, if applicable. */
  Segment segment;

  /* Value of `text_ptr' at define time.
     (Only relevant for a defn.) */
  int start;
  /* Value of `text_ptr' after fully defined.  -1 before fully defined.
     (Only relevant for a defn.) */
  int end;

  /* Interleave-encoded stack effect for defn.  0 before it's defined.
     (Only relevant for a defn.) */
  u32 stack_effect;

  /* Hash bucket list link */
  struct Symbol *next;

} Symbol;


enum { num_buckets = 1024 };	/* must be a power of 2 */

typedef struct Dictionary {
  int size;			/* how many names bound */
  int space;			/* how many slots allocated for stack */
  Symbol **stack;		/* all names currently bound, oldest first */
  Symbol *buckets[num_buckets];
} Dictionary;

static Dictionary globals, locals;

static void
setup_dictionary (Dictionary *d)
{
  int i;
  d->size = 0;
  d->space = 16;
  d->stack = allot (d->space * sizeof d->stack[0]);
  for (i = 0; i < num_buckets; ++i)
    d->buckets[i] = NULL;
}

static void
setup_dictionaries (void)
{
  setup_dictionary (&globals);
  setup_dictionary (&locals);
}


static unsigned
hash (const char *key)
{
  /* One-at-a-Time Hash from http://burtleburtle.net/bob/hash/doobs.html */
  unsigned hash = 0, i;
  for (i = 0; key[i]; ++i)
    {
      hash += key[i];
      hash += hash << 10;
      hash ^= hash >> 6;
    }
  hash += hash << 3;
  hash ^= hash >> 11;
  hash += hash << 15;
  return hash % num_buckets;
}


static Symbol *
bucket_find (Symbol *e, const char *word)
{
  for (; e != NULL; e = e->next)
    if (0 == strcmp (e->name, word))
      return e;

  return NULL;
}


static Symbol *
find (const Dictionary *d, const char *word)
{
  return bucket_find (d->buckets[hash (word)], word);
}


static void
new_header (Dictionary *d, const char *name, 
	    int opcode, int index, Segment segment, u32 se)
{
  unsigned b = hash (name);
  Symbol *found = bucket_find (d->buckets[b], name);
  Symbol *e = found;
  if (found == NULL)
    {
      e = allot (sizeof *e);
      e->next = d->buckets[b];
      d->buckets[b] = e;
    }
  else if (found->opcode != unbound)
    syntax_error ("Word redefined: `%s'", name); /* FIXME: locals should
                                                    be redefinable */

  e->name = name;
  e->opcode = opcode;
  e->index = index;
  e->segment = segment;
  e->start = text_ptr;
  e->end = -1;
  e->stack_effect = se;
  
  if (d->size == d->space)
    {
      d->space *= 2;
      d->stack = reallot (d->stack, d->space * sizeof d->stack[0]);
    }
  d->stack[d->size++] = e;
}


static void
unbind (Dictionary *d, int count)
{
  int i;
  for (i = 0; i < count; ++i)
    {
      Symbol *s = d->stack[d->size - i - 1];
      s->opcode = unbound;
      s->index = -1;
    }
  d->size -= count;
}


static int current_index = 0;

static void
create (const char *name, u32 stack_effect)
{
  Symbol *e = find (&globals, name); /* ugh */
  if (e == NULL)
    new_header (&globals, name, 
		CALL, current_index, text_segment, stack_effect);
  else if (e->index == -1)
    {
      e->index = current_index;
      e->start = text_ptr;
      e->stack_effect = stack_effect;
    }
  else
    syntax_error ("Redefinition not allowed");
}

static void
stub (const char *name)
{
  new_header (&globals, name, CALL, -1, text_segment, 0);
}


static void
created (const char *name)
{
  Symbol *e = find (&globals, name);
  assert (e->opcode == CALL);
  assert (e->start <= text_ptr);
  e->end = text_ptr;

  ++current_index;
}


static void
define_local (const char *name)
{
  new_header (&locals, string_dup (name), 
	      LOCAL, locals.size, absolute, 0);
}


static void
install_primitive (const char *name, int opcode)
{
  new_header (&globals, name, 
	      opcode, 0, absolute, 0);
}

static void
setup_primitives (void)
{
#include "dict.inc"
}


/* The parser */

static char *program = NULL;	/* text of the input program */

static const char *line_start;  /* start of current line within program */
static char *scan;		/* current position within program */
static unsigned char ch;	/* current character being scanned */

static char *token_text;	/* current token */
static int   token_value;	/* used only for char-literal tokens */


static void
syntax_error (const char *complaint, ...)
{
  va_list args;

  print_place (the_place);

  va_start(args, complaint);
  vfprintf(stderr, complaint, args);
  va_end(args);
  fprintf(stderr, "\n");

  exit (1);
}


static char
next (void)
{
  assert (ch != '\0');
  return ch = *++scan;
}


/* Try to parse token_text as a 32-bit number (either signed or unsigned).
   Return true iff successful, and set *result to the value. */
static int
parse_number (int *result)
{
  char *endptr;
  int value;

  if (token_text[0] == '\0')
    return 0;

  if (token_text[0] == '0' && token_text[1] == 'x')
    {
      errno = 0;
      value = strtoul (token_text + 2, &endptr, 16);
      if (*endptr != '\0' || errno == ERANGE)
	return 0;
    }
  else if (token_text[0] == '\'')
    value = token_value;	/* already parsed by gobble() */
  else
    {
      errno = 0;
      value = strtol (token_text, &endptr, 10);
      if (*endptr != '\0' || errno == ERANGE)
	{
	  errno = 0;
	  value = strtoul (token_text, &endptr, 10);
	  if (*endptr != '\0' || errno == ERANGE)
	    return 0;
	}
    }

  *result = value;
  return 1;
}


static int
hex_digit (char c)
{
  char lc = tolower (c);
  if ('0' <= lc && lc <= '9')
    return lc - '0';
  if ('a' <= lc && lc <= 'f')
    return lc - ('a' - 10);
  syntax_error ("Not a hex digit: '%c'", c);
  return 0;			/* to mollify the C compiler */
}


/* Parse the string at `scan' as an escape sequence.  (It must start
   with a `\' character.)  Advance to the end of it and return the
   character constant encoded. */
static int
scan_escape (void)
{
  next ();
  switch (ch)
    {
    case 'f':  return '\f';
    case 'n':  return '\n';
    case 'r':  return '\r';
    case 't':  return '\t';
    case '\\': return '\\';

    case 'x':
      {
	char d1 = next ();
	char d2 = (d1 == '\0' ? '\0' : next ());
	return 16 * hex_digit (d1) + hex_digit (d2);
      }

    case '\0': 
      syntax_error ("Unterminated string constant");
      return 0;
    default:   
      syntax_error ("Unknown escape code in string: '%c'", ch);
      return 0;
    }
}


static void
gobble_char_literal (void) 
{
  next ();
  if (ch == '\0')
    syntax_error ("Unterminated character constant");
  else if (ch == '\\')
    token_value = scan_escape ();
  else
    token_value = ch;

  next ();
  if (ch != '\'')
    syntax_error ("Malformed character constant");
  next ();
  if (!isspace (ch))
    syntax_error ("Malformed character constant");
  *scan = '\0';
}


static void
gobble (void) 
{
  token_text = scan;
  if (ch == '\'')
    gobble_char_literal ();
  else
    {
      while (ch != '\0' && !isspace (ch))
	next ();
      *scan = '\0';
    }
}


static void
skip_blanks (void)
{
  while (isspace (ch) && ch != '\n')
    next ();
}


static const char *
scan_filename (void)
{
  the_place.column = scan - line_start;

  if (ch != '"')
    syntax_error ("Bad # syntax");
  next ();

  token_text = scan;
  while (ch != '\0' && ch != '"')
    next ();

  if (ch != '"')
    syntax_error ("Bad # syntax");
  *scan = '\0';
  next ();

  return token_text;
}


static void
scan_line_directive (void)
{
  int line;
  const char *filename;

  next ();				/* skip over the '#' */
  skip_blanks ();
  gobble ();
  if (!parse_number (&line))
    syntax_error ("Bad # syntax");
  skip_blanks ();
  filename = scan_filename ();
  skip_blanks ();
  gobble ();
  skip_blanks ();
  if (ch != '\n' && ch != '\0')
    syntax_error ("Bad # syntax");
  if (ch) next ();
  
  the_place.line = line;
  the_place.filename = add_place_filename (filename);
}


static void
start_line (void)
{
  line_start = scan;
  the_place.line++;
  while (ch == '#')
    scan_line_directive ();
}


/* Skip whitespace and comments and the C preprocessor's # lines. */
static void
skip_preprocessor (void)
{
  for (;;)
    {
      char c = ch;
      while (isspace (c))
	{
	  next ();
	  if (c == '\n')
	    start_line ();
	  c = ch;
	}

      if (c != '\\')		/* comment character */
	break;
      do { next (); } while (ch != '\n' && ch != '\0');
    }
}


static void
advance (void) 
{
  skip_preprocessor ();
  the_place.column = scan - line_start + 1;
  gobble ();
}


static void
compile_number (void)
{
  int value;
  if (parse_number (&value))
    gen2 (PUSH, value, absolute);
  else
    {
      stub (string_dup (token_text));
      gen_insn (CALL, 0, text_segment, find (&globals, token_text));
    }
}


static void 
compile_token (void)
{
  const Symbol *e = find (&locals, token_text);
  if (e && e->opcode != unbound)
    gen2 (LOCAL, locals.size - 1 - e->index, absolute);
  else
    {
      const Symbol *e = find (&globals, token_text);
      if (e == NULL)
	compile_number ();
      else
	switch (e->opcode)
	  {
	  case CALL:  gen_insn (CALL, 0, e->segment, e); break;
	  case PUSH:  gen2 (PUSH, e->index, e->segment); break;
	  case LOCAL: unreachable (); break;
	  default:    gen (e->opcode); break;
	  }
    }
}


static int
seeing (const char *string)
{
  return 0 == strcmp (token_text, string);
}


static void
expect (const char *string)
{
  if (!seeing (string))
    syntax_error ("Missing `%s'", string);
}


static void
eat (const char *string)
{
  expect (string);
  advance ();
}


static int
see_locals (void)
{
  int before = locals.size;
  for (;;)
    {
      advance ();
      if (seeing ("--"))
	break;
      else if (seeing ("}"))
	syntax_error ("Missing `--'");
      else
	define_local (token_text);
    }
  return locals.size - before;
}


static void
nest (void)
{
  for (;;)
    {
      advance ();
      if (seeing ("{"))
	{
	  int count = see_locals ();
	  if (count != 0)
	    gen (BEGIN);
	  nest ();
	  expect ("}");
	  if (count != 0)
	    gen2 (DROP, count, absolute);
	  unbind (&locals, count);
	}
      else if (seeing ("}"))
	break;
      else if (seeing ("if"))
	{
	  gen (BEGIN);
	  nest ();
	  if (seeing ("else"))
	    {
	      gen (ELSE);
	      nest ();
	    }
	  expect ("then");
	  gen (THEN);
	}
      else if (seeing ("else"))
	break;
      else if (seeing ("then"))
	break;
      else if (seeing (";"))
	break;
      else if (seeing (""))
	break;
      else
	compile_token ();
    }
}


static u32
parse_stack_effect (void)
{
  int popping, pushing;
  advance ();
  if (!parse_number (&popping))
    syntax_error ("Number expected (pushing)");
  advance ();
  if (!parse_number (&pushing))
    syntax_error ("Number expected (popping)");
  return interleave_bits (popping, pushing);
}


static void
colon (void)
{
  u32 se = parse_stack_effect ();
  advance ();
  if (seeing ("")) syntax_error ("Name expected");
  {
    const char *name = string_dup (token_text);
    create (name, se);
    nest ();
    eat (";");
    created (name);
  }
}


static void
compile_bytes (void)
{
  int token_value;
  while (parse_number (&token_value))
    {
      emit_byte (token_value);
      advance ();
    }
}


static void
compile_ints (void)
{
  int token_value;
  while (parse_number (&token_value))
    {
      emit_int (token_value);
      advance ();
    }
}


static void
parse_bytes (void)
{
  advance ();
  if (seeing ("")) syntax_error ("Name expected");
  {
    const char *name = string_dup (token_text);
    new_header (&globals, name, 
		PUSH, data_ptr, data_segment, 0);
    advance ();
    compile_bytes ();
    eat (";bytes");
  }
}


static void
parse_ints (void)
{
  advance ();
  if (seeing ("")) syntax_error ("Name expected");
  {
    const char *name = string_dup (token_text);
    data_ptr = word_align (data_ptr);
    new_header (&globals, name, 
		PUSH, data_ptr, data_segment, 0);
    advance ();
    compile_ints ();
    eat (";ints");
  }
}


static void
compile_string_constant (void)
{
  skip_preprocessor ();
  if (ch != '"')
    syntax_error ("Expected a string constant");
  next ();

  for (; ch != '"'; next ())
    if (ch == '\0')
      syntax_error ("Unterminated string constant");
    else if (ch == '\\')
      emit_byte (scan_escape ());
    else
      emit_byte (ch);

  next ();
}


static void
parse_string (void)
{
  advance ();
  if (seeing ("")) syntax_error ("Name expected");
  {
    const char *addr_name = string_cat (token_text, ".addr");
    const char *size_name = string_cat (token_text, ".size");
    int start = data_ptr;

    new_header (&globals, addr_name, 
		PUSH, start, data_segment, 0);
    compile_string_constant ();
    new_header (&globals, size_name, 
		PUSH, data_ptr - start, data_segment, 0);

    advance ();
  }
}


static void
parse_bss_data (void)
{
  int value;

  advance ();
  if (seeing ("")) syntax_error ("Name expected");
  {
    const char *name = string_dup (token_text);
    new_header (&globals, name, 
		PUSH, bss_ptr, bss_segment, 0);
    advance ();
    if (!parse_number (&value))
      syntax_error ("Integer expected");
    if (value < 0)
      syntax_error ("Negative count");
    advance ();
    if (bss_ptr + value < bss_ptr)
      syntax_error ("BSS space overflow");
    bss_ptr += value;
  }
}


static void
compile_program (void)
{
  scan = program;
  ch = *scan;
  start_line ();
  advance ();
  while (!seeing (""))
    {
      if (seeing ("def"))
	colon ();
      else if (seeing ("bytes:"))
	parse_bytes ();
      else if (seeing ("ints:"))
	parse_ints ();
      else if (seeing ("string:"))
	parse_string ();
      else if (seeing ("bss-data:"))
	parse_bss_data ();
      else
	syntax_error ("Definition expected");
    }
}


/* Output */

static void
push_insn (OW *ow, const Insn *insn, int this_index)
{
  int op = insn->opcode;
  switch (op)
    {
    case DROP: 
    case LOCAL:
      push_u32 (ow, insn->operand);
      break;
    case PUSH: 
      if (insn->segment == bss_segment)
	push_i32 (ow, bss_start + insn->operand);
      else
	push_i32 (ow, insn->operand);
      break;
    case CALL:
      if (insn->symbol->index == -1)
	{
	  the_place = insn->place;
	  syntax_error ("Unresolved reference to %s", insn->symbol->name);
	}
      push_i32 (ow, insn->symbol->index - this_index);
      break;
    }
  push_u8 (ow, op);
}


static void
push_defn (OW *ow, const Symbol *e)
{
  int mark = start_subfile (ow);

  int i;
  for (i = e->start; i < e->end; ++i)
    push_insn (ow, &text[i], e->index);

  end_subfile (ow, mark);
}


static Symbol **
defns_fill (void)
{
  int d, i;
  Symbol **defn_symbol = allot (current_index * sizeof defn_symbol[0]);

  for (d = 0; d < current_index; ++d)
    defn_symbol[d] = NULL;
  for (i = 0; i < globals.size; ++i)
    {
      Symbol *e = globals.stack[i];
      if (e->opcode == CALL && e->index != -1)
	{
	  assert (0 <= e->index && e->index < current_index);
	  defn_symbol[e->index] = e;
	}
    }
  return defn_symbol;
}
  
static Symbol *
defns_ref (Symbol **defn_symbol, int i)
{
  assert (0 <= i && i < current_index);

  if (defn_symbol[i] == NULL)
    die ("Unresolved defn");	/* This can't happen (right?) */
  return defn_symbol[i];
}


static void
push_defns (OW *ow)
{
  int mark = start_subfile (ow);

  Symbol **defn_symbol = defns_fill ();
  int i;
  for (i = current_index - 1; 0 <= i; --i)
    push_defn (ow, defns_ref (defn_symbol, i));
  for (i = current_index - 1; 0 <= i; --i)
    push_u32 (ow, defns_ref (defn_symbol, i)->stack_effect);
  free (defn_symbol);

  push_u32 (ow, current_index);

  end_subfile (ow, mark);
  push_tag (ow, tag_defns);
}


static void
push_int_run (OW *ow, int top, int bottom)
{
  int mark = start_subfile (ow);

  while (bottom < top)
    {
      top -= word_size;
      push_i32 (ow, *(i32 *)(data + top));
    }

  end_subfile (ow, mark);
  push_tag (ow, tag_ints);
}


static void
push_byte_run (OW *ow, int top, int bottom)
{
  int mark = start_subfile (ow);

  while (bottom < top)
    {
      top -= 1;
      push_u8 (ow, data[endianness ^ top]);
    }

  end_subfile (ow, mark);
  push_tag (ow, tag_bytes);
}


static void
push_run (OW *ow, int top, int bottom, int int_encoded_size)
{
  if (int_encoded_size < top - bottom)
    push_int_run (ow, top, bottom);
  else
    push_byte_run (ow, top, bottom);
}


static int
block_size (int byte_count)
{
  return 1 + u32_encoded_size (byte_count) + byte_count;
}


static bool
should_join (int top, int bot1, int size1, int bot2, int size2)
{
  if (size1 < top - bot1)
    return size2 < block_size (bot1 - bot2);
  else
    return bot1 - bot2 < block_size (size2);
}


/* Traverse the run of data words ending at index `top' whose sizes
   when int-encoded are either all >= word_size or all <= word_size.
   Set *bottom to the index of the start of the run, and
   *int_encoded_size to the size as encoded.
   Pre: `top' points to a word boundary > 0. */
static void
find_run (int *bottom, int *int_encoded_size, int top)
{
  int bot = top;
  int sum = 0;		/* sum of the sizes from bot to top */
  int L, variety;

  /* Go through any initial run of size=word_size words */
  do {
      bot -= word_size;
      L = i32_encoded_size (*(i32 *)(data + bot));
      sum += L;
  } while (0 < bot && word_size != L);

  /* Now we know which variety of run we're looking for */
  variety = (L <= word_size);

  /* So go through the ensuing run of that type, and we're done */
  while (0 < bot)
    {
      L = i32_encoded_size (*(i32 *)(data + bot - word_size));
      if (variety != (L <= word_size))
	break;
      bot -= word_size, sum += L;
    }

  *bottom = bot;
  *int_encoded_size = sum;
}


/* Push a Zeroes block if needed to pad out to the total space, and
   return the address of the top of the nonzero data. */
static int
push_zeroes (OW *ow)
{
  int top = word_align (data_ptr);
  int space = top + word_align (bss_ptr);

  /* Skip down over any zeroes at the top of the data segment */
  while (0 < top && 0 == *(i32 *)(data + top - 4))
    top -= 4;

  if (top < space)
    {
      int mark = start_subfile (ow);
      push_i32 (ow, space - top);
      end_subfile (ow, mark);
      push_tag (ow, tag_zeroes);
    }

  return top;
}


/* Push all the static data onto `ow'.  Produces a combination of int
   and byte sections for space efficiency -- it's essentially doing
   run-length encoding on the int-encoded sizes of the data words.
   This lets us encode both arrays of small integers and arrays of
   random bytes with reasonable efficiency. */
static void
push_data (OW *ow)
{
  int top = push_zeroes (ow);

  int bot1, size1;
  int bot2, size2;

  if (top == 0)
    return;

  find_run (&bot1, &size1, top);

  while (0 < bot1)
    {
      find_run (&bot2, &size2, bot1);
      /* There's a run-boundary at bot1 now.  If switching the run
         type is too expensive we instead join the following two runs
         into the topmost one. */
      if (should_join (top, bot1, size1, bot2, size2))
	{
	  bot1 = bot2, size1 += size2;
	  if (0 < bot1)
	    {
	      find_run (&bot2, &size2, bot1);
	      bot1 = bot2, size1 += size2;
	    }
	}
      else
	{
	  push_run (ow, top, bot1, size1);
	  top = bot1, bot1 = bot2, size1 = size2;
	}
    }

  push_run (ow, top, bot1, size1);
}


static void
write_program (const char *comment)
{
  OW *ow = ow_make ();

  bss_start = word_align (data_ptr);

  push_data (ow);
  push_defns (ow);
  push_header (ow, comment);
  ow_write (stdout, ow);

  ow_unmake (ow);
}


/* The main program */

static void
process_file (const char *filename)
{
  int size;
  FILE *input = open_file (filename, "r");

  the_place.filename = (input == stdin ? NULL : filename);
  the_place.line = the_place.column = 0;

  read_file (&program, &size, input);
  fclose (input);
  compile_program ();
  free (program);
}


static void
show_version (void)
{
  printf ("idelasm version %s\n", idel_version_string);
  exit (0);
}


static void
show_help (void)
{
  printf (
"Usage: %s [option]... [file]...\n", program_name);
  printf (
"where the legal options are:\n"
"  -comment <string>\n"
"  -help\n"
"  -version\n"
"Report bugs to darius@accesscom.com.\n"
);
  exit (0);
}


static const char *
parse_comment (const char *comment)
{
  if (strchr (comment, '\n'))
    die ("Comment contains a newline: %s", comment);
  return comment;
}


const char *program_name;

int
main (int argc, char **argv)
{
  const char *comment = NULL;

  program_name = argv[0];

  setup_dictionaries ();
  setup_primitives ();
  setup_insns ();
  setup_data_space ();

  /* Parse command-line options */
  for (;;)
    {
      if (1 < argc)
	{
	  if (0 == strcmp ("-version", argv[1]))
	    show_version ();
	  if (0 == strcmp ("-help", argv[1]))
	    show_help ();
	}
      if (2 < argc)
	{
	  if (0 == strcmp ("-comment", argv[1]))
	    comment = parse_comment (argv[2]);
	  else
	    break;
	  argc -= 2, argv += 2;	  
	}
      else
	break;
    }

  if (argc == 1)
    process_file ("-");
  else
    for (; 1 < argc; ++argv, --argc)
      process_file (argv[1]);

  write_program (comment);

  return 0;
}
