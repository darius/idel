/*
 * Bitty stuff.
 * Copyright (C) 2001-2002 Darius Bacon
 */

#include "idel_private.h"


/* Return the number composed of each bit of X and Y, taken alternately.
   For example, if Y=0110 (binary) and X=1010, then the result is

        1 0 1 0
       0 1 1 0

   or 01101100.  The method used here is much faster than the obvious
   loop.  (The idea is to swap parts of the word in parallel.  Here's
   how it works when X=0x0000, Y=0xffff, giving the successive values
   of A:
       
       ffff0000  ==>  swap middle bytes of longword
       ff00ff00  ==>  swap middle nybbles of both 16-bit words
       f0f0f0f0  ==>  swap middle half-nybbles of all bytes
       cccccccc  ==>  swap middle bits of all nybbles
       aaaaaaaa) 
*/
u32
interleave_bits (int x, int y)
{
  u32 a = ((u32)x & 0xffff) | ((u32)y << 16);
  a = (a & 0xff0000ff) | ((a & 0x0000ff00) << 8) | ((a & 0x00ff0000) >> 8);
  a = (a & 0xf00ff00f) | ((a & 0x00f000f0) << 4) | ((a & 0x0f000f00) >> 4);
  a = (a & 0xc3c3c3c3) | ((a & 0x0c0c0c0c) << 2) | ((a & 0x30303030) >> 2);
  a = (a & 0x99999999) | ((a & 0x22222222) << 1) | ((a & 0x44444444) >> 1);
  return a;
}


/* Post: interleave_bits(*x, *y) == a */
void 
uninterleave_bits (int *x, int *y, u32 a)
{
  a = (a & 0x99999999) | ((a & 0x22222222) << 1) | ((a & 0x44444444) >> 1);
  a = (a & 0xc3c3c3c3) | ((a & 0x0c0c0c0c) << 2) | ((a & 0x30303030) >> 2);
  a = (a & 0xf00ff00f) | ((a & 0x00f000f0) << 4) | ((a & 0x0f000f00) >> 4);
  a = (a & 0xff0000ff) | ((a & 0x0000ff00) << 8) | ((a & 0x00ff0000) >> 8);
  *y = a >> 16;
  *x = a & 0xffff;
}
