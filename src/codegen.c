/*
 * Internal code generator.
 * Copyright (C) 2001-2002 Darius Bacon
 *
 * Read an object file and generate the internal form of threaded code
 * implementing it.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "idel_private.h"


/* Equivalent to die() but more pertinent to object-file parsing.  */
static void
bad_input (const char *message, ...)
{
  va_list args;

  fprintf (stderr, "%s: Bad object file: ", program_name);
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  fprintf(stderr, "\n");

  exit (128);
}


/* Like bad_input() but giving the current defn #. */
static void
bad_defn (const VM *vm, const char *message, ...)
{
  va_list args;

  fprintf (stderr, "%s: Bad object file in defn #%d: ", 
	   program_name, vm->defn_number);
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  fprintf(stderr, "\n");

  exit (128);
}


/* Return a new vm with codegen-private fields initialized. */
VM *
vm_make (int stack_size, int data_size, int profiling)
{
  VM *vm = vm_sort_of_make (stack_size, data_size);

  assert (false == profiling || true == profiling);
  vm->profiling = profiling;
  vm->tallies_size = 0;
  vm->num_tallies = 0;
  vm->tallies = NULL;

  vm->ns_size = stack_size;
  vm->nesting = allot (stack_size * sizeof vm->nesting[0]);
  vm->nsp = vm->nesting;

  vm->height = vm->locals = vm->max_width = 0;
  vm->returning = 0;
  vm->at_jump_target = false;
  vm->bb_end = NULL;

  vm->defn_number = 0;
  vm->block_start = vm->block_end = 0;
  vm->entries_size = 256;
  vm->entries = allot (vm->entries_size * sizeof vm->entries[0]);

  vm->there = vm->chunks[0] + chunk_size;
  vm->data_ptr = 0;

  if (paranoid) vm_check (vm);
  return vm;
}


/* Add a new tally entry with a 0 count for the [start..end] code block.
   Return the index of the tally counter. */
static int
add_tally (VM *vm, int defn, cell *start, cell *end)
{
  int i = vm->num_tallies++;

  if (i == vm->tallies_size)
    {
      if (0 == i)
	{
	  vm->tallies_size = 1;
	  vm->tallies = allot (sizeof vm->tallies[0]);
	}
      else
	{
	  vm->tallies_size *= 2;
	  vm->tallies = 
	    reallot (vm->tallies, vm->tallies_size * sizeof vm->tallies[0]);
	}
    }

  vm->tallies[i].count = 0;
  vm->tallies[i].defn = defn;
  vm->tallies[i].start = start;
  vm->tallies[i].end = end;

  return i;
}


/* Write vm's profile data to `out'. */
void
vm_dump_profile (VM *vm, FILE *out)
{
  int i;
  for (i = 0; i < vm->num_tallies; ++i)
    {
      const Tally *t = &vm->tallies[i];
      const cell *p = t->start;
      fprintf (out, "%10lu %3d", t->count, t->defn);
      assert (p <= t->end);
      while (p != t->end)
	{
	  int opcode = lookup_op (p->ptr);
	  int operands = operand_count (opcode);
	  assert (0 <= opcode && 0 <= operands);
	  fprintf (out, " %s", format_opcode (opcode));
	  p += 1 + operands;
	  assert (p <= t->end);
	}
      fprintf (out, "\n");
    }
}


/* Code emission */

/* Ensure that vm's data space can hold at least one word at its data_ptr. */
static void
ensure_data_space (VM *vm)
{
  if (vm->data_size <= vm->data_ptr)
    bad_input ("Out of data space");
}


/* Append b to vm's data space. */
static void
emit_data_byte (VM *vm, i8 b)
{
  ensure_data_space (vm);
  vm->data[endianness ^ vm->data_ptr] = b;
  vm->data_ptr += 1;
}


