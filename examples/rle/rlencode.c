/*
  Run-length encoding.
  Stolen from _Software Tools_ page 44.

  FIXME: check error return codes on I/O
 */

#include <stdio.h>


enum {
  MAXCHUNK = 255,
  RCODE = 0,
  THRESH = 5
};


static char buf[MAXCHUNK];


static void
putbuf (FILE *out, int n)
{
  if (n > 0)
    {
      putc (n, out);
      fwrite (buf, 1, n, out);
    }
}


static void
encode (FILE *out, FILE *in)
{
  int c, lastc;
  int nrep, nsave;

  nsave = 0;
  for (lastc = getc (in); lastc != EOF; lastc = c)
    {
      for (nrep = 1; (c = getc (in)) == lastc; ++nrep)
	if (nrep >= MAXCHUNK)
	  break;

      if (nrep < THRESH)
	for (; nrep > 0; --nrep)
	  {
	    buf[nsave++] = lastc;
	    if (nsave >= MAXCHUNK)
	      {
		putbuf (out, nsave);
		nsave = 0;
	      }
	  }
      else
	{
	  putbuf (out, nsave);
	  nsave = 0;
	  putc (RCODE, out);
	  putc (lastc, out);
	  putc (nrep, out);
	}
    }
  putbuf (out, nsave);
}


int 
main (void)
{
  encode (stdout, stdin);

  return 0;
}
