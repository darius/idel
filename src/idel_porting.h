/*
 * System-dependent declarations.
 * Copyright (C) 2001-2002 Darius Bacon
 */
#ifndef _IDEL_PORTING
#define _IDEL_PORTING

/* If you use a type with a larger word size here (e.g. 64 bits), code
   will break.  Also, on a 16-bit machine you'll need to change
   idel_i32 to be a long int, etc. */

typedef          int  idel_i32;	/* 32-bit, signed */
typedef unsigned int  idel_u32;	/* 32-bit, unsigned */
typedef   signed char idel_i8;	/*  8-bit, signed */
typedef unsigned char idel_u8;	/*  8-bit, unsigned */

/* printf formats */
#define idel_i32_fmt "%d"
#define idel_u32_fmt "%u"
#define idel_x32_fmt "%x"


/* The effect of >> on a negative number is not specified by the C
   standard.  We need it to round to -infinity, so we encapsulate that in
   asr: */

#define idel_ASR(x, y)    ( (x) >> (y) )


/* The direction of rounding in division of signed integers is
   implementation-specified.  These should round towards 0: */

#define idel_IDIV0(x, y)   ( (x) / (y) )
#define idel_IMOD0(x, y)   ( (x) % (y) )

/* There are other unspecified behaviors like arithmetic overflow, but
   I haven't got around to wrappers for them yet... */


#ifdef WORDS_BIGENDIAN
enum { idel_endianness = 0 };
#else
enum { idel_endianness = 3 };
#endif

#endif /* _IDEL_PORTING */