/* Append i to vm's data space. */
static void
emit_data_int (VM *vm, i32 i)
{
  vm->data_ptr = word_align (vm->data_ptr);
  ensure_data_space (vm);
  ((cell *)(vm->data + vm->data_ptr))->i32 = i;
  vm->data_ptr += word_size;
}


/* Mark the beginning of the current basic block (at vm->there) and
   the end of a new one. */
static void
basic_block_boundary (VM *vm)
{
  if (vm->profiling)
    if (vm->bb_end != vm->there)
      {
	int counter = add_tally (vm, vm->defn_number, vm->there, vm->bb_end);

	/* These lines are the same as: really_emit1 (vm, TALLY, counter); 
	   except with no peepholing and leaving at_jump_target alone. */
	/* TODO: perhaps we should allow peepholing */
	vm->there[-2].lbl = opcode_labels[TALLY];
	vm->there[-1].i32 = counter;
	vm->there -= 2;

	vm->bb_end = vm->there;
      }
}


/* Move vm's new-code pointer into a freshly allocated chunk, leaving
   a JUMP to the former new-code location. */
static void
move_to_new_chunk (VM *vm)
{
  if (paranoid) vm_check (vm);

  /* This is effectively a new basic block since we assume a BB's
     instructions are contiguous in memory. */
  basic_block_boundary (vm);

  /* Fill current chunk's unused space, just to make debugging easier */
  {
    cell *p = vm->there - 1;
    for (; vm->chunks[vm->num_chunks - 1] <= p; --p)
      p->i32 = -1;		/* garbage value */
  }

  /* Add another chunk to the chunks array */
  if (vm->num_chunks == vm->chunks_size)
    {
      vm->chunks_size *= 2;
      vm->chunks = reallot (vm->chunks, vm->chunks_size * sizeof vm->chunks[0]);
    }
  vm->chunks[vm->num_chunks] = 
    allot (chunk_size * sizeof vm->chunks[0][0]);

  /* Set the code allocation pointer and emit a jump to where we left off */
  {				
    cell *continue_point = vm->there;

    /* Finish splitting the current basic block */
    vm->bb_end = vm->chunks[vm->num_chunks] + chunk_size;

    vm->there = vm->bb_end - 2;
    vm->there[0].lbl = opcode_labels[JUMP];
    vm->there[1].ptr = continue_point;
  }

  vm->num_chunks++;
  if (paranoid) vm_check (vm);
}


/* Ensure that vm's current chunk has space for an n-operand instruction.
   Pre: 0 <= n < chunk_size */
static void
reserve (VM *vm, int n)
{
  /* The +2 reserves space for a TALLY instruction in case
     basic_block_boundary() needs it. */
  if (vm->there <= vm->chunks[vm->num_chunks - 1] + 2 + n)
    move_to_new_chunk (vm);
}


/* Prepend w to vm's code space. */
static void 
emit_lit (VM *vm, i32 w)
{
  reserve (vm, 0);
  (--vm->there)[0].i32 = w;
}


/* Pre: vm's current chunk has space for a 0-operand instruction.
   Prepend a no-operand instruction to vm's code space. */
static void 
really_emit (VM *vm, int opcode)
{
  int combined_opcode = -1;
  if (!vm->at_jump_target)
    switch (opcode)
      {
#include "peep.inc"
      }

  if (0 <= combined_opcode)
    vm->there[0].lbl = opcode_labels[combined_opcode];
  else
    (--vm->there)[0].lbl = opcode_labels[opcode];

  vm->at_jump_target = false;
}


/* Pre: vm's current chunk has space for a 1-operand instruction.
   Prepend a 1-operand instruction to vm's code space. */
static void
really_emit1 (VM *vm, int opcode, cell operand)
{
  int combined_opcode = -1;
  if (!vm->at_jump_target)
    switch (opcode)
      {
#include "peep1.inc"
      }

  if (0 <= combined_opcode)
    {
      vm->there[-1].lbl = opcode_labels[combined_opcode];
      vm->there[ 0] = operand;
      vm->there -= 1;
    }
  else
    {
      vm->there[-2].lbl = opcode_labels[opcode];
      vm->there[-1] = operand;
      vm->there -= 2;
    }

  vm->at_jump_target = false;
}


