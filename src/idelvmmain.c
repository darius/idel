/*
 * Load and run an Idel VM, from the shell.
 * Copyright (C) 2001-2002 Darius Bacon
 *
 * This is also an example of how to use the API.
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "idel.h"


/* A fail-stop fopen(), treating a filename of "-" as stdin/stdout. */
static FILE *
open_file (const char *filename, const char *mode)
{
  if (strcmp (filename, "-") == 0) 
    {
      assert (mode[0] == 'r' || mode[0] == 'w');
      return mode[0] == 'w' ? stdout : stdin;
    } 
  else
    {
      FILE *result = fopen (filename, mode);
      if (!result)
	idel_die ("%s: %s", filename, strerror (errno));
      return result;
    }
}


static int
int_value (const char *s)
{
  int n;
  char *endptr;

  errno = 0;
  n = strtol (s, &endptr, 10);
  if (endptr[strspn (endptr, " \t\n")] != '\0' || errno == ERANGE)
    idel_die ("Not a 32-bit integer: %s", s);

  return n;
}


static void
show_version (void)
{
  printf ("idelvm version %s\n", idel_version_string);
  exit (0);
}


static void
show_help (int exit_status)
{
  printf (
"Usage: %s [option]... [object-file]\n", idel_program_name);
  printf (
"where the legal options are:\n"
"  -data n\n"
"  -fuel n\n"
"  -help\n"
"  -stack n\n"
"  -version\n"
"Report bugs to darius@wry.me.\n"
);
  exit (exit_status);
}


const char *idel_program_name;

/* Load and run an object file in a fresh VM. */
int
main (int argc, char **argv)
{
  int stack_size = 200;		/* defaults */
  int data_size = 64 * 1024;
  int fuel = 100000000;
  int profiling = 0;

  idel_OR *or;
  FILE *in = stdin;

  idel_program_name = argv[0];

  /* Parse command-line options */
  for (;;)
    {
      if (1 < argc)
	{
	  if (0 == strcmp ("-version", argv[1]))
	    show_version ();
	  if (0 == strcmp ("-help", argv[1]))
	    show_help (0);
	  if (0 == strcmp ("-development", argv[1]))
	    {
	      idel_development_enabled = 1;
	      --argc, ++argv;
	    }
	  else if (0 == strcmp ("-profile", argv[1]))
	    {
	      profiling = 1;
	      --argc, ++argv;
	    }
	}
      if (2 < argc)
	{
	  if (0 == strcmp ("-stack", argv[1]))
	    stack_size = int_value (argv[2]);
	  else if (0 == strcmp ("-data", argv[1]))
	    data_size = int_value (argv[2]);
	  else if (0 == strcmp ("-fuel", argv[1]))
	    {
	      fuel = int_value (argv[2]);
	      if (fuel < 0)
		idel_die ("Negative fuel: %d", fuel);
	    }  
	  else
	    break;
	  argc -= 2, argv += 2;	  
	}
      else
	break;
    }

  /* Read the object file */
  if (2 < argc)
    show_help (128);
  else if (2 == argc)
    in = open_file (argv[1], "rb");
  or = idel_read_program (in);
  fclose (in);

  /* Run the program */
  {
    int status;
    idel_VM *vm = idel_vm_make (stack_size, data_size, profiling);
    idel_vm_load (vm, or);
    status = idel_vm_run (vm, fuel);
    if (profiling)
      idel_vm_dump_profile (vm, stderr);
    return 0x7f & status;
  }
}
