/*
 * The Idel virtual machine -- public interface.
 * Copyright (C) 2001-2002 Darius Bacon
 */

#ifndef _IDEL
#define _IDEL

enum { idel_major_version = 0 };
enum { idel_minor_version = 1 };

#include "idel_porting.h"
#include <stdio.h>

#ifdef __cplusplus
#  define EXTERN extern "C"
#else
#  define EXTERN extern
#endif

enum { idel_bytes_per_word = 4 };

/* This stuff is for panicking with an error message; it will go away
   later and be replaced by longjmp-based exceptions. */
EXTERN const char *idel_program_name;
EXTERN void        idel_die (const char *message, ...);

/* Similarly transient; stack-pickling needs to be disabled until it's
   done right. */
EXTERN int idel_development_enabled;

/* Object file input/output */

enum {
  idel_tag_defns  = 'X',
  idel_tag_bytes  = 'B',
  idel_tag_ints   = 'I',
  idel_tag_zeroes = 'Z',
  idel_tag_stack  = 'S'
};

typedef struct idel_OW idel_OW;	/* Object Writer */

EXTERN idel_OW *idel_ow_make (void);
EXTERN void     idel_ow_unmake (idel_OW *ow);

EXTERN void     idel_push_u8 (idel_OW *ow, idel_u8 u);
EXTERN void     idel_push_u32 (idel_OW *ow, idel_u32 u);
EXTERN void     idel_push_i32 (idel_OW *ow, idel_i32 i);
EXTERN void     idel_push_tag (idel_OW *ow, idel_u32 u);
EXTERN void     idel_push_header (idel_OW *ow, const char *comment);

EXTERN int      idel_start_subfile (idel_OW *ow);
EXTERN void     idel_end_subfile (idel_OW *ow, int mark);

EXTERN void     idel_ow_write (FILE *fp, idel_OW *ow);


typedef struct idel_OR idel_OR;	/* Object Reader */

EXTERN idel_u8  idel_pop_u8 (idel_OR *or);
EXTERN idel_u32 idel_pop_u32 (idel_OR *or);
EXTERN idel_i32 idel_pop_i32 (idel_OR *or);
EXTERN idel_u32 idel_pop_tag (idel_OR *or);
EXTERN void     idel_pop_header (idel_OR *or);

EXTERN idel_i8 *idel_start_subfile_in (idel_OR *or);
EXTERN void     idel_end_subfile_in (idel_OR *or, idel_i8 *old_limit);

EXTERN idel_OR *idel_read_program (FILE *in);


/* Virtual machine structure */

typedef struct idel_VM idel_VM;

EXTERN idel_VM *idel_vm_make (int stack_size, int data_size, int profiling);
EXTERN void     idel_vm_unmake (idel_VM *vm);
EXTERN void     idel_vm_check (const idel_VM *vm);

EXTERN void     idel_vm_load (idel_VM *vm, idel_OR *or);
EXTERN int      idel_vm_run (idel_VM *vm, int fuel);

EXTERN void     idel_vm_dump_profile (idel_VM *vm, FILE *out);

#endif /* _IDEL */