/* Prepend a no-operand instruction to vm's code space. */
static void 
emit (VM *vm, int opcode)
{
  reserve (vm, 0);
  really_emit (vm, opcode);
}


/* Prepend a 1-operand instruction to vm's code space. */
static void
emit1 (VM *vm, int opcode, cell operand)
{
  reserve (vm, 1);
  really_emit1 (vm, opcode, operand);
}


/* Code generation */

typedef struct Insn {
  u8 opcode;
  i32 operand;
} Insn;


/* Set up to generate a block of n defns. */
static void
start_defns (VM *vm, u32 n)
{
  int i;
  if (paranoid) vm_check (vm);

  vm->block_start = vm->defn_number;
  vm->block_end = vm->defn_number + n;
  if (vm->entries_size < vm->block_end)
    {
      do {
	vm->entries_size *= 2;
      } while (vm->entries_size < vm->block_end);
      vm->entries = reallot (vm->entries, 
			     vm->entries_size * sizeof vm->entries[0]);
    }
  for (i = vm->block_start; i < vm->block_end; ++i)
    {
      vm->entries[i].address = NULL;
      vm->entries[i].refs = NULL;
    }

  if (paranoid) vm_check (vm);
}


/* Finish generating the current defns block. */
static void
finish_defns (VM *vm)
{
  if (paranoid)
    {
      /* Check that all references were resolved. */
      int i;
      for (i = vm->block_start; i < vm->block_end; ++i)
	assert (vm->entries[i].address != NULL);

      vm_check (vm);
    }
}


/* Return the entry for defn #`defn'. */
static Entry *
defn_entry (const VM *vm, int defn)
{
  if ((unsigned) vm->block_end <= (unsigned) defn)
    bad_defn (vm, "Reference out of range");
  return vm->entries + defn;
}


/* Pre: where != NULL
   Record that the code for `defn' starts at `where'. */
static void
resolve (VM *vm, int defn, cell *where)
{
  Entry *entry = defn_entry (vm, defn);
  assert (where != NULL);
  assert (entry->address == NULL);
  {
    Unresolved *u = entry->refs;
    while (u != NULL)
      {
	Unresolved *v = u->next;
	u->ref[0].ptr = where;
	free (u);
	u = v;
      }
    entry->refs = NULL;
  }
  entry->address = where;
}


/* Pre: ref != NULL
   Record that the code word at `ref' should hold the starting address of 
   the defn that `entry' denotes.
   Return that address if known, else NULL. */
static const cell *
reference (Entry *entry, cell *ref)
{
  if (entry->address == NULL)
    {
      Unresolved *u = allot (sizeof *u);
      u->ref = ref;
      u->next = entry->refs;
      entry->refs = u;
    }
  return entry->address;
}


/* Pre: opcode is CALL or TAILCALL.
   Emit a call to the defn that `entry' denotes. */
static cell *
emit_call (VM *vm, int opcode, Entry *entry)
{
  reserve (vm, 1);
  {
    cell *after = vm->there;
    cell operand;
    operand.ptr = reference (entry, after - 1);
    really_emit1 (vm, opcode, operand);
    return after;
  }
}


/* Emit a branch to `target'. */
static void
emit_branch (VM *vm, cell *target)
{
  reserve (vm, 1);
  {
    int d = target - vm->there;
    if (2 <= d && d <= 9)	/* specialized short-branch instructions */
      really_emit (vm, BRANCH2 + d-2);
    else
      {
	cell operand;
	operand.ptr = target;
	really_emit1 (vm, BRANCH, operand);
      }
  }
}


/* Emit a 1-operand instruction, possibly using a version of the
   instruction customized to the particular operand value (if the
   operand's in the range [lo..hi]). */
