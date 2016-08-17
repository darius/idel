/*
 * Stuff that's internal to the VM implementation.
 * Copyright (C) 2001-2002 Darius Bacon
 */
#ifndef _IDEL_PRIVATE
#define _IDEL_PRIVATE

#include "idel.h"

/* Define aliases without all the prefixes.  Yargh! */
#include "public_names.inc"
#include "private_names.inc"

enum {
#include "enum.inc"
};

/* Set to true for `unreasonable' sanity checking. */
enum { paranoid = 0 };

/* Initial memory allocations for a vm. */
enum { chunk_size = 1024 }; /* words, not bytes.  ugh, inconsistent. */

/* Misc utilities. */
#include <assert.h>
#define unreachable()  assert(0)

#define NELEMS(array)  ( sizeof(array) / sizeof((array)[0]) )

/* The first word boundary at or after n */
#define word_align(n)  (((n) + word_size - 1) & ~(word_size - 1))

typedef enum { false = 0, true = 1 } bool;

EXTERN void  *allot (size_t size);
EXTERN void  *reallot (void *p, size_t size);
EXTERN void   read_file (char **result, int *size, FILE *in);
EXTERN FILE  *open_file (const char *filename, const char *mode);


enum { word_bits = 32 };
enum { word_size = (int) sizeof (idel_u32) };

typedef const void *cplabel;	/* A GNU C label pointer; not used unless
				   we're compiled by GCC. */
typedef union cell {
  idel_i32 i32;
  cplabel lbl;
  const union cell *ptr;
} cell;				/* FIXME: come up with a better name */


struct idel_OW {		/* Object Writer */
  /* TODO: better to make these be u8's? */
  idel_i8 *buffer;
  idel_i8 *top;			/* end of buffer */
  idel_i8 *ptr;			/* first byte of data in buffer */
};

struct idel_OR {		/* Object Reader */
  idel_i8 *buffer;
  idel_i8 *ptr;			/* current scan position in buffer */
  idel_i8 *limit;		/* end of buffer */
};

EXTERN u32    interleave_bits (int x, int y);
EXTERN void   uninterleave_bits (int *x, int *y, u32 a);

EXTERN int    i32_encoded_size (i32 i);
EXTERN int    u32_encoded_size (u32 u);

EXTERN void   ow_grow (OW *ow);

/* 
A vm has a pair of stacks, a program counter, a data area, compiled
code, uncompiled code, and a bunch of code-generator state variables.
(It'd make sense to split out the code-generator state into a separate
structure.)

The uncompiled code is retained to make it easier to serialize the
full vm state.  (It's copied verbatim.)

Compiled code is a linear sequence of machine words; each instruction
is 1 or 2 words long, with the first one being the `opcode'.  How the
opcode is represented varies: if we're using Gnu C, it points directly
to the code implementing it (using label pointers); otherwise it's a
small integer used as a switch case label.  (Label pointers are much
faster, by my measurements.)  Instructions implicitly follow each
other in ascending order in memory.

Besides the instructions inherited from the Idel object format, there
are instructions for control flow and stack management:
    procedures: CALL, TAILCALL, RETURN 
    control flow: JUMP, BRANCH
    stack: GRAB
    misc: HALT
The target of JUMP and BRANCH instructions is always logically
forward; the only way to do backward jumps is with TAILCALL.

The code for a defn starts with a word containing the maximum stack
depth the defn may require (including both data and return stack).
That word is pointed to by CALL/TAILCALL instructions and the vm's
entries array.

Code is stored in dynamically allocated chunks; a defn may get split
across multiple chunks, with JUMP instructions inserted automatically
where needed.  (This means logically forward jumps may be physically
backward.)  Since object files list instructions in reverse order,
chunks get filled from top to bottom in memory.  This also implies
that when we emit a jump, its target is always already resolved.
Calls might not be resolved immediately, on the other hand, so we have
to remember them and backpatch later.

During code generation we need to track what the runtime stack frame
will look like, for three reasons:
    - to check the legality of the input code
    - to compute the max stack usage of the defn
    - to remember the frame map at each return point, for state-saving.
For us to be able to compute this, the entries table must have the
stack effect of each defn called, even the ones not compiled yet.

Finally, there's another compile-time stack to track the nesting level
of the Idel object code and flatten it out into linear compiled code.

The return stack is never empty: there's always a sentinel entry at
the bottom pointing to a HALT instruction.  */

