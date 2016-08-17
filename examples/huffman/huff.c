#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* Usual utilities */

static const char *program_name;


/* Complain and terminate. */
void
die (const char *message, ...)
{
  va_list args;

  fprintf (stderr, "%s: ", program_name);
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  fprintf(stderr, "\n");

  exit (1);
}


/* A safe malloc(). */
void *
allot (size_t size)
{
  void *p = malloc (size);
  if (p == NULL && size != 0)
    die ("%s", strerror (errno));
  return p;
}


/* A safe fopen(). */
FILE *
open_file (const char *filename, const char *mode)
{
  FILE *result = fopen (filename, mode);
  if (!result)
    die ("%s: %s", filename, strerror (errno));
  return result;
}


/* A safe fclose(). */
void
close_file (FILE *fp)
{
  if (0 != fclose (fp))
    die ("Couldn't close file: %s", strerror (errno));
}


/* Bit I/O */

static int nbits = 0;
static int obuf = 0;


static void
put_byte (FILE *out, int b)
{
  fprintf (out, "\\x%02x", b);
}


static void
put_bit (FILE *out, int bit)
{
  obuf = ((bit << 7) | (obuf >> 1));
  ++nbits;
  if (nbits % 8 == 0)
    put_byte (out, obuf);
}


static void
flush_bits (FILE *out)
{
  if (nbits % 8 != 0)
    put_byte (out, obuf >> (8 - nbits % 8));
}


/* Huffman tree nodes */

enum { alphabet_size = 256 };


typedef struct Node *Node;
struct Node {
  int weight;
  int leaf;
  Node kid0, kid1;		/* talk about your mixed metaphors */
};


static void
node_check (Node n)
{
  assert (n != NULL);
  assert (0 <= n->weight);
  if (n->leaf < 0)
    {
      node_check (n->kid0);
      node_check (n->kid1);
    }
  else
    assert (n->kid0 == NULL && n->kid1 == NULL && n->leaf < alphabet_size);
}


static int
is_leaf (Node n)
{
  return 0 <= n->leaf;
}


static Node
leaf_make (int weight, int leaf)
{
  Node n = allot (sizeof *n);
  n->weight = weight;
  n->leaf = leaf;
  n->kid0 = n->kid1 = NULL;
  return n;
}


static Node
branch_make (int weight, Node kid0, Node kid1)
{
  Node n = allot (sizeof *n);
  n->weight = weight;
  n->leaf = -1;
  n->kid0 = kid0;
  n->kid1 = kid1;
  return n;
}


Node nodes[alphabet_size];


/* Pre: 0 <= i && i < alphabet_size */
static void
insert (int i)
{
  Node n = nodes[i];
  for (; i < alphabet_size - 1; ++i)
    if (nodes[i+1]->weight <= n->weight)
      nodes[i] = nodes[i+1];
    else
      break;
  nodes[i] = n;
}


/* Pre: 0 <= i && i+1 < alphabet_size */
static void
combine_pair (int i)
{
  Node kid0 = nodes[i];
  Node kid1 = nodes[i+1];
  nodes[i+1] = branch_make (kid0->weight + kid1->weight, kid0, kid1);
  insert (i+1);
}


static int
compare_weights (const void *n1, const void *n2)
{
  Node node1 = *(Node *)n1;
  Node node2 = *(Node *)n2;
  if (node1->weight < node2->weight)
    return -1;
  else if (node1->weight == node2->weight)
    return 0;
  else
    return 1;
}


static Node
build_tree (void)
{
  int i;
  qsort (nodes, alphabet_size, sizeof nodes[0], compare_weights);

  /* Skip unused nodes (except we keep the last node even if it's unused */
  for (i = 0; i < alphabet_size - 1; ++i)
    if (0 != nodes[i]->weight)
      break;

  /* Combine the remainder into a tree */
  for (; i < alphabet_size - 1; ++i)
    combine_pair (i);

  return nodes[i];
}