static void
emit_immediate (VM *vm, int opcode, i32 operand,
		int custom_base_opcode, i32 lo, i32 hi)
{
  if (lo <= operand && operand <= hi)
    emit (vm, custom_base_opcode + operand - lo);
  else
    {
      cell rand;
      rand.i32 = operand;
      emit1 (vm, opcode, rand);
    }
}


/* Emit a DROP instruction. */
static void
emit_drop (VM *vm, int count)
{
  emit_immediate (vm, DROP, count, DROP1, 1, 4);
}


/* Restore the validity of max_width after a change to height or locals
   that might have increased it. */
static void
update_width (VM *vm)
{
  if (vm->height + vm->locals > vm->max_width)
    vm->max_width = vm->height + vm->locals;
}


/* Update the static stack height for an instruction with `using' inputs
   off the stack and `delta' overall change in stack height.  (The old
   stack state holds for *after* the instruction executes, and the new
   state for *before* it.) */
static void
update_stack (VM *vm, int using, int delta)
{
  vm->height -= delta;
  if (vm->height < using)
    bad_defn (vm, "Inaccurate stack effect declaration (more results "
	       "are yielded than declared)");
}


/* Update the static locals depth for an instruction that changes it by
   `delta'.  (Used the same way as update_stack.  We don't need a `using'
   parameter because underflow can't happen.) */
static void
update_locals (VM *vm, int delta)
{
  vm->locals -= delta;
}


/* Record that `code' is an address that may go on the return stack,
   with `popping' as the number of stack elements the callee pops off.
   Pre: This must be done right after the responsible instruction is
   compiled, so the static stack state will be right.  (Ugh...) */
static void
note_frame_map (VM *vm, cell *code, unsigned popping)
{
  if (vm->num_fms == vm->fm_size)
    {
      vm->fm_size *= 2;
      vm->fm = reallot (vm->fm, vm->fm_size * sizeof vm->fm[0]);
    }
  {
    Frame_map *m = vm->fm + vm->num_fms++;
    m->code = code;
    m->height = vm->height - popping;
    m->locals = vm->locals;
  }
}


const struct {
  int arg_count, delta;
} op_effect[] = {
#include "effect.inc"
};


