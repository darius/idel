/*
 * Virtual machine interpreter.
 * Copyright (C) 2001-2002 Darius Bacon
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "idel_private.h"


/* Table mapping Idel opcodes to internal labels. */
const cplabel *opcode_labels = NULL;


/* Verify that e is a valid Entry*. */
static void
entry_check (const Entry *e)
{
  assert (e != NULL);
  assert (0 <= e->popping);
  assert (0 <= e->pushing);
  if (e->address != NULL)
    assert (e->refs == NULL);
  /* TODO: check that address & refs point to safe & valid stuff */
}


/* Verify that vm is a valid VM*. */
void
vm_check (const VM *vm)
{
  int i;

  assert (vm != NULL);

  assert (0 < vm->stack_size);
  assert (vm->stack != NULL);
  assert (vm->stack <= vm->sp && vm->sp <= vm->rp && 
	  vm->rp <= vm->stack + vm->stack_size - 1);
  assert (vm->stack[vm->stack_size - 1].ptr[0].lbl == opcode_labels[HALT]);
  /* TODO: frames_check() */
  
  assert (0 < vm->num_chunks && vm->num_chunks <= vm->chunks_size);
  assert (vm->chunks != NULL);
  for (i = 0; i < vm->num_chunks; ++i)
    assert (vm->chunks[i] != NULL);

  assert (vm->pc != NULL);
  /* TODO: check that it points into the code space or a stub */

  assert (0 < vm->data_size);
  assert (0 == vm->data_size % word_size); /* data ends on a word boundary */
  assert (vm->data != NULL);
  assert (0 <= vm->data_ptr && vm->data_ptr <= vm->data_size);

  assert (false == vm->profiling || true == vm->profiling);
  assert (0 <= vm->tallies_size);
  assert (0 == vm->tallies_size || vm->tallies != NULL);
  assert (0 <= vm->num_tallies && vm->num_tallies <= vm->tallies_size);
  /* TODO: check that each tally entry is reasonable */

  assert (0 < vm->ns_size);
  assert (vm->nesting != NULL);
  assert (vm->nesting <= vm->nsp && vm->nsp <= vm->nesting + vm->ns_size);
  /* TODO: nest_check() */

  assert (0 <= vm->height);
  assert (0 <= vm->locals);
  assert (vm->height + vm->locals <= vm->max_width);

  assert (-1 <= vm->returning);
  assert (false == vm->at_jump_target || true == vm->at_jump_target);

  assert (0 <= vm->block_start && vm->block_start <= vm->block_end);
  assert (vm->block_start <= vm->defn_number && 
	  vm->defn_number <= vm->block_end);

  assert (0 <= vm->entries_size);
  assert (vm->entries != NULL);
  for (i = 0; i < vm->block_end; ++i)
    entry_check (vm->entries + i);

  assert (vm->there != NULL);
  assert (vm->chunks[vm->num_chunks - 1] <= vm->there &&
	  vm->there <= vm->chunks[vm->num_chunks - 1] + chunk_size);
}


/* Return a new VM (but with uninitialized codegen fields). */
VM *
vm_sort_of_make (int stack_size, int data_size)
{
  static cell halt_stub;

  if (stack_size <= 0 || data_size <= 0 || data_size % bytes_per_word != 0)
    die ("Bad argument to vm_make()");
  {
    VM *vm = allot (sizeof (*vm));
    vm->stack_size = stack_size;
    vm->stack = allot (stack_size * sizeof vm->stack[0]);
    vm->sp = vm->stack;
    vm->rp = vm->stack + stack_size;

    vm->num_chunks = 1;
    vm->chunks_size = 1;
    vm->chunks = allot (vm->chunks_size * sizeof vm->chunks[0]);
    vm->chunks[0] = allot (chunk_size * sizeof vm->chunks[0][0]);
    vm->data_size = data_size;
    vm->data = allot (data_size * sizeof vm->data[0]);
    memset (vm->data, 0, data_size * sizeof vm->data[0]);

    vm->obj_code = NULL;

    vm->fm_size = 64;
    vm->num_fms = 0;
    vm->fm = allot (vm->fm_size * sizeof vm->fm[0]);

    /* set up the label table */
    if (opcode_labels == NULL)
      {
	vm_run (NULL, 0);
	halt_stub.lbl = opcode_labels[HALT];
      }

    /* Push a sentinel on the return stack. */
    (--vm->rp)[0].ptr = &halt_stub;

    vm->pc = &halt_stub;

    return vm;
  }
}


/* Destroy a vm. */
void
vm_unmake (VM *vm)
{
  /* FIXME: free codegen vars & obj_code */
  int i;

  free (vm->fm);
  free (vm->data);
  for (i = 0; i < vm->num_chunks; ++i)
    free (vm->chunks[i]);
  free (vm->chunks);
  free (vm->stack);
  free (vm);
}


/* State saving */