EXTERN const cplabel *opcode_labels;	
                                /* Table giving label ptr for each opcode */

/* (some of the Nest fields are only filled for certain opcodes) */
typedef struct Nest {	      /* State of a nested block in codegen */
  u8 opcode;			/* the instruction that opened this block */
  i32 operand;			/* its operand field, if any */
  cell *addr;			/* ptr to the instruction after this block */
  int height;			/* stack height at addr */
  int returning;		/* vm's value of `returning' at addr */
} Nest;

typedef struct Unresolved {   /* An unresolved reference to a defn */
  cell *ref;			/* a place the resolved address will go */
  struct Unresolved *next;	/* linked list pointer */
} Unresolved;

typedef struct Entry {	      /* Descriptor of a defn */
  const cell *address;		/* starting address; is NULL when unresolved */
  Unresolved *refs;		/* references to this defn */
  int popping, pushing;		/* stack effect of this defn */
} Entry;

typedef struct Frame_map {    /* Maps return address to stack frame layout */
  const cell *code;		/* return address */
  int height;			/* stack height */
  int locals;			/* depth of locals */
} Frame_map;

typedef struct Tally {	      /* Execution tally for a basic block */
  unsigned long count;		/* how many times executed */
  int defn;			/* index of the defn the basic block is in */
  const cell *start;		/* pointer to the first instruction */
  const cell *end;		/* pointer after the last instruction */
} Tally;

struct VM {		      /* The state of a virtual machine */
  int stack_size;		/* count of slots allocated */
  cell *stack;			/* space for both stacks */
  cell *sp;			/* data stack pointer; grows upwards */
  cell *rp;			/* return stack pointer; grows downwards */

  int chunks_size;		/* count of chunk pointers allotted */
  int num_chunks;		/* count of code chunks in use */
  cell **chunks;		/* one for each chunk */
  const cell *pc;		/* program counter */
  cell stub[2];			/* execution jump-off point */

  int data_size;		/* space allotted for data */
  i8 *data;			/* data segment of the interpreted program */

  OR *obj_code;  		/* uncompiled code block */

  int fm_size;			/* count of frame-map entries allocated */
  int num_fms;			/* count of entries in use */
  Frame_map *fm;		/* entries */

  bool profiling;		/* true iff we're to collect profiling info */
  int tallies_size;		/* count of profile tallies allocated */
  int num_tallies;		/* count of tallies in use */
  Tally *tallies;		/* the tally table */

  int ns_size;			/* nesting stack size */
  Nest *nsp;			/* nesting stack pointer; grows upwards */
  Nest *nesting;		/* nesting stack */

  int height;			/* current static height of the stack */
  int locals;			/* current static depth of locals */
  int max_width;		/* max value of height+locals seen so far 
				   in the current defn */
  int returning;		/* if the code starting at `there' has
                                   the effect of ``DROP n; RETURN'', then
                                   returning==n, else returning==-1. */
  bool at_jump_target;		/* true iff the code at `there' will
                                   be the target of a jump */
  cell *bb_end;			/* points to the instruction after the
				   end of the current basic block */

  int defn_number;		/* # of current defn */
  int block_start;		/* # of defn starting current defn-block */
  int block_end;		/* # of first defn after current block */
  int entries_size;		/* count of entry pointers allotted */
  Entry *entries;		/* an entry for each defn up to block_end */

  cell *there;			/* current codegen cursor */
  int data_ptr;			/* current data-space cursor */
};

EXTERN void   vm_dump_code (const VM *vm);
EXTERN void   vm_dump_stacks (const VM *vm);

EXTERN int    operand_count (int opcode);
EXTERN int    lookup_op (cplabel internal_opcode);
EXTERN const char *format_op_at (const cell *pc);
EXTERN const char *format_opcode (const i32 opcode);

EXTERN VM    *vm_sort_of_make (int stack_size, int data_size);

#endif /* _IDEL_PRIVATE */