/* Prepend `insn' to the current defn. */
static void
translate_insn (VM *vm, Insn insn)
{
  switch (insn.opcode)
    {
    case BEGIN:
      if (vm->nsp == vm->nesting)
	bad_defn (vm, "Badly nested control flow at BEGIN");
      {
	const Nest *nest = --(vm->nsp);
	int pending = nest->opcode;
	switch (pending)
	  {
	  case ELSE:
	  case THEN:
	    basic_block_boundary (vm);
	    if (nest->height != vm->height)
	      bad_defn (vm, "Stack effects don't match across branches");
	    emit_branch (vm, nest->addr);
	    update_stack (vm, 1, -1);
	    update_width (vm);
	    break;
	  case DROP: 
	    {
	      i32 locals = nest->operand;
	      emit_immediate (vm, GRAB, locals, GRAB1, 1, 4);
	      update_stack (vm, locals, -locals);
	      update_locals (vm, locals);
	      break;
	    }
	  default:
	    unreachable ();
	  }
	vm->returning = -1;
      }
      break;

    case ELSE: 
      basic_block_boundary (vm);
      {
	int n;
	cell *after = vm->there;
	Nest *nest = vm->nsp - 1;
	if (nest < vm->nesting || nest->opcode != THEN)
	  bad_defn (vm, "Badly nested control flow at ELSE");
	vm->at_jump_target = true;
	n = nest->returning;
	if (n == -1)
	  {
	    cell operand;
	    operand.ptr = nest->addr;
	    emit1 (vm, JUMP, operand);
	  }
	else
	  {			/* optimize out jumps to RETURN */
	    emit (vm, RETURN);
	    if (0 < n) emit_drop (vm, n);
	  }
	{
	  int old_height = nest->height;

	  nest->opcode    = ELSE;
	  nest->addr      = after;
	  nest->height    = vm->height;
	  nest->returning = vm->returning;

	  vm->height    = old_height;
	  vm->returning = n;
	}
	break;
      }

    case THEN:
      basic_block_boundary (vm);
      {
	Nest *nest = vm->nsp++;
	/* FIXME: this isn't really bad input, it's a hard limit: */
	if (vm->nesting + vm->ns_size <= nest)
	  bad_defn (vm, "Too many levels of control-flow nesting at THEN");
	nest->opcode    = THEN;
	nest->addr      = vm->there;
	nest->height    = vm->height;
	nest->returning = vm->returning;
	vm->at_jump_target = true;
	break;
      }

    case CALL: 
      basic_block_boundary (vm);
      {
	Entry *entry = defn_entry (vm, vm->defn_number + insn.operand);
	int n = vm->returning;
	cell *after;

	after = emit_call (vm, n == -1 ? CALL : TAILCALL, entry);
	if (0 < n) emit_drop (vm, n);

	update_stack (vm, entry->popping, entry->pushing - entry->popping);
	update_width (vm);
	note_frame_map (vm, after, entry->popping);
	vm->returning = -1;
	break;
      }

    case LOCAL: 
      if ((unsigned) vm->locals <= (unsigned) insn.operand)
	bad_defn (vm, "Reference to nonexistent local");
      emit_immediate (vm, LOCAL, insn.operand, LOCAL0, 0, 3);
      update_stack (vm, 0, 1);
      vm->returning = -1;
      break;

    case DROP:
      {
	int locals = insn.operand;
	if (locals < 0)
	  bad_defn (vm, "Can't DROP a negative amount");

	{
	  Nest *nest = vm->nsp++;
	  if (vm->nesting + vm->ns_size <= nest)
	    bad_defn (vm, "Too many levels of control-flow nesting at DROP");
	  nest->opcode = DROP;
	  nest->operand = locals;
	}

	emit_drop (vm, locals);
	update_locals (vm, -locals);
	update_width (vm);
	if (0 <= vm->returning)
	  vm->returning += locals;
	break;
      }

    case save:
      {
	cell *after = vm->there;
	emit (vm, save);
	update_stack (vm, 0, 1);
	note_frame_map (vm, after, 0);
	vm->returning = -1;
	break;
      }

    case PUSH:
      {
	emit_immediate (vm, PUSH, insn.operand, PUSH_1, -1, 4);
	update_stack (vm, 0, 1);
	update_width (vm);
	vm->returning = -1;
	break;
      }

    /* We list all the primitive cases explicitly here so that anything
       else causes an error */
#include "prims.inc"
      emit (vm, insn.opcode);
      update_stack (vm,
		    op_effect[insn.opcode].arg_count,
		    op_effect[insn.opcode].delta);
      update_width (vm);
      vm->returning = -1;
      break;

    default:
      bad_defn (vm, "Invalid opcode");
    }
}


/* FIXME: note that preconditions like the next two below hold for
   most of the other functions here...  When some version of these
   functions becomes part of the public API we'll have to figure out
   which preconditions we can avoid and which will turn into checked
   error conditions. */

/* Start compiling a new defn.
   Pre: we're not already in a defn. */
static void
start_defn (VM *vm)
{
  vm->height = vm->entries[vm->defn_number].pushing;
  vm->locals = 0;
  vm->max_width = vm->height;
  vm->returning = 0;
  vm->bb_end = vm->there;
  emit (vm, RETURN);
}


/* Finish compiling a defn. 
   Pre: we're currently in a defn. */
