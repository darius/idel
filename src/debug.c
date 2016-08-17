/*
 * Support for debugging the implementation.
 * Copyright (C) 2001-2002 Darius Bacon
 */

#include "idel_private.h"

static const char *op_name[] = {
#include "names.inc"
};

static const int op_operand_count[] = {
#include "args.inc"
};


/* Return the number of operands that Idel opcode `op' takes, or -1
   if there's no such opcode. */
int
operand_count (int op)
{
  if (NELEMS (op_operand_count) <= (unsigned) op)
    return -1;
  return op_operand_count[op];
}


/* Return the Idel opcode corresponding to internal code `s', or -1 if none. */
int
lookup_op (cplabel s)
{
  int i, limit = NELEMS (op_name);
  for (i = 0; i < limit; ++i)
    if (opcode_labels[i] == s)
      return i;

  return -1;
}


/* Return a human-readable string describing the compiled instruction
   starting at `pc'.  The result is statically allocated: non-reentrant, etc. */
const char *
format_op_at (const cell *pc)
{
  /* The scratch buffer has to be big enough to fit all the sprintfs,
     or we have a potential exploit.  The format strings have limits
     on all the field sizes (like %15.15s) to ensure this.  (Actually
     it's safe anyway because all the instruction names are short, and
     this note is just here for defense in depth.) */
  static char scratch[80];
  int op = lookup_op (pc[0].lbl);

  if (NELEMS (op_name) <= (unsigned) op)
    sprintf (scratch, "%p: Bad opcode: %d", pc, op);
  else if (op == JUMP)
    sprintf (scratch, "%p: %-15.15s %p", pc, op_name[op], pc + 2 + pc[1].i32);
  else if (operand_count (op) == 1)
    sprintf (scratch, "%p: %-15.15s " x32_fmt, pc, op_name[op], pc[1].i32);
  else
    sprintf (scratch, "%p: %-15.15s", pc, op_name[op]);

  return scratch;
}


/* Return a human-readable string naming `opcode'. */
const char *
format_opcode (const i32 opcode)
{
  if (NELEMS (op_name) <= (unsigned) opcode)
    return "Bad_opcode";
  else
    return op_name[opcode];
}


/* Return vm's Entry for the defn starting at `p', or NULL if none. */
static const Entry *
find_entry (const VM *vm, const cell *p)
{
  int i;
  for (i = 0; i < vm->defn_number; ++i)
    if (vm->entries[i].address == p)
      return vm->entries + i;
  return NULL;
}


/* Display all vm's code on stdout. */
void
vm_dump_code (const VM *vm)
{
  int i;
  for (i = 0; i < vm->num_chunks; ++i)
    {
      const cell *end = vm->chunks[i] + chunk_size;
      const cell *p = (i == vm->num_chunks - 1 ? vm->there : vm->chunks[i]);
      while (p < end)
	{
	  const Entry *e = find_entry (vm, p);
	  if (e)
	    {
	      printf ("%p:\t\t%d %d (" i32_fmt ")\n", 
		      p, e->popping, e->pushing, p[0].i32);
	      ++p;
	    }
	  printf ("%s\n", format_op_at (p));
	  p += 1 + operand_count (lookup_op (p[0].lbl));
	}
    }
}


/* Display vm's stacks on stdout. */
void
vm_dump_stacks (const VM *vm)
{
  cell *sp = vm->sp, *rp = vm->rp, *stack = vm->stack;

  printf ("Data stack:\n");
  for (--sp; stack <= sp; --sp)
    printf ("%p: " x32_fmt "\n", sp, sp[0].i32);

  printf ("Return stack:\n");
  for (; rp < stack + vm->stack_size; rp++)
    printf ("%p: " x32_fmt "\n", sp, rp[0].i32);

  printf ("\n");
}