/* Compression */

static void
make_histogram (FILE *in)
{
  int c;

  for (c = 0; c < alphabet_size; ++c)
    nodes[c] = leaf_make (0, c);

  while ((c = getc (in)) != EOF)
    nodes[c]->weight++;

  if (ferror (in))
    die ("Couldn't read input: %s", strerror (errno));
}


typedef struct Bitstring {
  int length;
  unsigned char bits[1];	/* variable length, actually */
} Bitstring;

Bitstring *char_map[alphabet_size];


struct Frame {
  int bit;
  const struct Frame *next;
};


static Bitstring *
make_bitstring (int length, const struct Frame *frame)
{
  Bitstring *bs = allot (sizeof *bs + sizeof bs->bits[0] * length);
  int i;

  bs->length = length;
  for (i = 0; i < length; ++i, frame = frame->next)
    bs->bits[length - 1 - i] = frame->bit;

  return bs;
}


static void
mapping (Node n, int length, const struct Frame *next)
{
  if (is_leaf (n))
    char_map[n->leaf] = make_bitstring (length, next);
  else
    {
      /*
	For some reason Sun's CC complains about this:
        struct Frame frame = { 0, next };
       */
      struct Frame frame;
      frame.bit = 0;
      frame.next = next;

      mapping (n->kid0, length + 1, &frame);
      frame.bit = 1;
      mapping (n->kid1, length + 1, &frame);
    }
}


static void
make_char_map (Node root)
{
  mapping (root, 0, NULL);
}


static void
encode_symbol (FILE *out, int c)
{
  const Bitstring *bs = char_map[c];
  int i;
  for (i = 0; i < bs->length; ++i)
    put_bit (out, bs->bits[i]);
}


static void
encode (FILE *out, FILE *in)
{
  int c;
  
  while ((c = getc (in)) != EOF)
    encode_symbol (out, c);

  if (ferror (in))
    die ("Couldn't read input: %s", strerror (errno));
  
  flush_bits (out);
}


/* Decompressor generator */

static void
indent (int level)
{
  int spaces = 6 + 2 * level;
  printf ("%*.*s", spaces, spaces, "");
}


static void
writing (Node n, int level)
{
  
  if (is_leaf (n))
    {
      indent (level); printf ("%d\n", n->leaf);
    }
  else
    {
      indent (level); printf ("bit if\n");
      writing (n->kid1, level + 1);
      indent (level); printf ("else\n");
      writing (n->kid0, level + 1);
      indent (level); printf ("then\n");
    }
}


static void
write_decompressor (Node root)
{
  if (root->weight == 0) 
    {
      printf ("def 0 1 main   0 ;\n");
      return;
    }

  printf ("def 0 1 main\n"
	  "  1  0 c@  7  %d  loop  0 ;\n", root->weight);

  printf ("def 3 4 bit\n"
	  "  { ptr bits nb --\n"
	  "    nb if\n"
	  "      ptr  bits 1 >>>  nb 1 -\n"
	  "    else\n"
	  "      ptr 1 +  ptr c@  7\n"
	  "    then\n"
	  "    bits 1 and } ;\n");

  printf ("def 4 0 loop\n"
	  "  { n --\n"
	  "    n if\n");
  writing (root, 0);
  printf ("      emit  n 1 - loop\n"
	  "    else\n"
	  "      { ptr bits nb -- }\n"
	  "    then } ;\n");
}


/* The end, hurrah */

int 
main (int argc, char **argv)
{
  FILE *in;
  Node root;

  program_name = argv[0];

  if (argc != 2)
    die ("Usage: huff infile");

  in = open_file (argv[1], "r");
  make_histogram (in);
  close_file (in);

  root = build_tree ();
  node_check (root);
  make_char_map (root);

  
  in = open_file (argv[1], "r");
  printf ("string: message\n\"");
  encode (stdout, in);
  printf ("\"\n");
  close_file (in);

  write_decompressor (root);

  return 0;
}