static void
finish_defn (VM *vm)
{
  if (vm->nsp != vm->nesting)
    bad_defn (vm, "Badly nested control flow: unclosed");

  if (vm->height != vm->entries [vm->defn_number].popping)
    bad_defn (vm, "Inaccurate stack effect declaration "
	       "(inferred %d arguments, not %d)",
	       vm->height, vm->entries [vm->defn_number].popping);

  basic_block_boundary (vm);

  /* + 1 is for any return address on the return stack */
  emit_lit (vm, vm->max_width + 1);

  resolve (vm, vm->defn_number, vm->there);

  if (0 == vm->defn_number)
    {				/* The main entry point */
      Entry *e = &vm->entries[vm->defn_number];
      /* We check for these errors here rather than in set_initial_pc
	 so that the error message will include the right defn number. */
      if (e->popping != 0)
	bad_defn (vm, "Entry point must take no argument");
      if (e->pushing != 1)
	bad_defn (vm, "Entry point must return a status value");
    }

  vm->defn_number++;
}


/* Set the initial program counter for `vm', either from its first
   defn (if the return stack is empty), or from after a `save'
   instruction (otherwise). */
static void
set_initial_pc (VM *vm)
{
  if (vm->rp == vm->stack + vm->stack_size - 1)
    {
      /* TODO: give a better error message if the object file has no code. */
      Entry *e = defn_entry (vm, 0);
      /* We start off with a tail-call through the stub to avoid
         duplicating that instruction's fuel- and stack-checking logic: */
      vm->pc = vm->stub;
      vm->stub[0].lbl = opcode_labels[TAILCALL];
      vm->stub[1].ptr = e->address;
    }
  else
    {
      if (!idel_development_enabled)
	die ("This input file has a pickled stack, which is an unsupported "
	     "still-unsafe feature");
      vm->pc = vm->rp++[0].ptr;
      /* TODO: make sure we're guaranteed pc[-1] is not a segfault */
      /* (Actually, do we really care if it stopped at a `save'?
	 Only for the sake of pushing -1...) */
      if (vm->pc[-1].lbl != opcode_labels[save])
	bad_input ("Restored program wasn't suspended at a `save' instruction");
      /* FIXME: check for stack overflow!  Or why not have the `save'
	 operation push this before pickling?  Well, that's no help in
	 general because in thawing out we'd still have to supply
	 values on the stack to whatever's pending... hm.  */
      vm->sp++[0].i32 = -1;
    }
}


/* Global flag controlling the above nonsense. */
int idel_development_enabled = false;


static void
set_initial_data_size (VM *vm)
{
  vm->data_size = word_align (vm->data_ptr);
}


/* Input */

/* Return the next insn from `or'. */
static Insn
pop_insn (OR *or)
{
  Insn insn;

  insn.opcode = pop_u8 (or);
  switch (insn.opcode)
    {
    case DROP: 
    case LOCAL:
      insn.operand = pop_u32 (or);
      break;
    case PUSH: 
    case CALL:
      insn.operand = pop_i32 (or);
      break;
    default:
      insn.operand = 0;
      break;
    }
  return insn;
}


/* Compile the next defn from `or'. */
static void
pop_defn (OR *or, VM *vm)
{
  start_defn (vm);
  while (or->ptr < or->limit)
    translate_insn (vm, pop_insn (or));
  finish_defn (vm);
}


/* Compile a block of defns from `or'. */
static void
pop_defns (OR *or, VM *vm)
{
  u32 n = pop_u32 (or);
  u32 i;
  int j;

  start_defns (vm, n);
  for (j = vm->block_start; j < vm->block_end; ++j)
    {
      u32 stack_effect = pop_u32 (or);
      uninterleave_bits (&vm->entries[j].popping, 
			 &vm->entries[j].pushing, 
			 stack_effect);
    }

  for (i = 0; i < n; ++i)
    {
      i8 *L = start_subfile_in (or);
      pop_defn (or, vm);
      end_subfile_in (or, L);
    }
  if (or->limit != or->ptr)
    bad_defn (vm, "Code left over in block");
  finish_defns (vm);
}


/* Compile a block of literal ints from `or'. */
static void
pop_ints (OR *or, VM *vm)
{
  while (or->ptr < or->limit)
    emit_data_int (vm, pop_i32 (or));
}