/* Push onto `ow' the contents of `or'. */
static void
copy_subfile (OW *ow, const OR *or)
{
  int n = or->limit - or->ptr;
  while (ow->ptr - ow->buffer < n)
    ow_grow (ow);
  ow->ptr -= n;
  memcpy (ow->ptr, or->ptr, n);
}


/* Push onto `ow' the defns that make up vm's code. */
static void
push_defns (OW *ow, const VM *vm)
{
  copy_subfile (ow, vm->obj_code);
}


/* Push onto `ow' a data section with the contents of vm's data space. */
static void
push_data (OW *ow, const VM *vm)
{
  int g = vm->data_size - word_size;
  for (; 0 <= g; g -= word_size)
    if (0 != *(i32 *)(vm->data + g))
      break;

  if (0 <= g)
    {
      int mark = start_subfile (ow);

      for (; 0 <= g; g -= word_size)
	push_i32 (ow, *(i32 *)(vm->data + g));

      end_subfile (ow, mark);
      push_u32 (ow, tag_ints);
    }
}


/* Return vm's frame map for the return address `code'. */
static const Frame_map *
frame_map_find (const VM *vm, const cell *code)
{
  int i;			/* TODO: binary search or something */
  for (i = 0; i < vm->num_fms; ++i)
    if (vm->fm[i].code == code)
      return vm->fm + i;

  die ("Unmapped return pointer");
  return NULL;
}


/* Push onto `ow' the stack frame at (sp,rp) described by `m'.
   Pre: sp, rp, and m designate a genuine stack frame in vm. */
static void
push_frame (OW *ow, const VM *vm, 
	    const Frame_map *m, const cell *sp, const cell *rp)
{
  int i;
  for (i = 0; i < m->height; ++i)
    push_i32 (ow, sp[-1 - i].i32);
  for (i = 0; i < m->locals; ++i)
    push_i32 (ow, rp[i].i32);
  push_u32 (ow, m - vm->fm);
}


/* Push onto `ow' all of vm's stack frames from (sp,rp) back to the 
   bottom of the stacks (except for the sentinel at the very bottom).
   Pre: (sp, rp) designates a genuine stack frame in vm. */
static void
push_frames (OW *ow, const VM *vm, const cell *sp, const cell *rp)
{
  if (rp < vm->stack + vm->stack_size - 1)
    {
      const Frame_map *m = frame_map_find (vm, rp[0].ptr);
      push_frames (ow, vm, sp - m->height, rp + (m->locals + 1));
      push_frame (ow, vm, m, sp, rp);
    }
}


/* Push onto `ow' all of vm's stack frames.
   Pre: vm's program counter is at a program point with a frame map. */
static void
push_stack (OW *ow, const VM *vm)
{
  int mark = start_subfile (ow);

  push_frames (ow, vm, vm->sp, vm->rp);

  end_subfile (ow, mark);
  push_u32 (ow, tag_stack);
}


/* Push onto `ow' the data needed to recreate vm's state.
   Pre: vm's program counter is at a program point with a frame map. */
static void
vm_freeze (OW *ow, const VM *vm)
{
  push_data (ow, vm);
  push_stack (ow, vm);
  push_defns (ow, vm);
  push_header (ow, NULL);
}


/* Write the contents of `ow' to `filename'. */
static void
ow_write_file (const char *filename, OW *ow)
{
  FILE *out = open_file (filename, "wb");
  ow_write (out, ow);
  fclose (out);
}


/* Save vm's state in object-file format to the file "frozen". */
static void
save_vm_state (const VM *vm)
{
  OW *ow = ow_make ();
  vm_freeze (ow, vm);
  ow_write_file ("frozen", ow);
  ow_unmake (ow);
}


/* The inner interpreter */

enum { 
  tracing = false		/* set to true to print instructions executed */
};


/* Print a data stack to stdout. */
static void
print_stack (const cell *stack, const cell *sp)
{
  int i;
  for (i = 0; stack + i < sp; ++i)
    printf (i32_fmt " ", stack[i].i32);
  printf ("\n");
}


/* Print current instruction and stack (for tracing) */
/* TODO: show locals too */
static void
trace_op (const cell *pc, const cell *stack, const cell *sp)
{
  printf ("%-30s | ", format_op_at (pc));
  print_stack (stack, sp);
}


/* If vm is non-NULL:
     Execute vm's code, starting at its pc, until completion, error, or
     exhaustion of fuel.  Return a status code.
   else:
     Initialize the opcode_labels table.
   (Blame GNU C's scope rules for this ugly disjunctive specification...) */
int
vm_run (VM *vm, int fuel)
{
  static const cplabel table[] = {
#include "labels.inc"
  };

  if (vm == NULL)
    {
      opcode_labels = table;
      return 0;
    }
  else
    {
      const cell *pc = vm->pc;
      cell *sp = vm->sp - 1, *rp = vm->rp;
      cell *stack = vm->stack;
      i8 *data = vm->data;
      unsigned data_size = vm->data_size;

      if (paranoid) vm_check (vm);
      if (0) vm_dump_code (vm);

      /* Now run it */
#include "interp.inc"
    }
}