/* Compile a block of literal bytes from `or'. */
static void
pop_bytes (OR *or, VM *vm)
{
  while (or->ptr < or->limit)
    emit_data_byte (vm, (i8) pop_u8 (or));
}


/* Compile a block of zero bytes from `or'. */
static void
pop_zeroes (OR *or, VM *vm)
{
  i32 count = pop_i32 (or);
  if (or->limit != or->ptr)
    bad_input ("Overlong Zeroes block");

  if (vm->data_size < vm->data_ptr + count ||
      vm->data_ptr + count < vm->data_ptr)
    bad_input ("Data count out of range in Zeroes");

  vm->data_ptr += count;
}


/* Return a new OR containing the bytes between [start..end)
   (by reference, not by copy). */
static OR *
or_subfile (i8 *start, i8 *end)
{
  OR *or = allot (sizeof *or);
  or->buffer = start;
  or->ptr = start;
  or->limit = end;
  return or;
}


/* Add to vm's data and return stacks the next stack frame from `or'. */
static void
pop_frame (OR *or, VM *vm)
{
  u32 fm_index = pop_u32 (or);
  if ((u32)(vm->num_fms) <= fm_index)
    bad_input ("Nonsensical frame label");
  {
    int i;
    const Frame_map *m = vm->fm + fm_index;
    if (vm->rp - vm->sp < m->locals + m->height + 1)
      die ("Out of stack space");

    for (i = m->locals - 1; 0 <= i; --i)
      (--vm->rp)[0].i32 = pop_i32 (or);
    (--vm->rp)[0].ptr = m->code;
    for (i = m->height - 1; 0 <= i; --i)
      vm->sp++[0].i32 = pop_i32 (or);
  }
}


/* Fill vm's stacks from a stack section in `or'. */
static void
pop_stack (OR *or, VM *vm)
{
  if (vm->rp != vm->stack + vm->stack_size - 1)
    bad_input ("Object file with more than one stack section");

  while (or->ptr < or->limit)
    pop_frame (or, vm);

  /* FIXME: need to check that the bottommost frame yields exactly one
   value.  Do that by checking the signature of the defn that the
   return address of the bottommost frame is in.  To do that, modify
   pop_frame to return the frame map or something.  Then how do we get
   from return address to defn?  Ugh.  Actually, you need to work out
   exactly what a valid pickled stack can look like, before writing
   any more code.  Otherwise you'll get badly embarrassed. 
   
   So we need two extra pieces of info in the frame map: how many
   return values we're expecting at this return point, and how many
   values we will return to our caller.  But couldn't tail calls
   produce situations with stack values belonging to nobody?  I guess
   we have to calculate how many of those there will be, by comparing
   the signatures of caller and returner, as we pop each frame. 

   Also the stack pickling needs to be fixed in the same way.  Whee.
   */
}


/* Load the object file `or' into `vm'. */
void
vm_load (VM *vm, OR *or)
{
  pop_header (or);
  while (or->ptr < or->limit)
    {
      i8 *p = or->ptr;
      int tag = pop_tag (or);
      i8 *L = start_subfile_in (or);
      switch (tag)
	{
	case tag_defns: 
	  /* TODO: remember multiple DEFNS blocks.  FIXME: until then,
	     we should raise an error if there are > 1 of them */
	  vm->obj_code = or_subfile (p, or->limit); 
	  pop_defns (or, vm); 
	  break;
	case tag_bytes: 
	  pop_bytes (or, vm); 
	  break;
	case tag_ints: 
	  pop_ints (or, vm); 
	  break;
	case tag_zeroes: 
	  pop_zeroes (or, vm); 
	  break;
	case tag_stack:
	  pop_stack (or, vm);
	  break;
	default: 
	  bad_input ("Unknown block tag");
	}
      end_subfile_in (or, L);
    }

  set_initial_data_size (vm);
  set_initial_pc (vm);
}
