# Generate the inner interpreter and related tables from the opcodes file.
# Copyright (C) 2001-2002 Darius Bacon

BEGIN {
  gnu_c = (cc == "gcc");

  nesting = 0;
  opcode = -1;
  code_file = "src/interp.inc";
  dict_file = "src/dict.inc";
  name_file = "src/names.inc";
  args_file = "src/args.inc";
  enum_file = "src/enum.inc";
  label_file = "src/labels.inc";
  effect_file = "src/effect.inc";
  prims_file = "src/prims.inc";
  peep_file = "src/peep.inc";
  peep1_file = "src/peep1.inc";

  # The peep files won't necessarily get written to, so let's clear
  # them out before we start -- otherwise we might have an old version
  # lying around afterwards.
  print "" >peep_file;
  print "" >peep1_file;

  if (gnu_c)
    code("goto *(pc[0].lbl);");
  else {
    code("for (;;) switch (pc[0].i32) {")
  }
}

/^#/ { next; }         # skip comments

/^[^ \t]/ { 
  ++opcode; 
  new_op(); 
  next;
}

NF != 0 { 
  sub(/^[ \t]*/, "");
  lines[opcode, ++nlines[opcode]] = $0;
}

END { 
  while (getline <"src/combos" > 0) {
    if ($1 in op_named && $2 in op_named)
      combine_ops(++opcode, op_named[$1], op_named[$2]);
    else
      error("No such opcode: one of " $0);
  }

  for (i = 0; i <= opcode; ++i)
    dump_op(i);

  printf ("  num_opcodes = %d\n", opcode + 1) >enum_file;

  if (!gnu_c) {
    code("default: unreachable ();");
    code("}");
  }
}


function new_op(    j, inp, ty, outp)
{
  mnemonic[opcode] = "";
  nlines[opcode] = 0;
  parse_signature();
}

function parse_signature(    i)
{
  popping[opcode] = istream[opcode] = pushing[opcode] = 0;
  range[opcode] = $1;
  c_name[opcode] = $2;
  op_named[c_name[opcode]] = opcode;
  for (i = 3; ; ++i) {
    if (NF < i)
      error("No `--' in instruction signature");
    if ($i == "--")
      break;
    if ($i == "??")
      popping[opcode] = -1;
    else if ($i ~ /^[unpc][1-4]$/) {
      if (mnemonic[opcode] == "")
	inputs[opcode, ++popping[opcode]] = $i;
      else
	inlines[opcode, ++istream[opcode]] = $i;
    } else {
      if (mnemonic[opcode] != "")
	error("Mnemonic not obvious");
      mnemonic[opcode] = $i;
    }
  }
  for (++i; i <= NF; ++i) {
    if ($i == "??")
      pushing[opcode] = -1;
    else if ($i ~ /^[unpc][1-4]$/)
      outputs[opcode, ++pushing[opcode]] = $i;
    else
      error("Unknown output type");
  }
}

function combine_ops(opc, op1, op2,   j, v, k, tween, tweens, inp, outp, src)
{
  part1[opc] = op1;
  part2[opc] = op2;
  mnemonic[opc] = mnemonic[op1] "_" mnemonic[op2];
  c_name[opc] = c_name[op1] "_" c_name[op2];
  op_named[c_name[opc]] = opc;

  istream[opc] = istream[op1] + istream[op2];
  for (j = 1; j <= istream[opc]; ++j) {
    if (j <= istream[op1])
      v = inlines[op1, j];
    else
      v = inlines[op2, j - istream[op1]];
    inlines[opc, j] = substr(v, 1, 1) "_operand_" c_name[opc] "_" j;
  }

  range[opc] = "3";

  if (0 <= popping[op2] - pushing[op1]) {
    popping[opc] = popping[op1] - pushing[op1] + popping[op2];
    pushing[opc] = pushing[op2];
  } else {
    popping[opc] = popping[op1];
    pushing[opc] = pushing[op1] - popping[op2] + pushing[op2];
  }
  # FIXME: account correctly for dynamic stack effects
  if (popping[op1] < 0)
    popping[opc] = -1;
  if (pushing[op2] < 0)
    pushing[opc] = -1;
  for (j = 1; j <= popping[opc]; ++j)
    inputs[opc, j] = "n_in_" c_name[opc] "_" j;
  for (j = 1; j <= pushing[opc]; ++j)
    outputs[opc, j] = "n_out_" c_name[opc] "_" j;

  tween = "n_tween_" c_name[opc] "_";
  tweens = min(pushing[op1], popping[op2]);
  k = 0;
  lines[opc, ++k] = "{";
  for (j = 1; j <= tweens; ++j)
    lines[opc, ++k] = sprintf("%s %s;", 
			      type("n"), tween j);
  lines[opc, ++k] = "{";
  for (j = 1; j <= istream[op1]; ++j)
    lines[opc, ++k] = sprintf("%s %s = %s;",
			      type(inlines[op1, j]),
			      inlines[op1, j],
			      inlines[opc, j]);
  for (j = 1; j <= pushing[op1]; ++j)
    lines[opc, ++k] = sprintf("%s %s;", 
			      type(outputs[op1, j]),
			      outputs[op1, j]);
  for (j = 1; j <= popping[op1]; ++j)
    lines[opc, ++k] = sprintf("%s %s = %s;", 
			      type(inputs[op1, j]), 
			      inputs[op1, j],
			      inputs[opc, popping[opc] - popping[op1] + j]);
  for (j = 1; j <= nlines[op1]; ++j)
    lines[opc, ++k] = lines[op1, j];
  for (j = 1; j <= pushing[op1]; ++j) {
    if (j <= pushing[op1] - tweens)
      outp = outputs[opc, j];
    else
      outp = tween (j - (pushing[op1] - tweens))
    lines[opc, ++k] = sprintf("%s = %s;", 
			      outp,
			      outputs[op1, j]);
  }
  lines[opc, ++k] = "}";
  lines[opc, ++k] = "{";
  for (j = 1; j <= istream[op2]; ++j)
    lines[opc, ++k] = sprintf("%s %s = %s;",
			      type(inlines[op2, j]),
			      inlines[op2, j],
			      inlines[opc, istream[op1] + j]);
  for (j = 1; j <= pushing[op2]; ++j)
    lines[opc, ++k] = sprintf("%s %s;", 
			      type(outputs[op2, j]),
			      outputs[op2, j]);
  for (j = 1; j <= popping[op2]; ++j) {
    inp = inputs[op2, j];
    if (j <= popping[op2] - tweens)
      src = inputs[opc, j];
    else
      src = tween (j - (popping[op2] - tweens))
    lines[opc, ++k] = sprintf("%s %s = %s;", type(inp), inp, src);
  }
  for (j = 1; j <= nlines[op2]; ++j)
    lines[opc, ++k] = lines[op2, j];
  for (j = 1; j <= pushing[op2]; ++j) 
    lines[opc, ++k] = sprintf("%s = %s;",
			      outputs[opc, j + (pushing[opc] - pushing[op2])],
			      outputs[op2, j]);
  lines[opc, ++k] = "}";
  lines[opc, ++k] = "}";
  nlines[opc] = k;
}

function dump_peep(opc,   file, any, j, combo, R, else_part)
{
  if (0 == istream[opc])
    file = peep_file;
  else
    file = peep1_file;
  for (j = 0; j <= opcode; ++j) {
    if (j in part1 && part1[j] == opc) {
      if (!any)
	print "      case " c_name[opc] ":" >file;
      combo = c_name[j]
      R = c_name[part2[j]];
      else_part = (any ? "else " : "");
      print "	" else_part "if (vm->there[0].ptr == opcode_labels[" R "])" >file;
      print "	  combined_opcode = " combo ";" >file;
      any = 1;
    }
  }
  if (any)
    print "	break;" >file;
}

function dump_op(opc)
{
  dump_peep(opc);
  printf("  \"%s\",\n", c_name[opc]) >name_file;
  printf("  %d,\n", istream[opc]) >args_file;
  printf ("  %s = %d,\n", c_name[opc], opc) >enum_file;

  if (!is_internal(opc)) {
    printf("  install_primitive(\"%s\", %d);\n", mnemonic[opc], opc) >dict_file;
    if (c_name[opc] != "save")
      printf("    case %s:\n", c_name[opc]) >prims_file;
  }

  printf("  { %d, %d },\n", popping[opc], pushing[opc] - popping[opc]) >effect_file;

  if (gnu_c) {
    print "  &&opcode" opc "," >label_file;
    code("opcode%d:\t\t\t/* %s */", opc, c_name[opc]);
  } else {
    print "  (cplabel) " opc "," >label_file;
    code("case %d:\t\t\t/* %s */", opc, c_name[opc]);
  }
  dump_op_code(opc);
}

function dump_op_code(opc,   j, inp, ty, outp, line, saw_next)
{
  code("if (tracing) trace_op (pc, stack, sp+1);");
  code("{");

  for (j = 1; j <= istream[opc]; ++j) {
    inp = inlines[opc, j];
    ty = type(inp);
    if (ty == "const cell *")
      code("%s %s = pc[%d].ptr;", ty, inp, j);
    else
      code("%s %s = (%s) pc[%d].i32;", ty, inp, ty, j);
  }
  for (j = 1; j <= popping[opc]; ++j) {
    inp = inputs[opc, popping[opc] - j + 1];
    ty = type(inp);
    code("%s %s = (%s) sp[-%d].i32;", ty, inp, ty, j-1);
  }
  for (j = 1; j <= pushing[opc]; ++j) {
    outp = outputs[opc, j];
    ty = type(outp);
    code("%s %s;", ty, outp);
  }
  for (j = 1; j <= nlines[opc]; ++j) {
    line = lines[opc, j];
    if (line ~ /NEXT/) {
      saw_next = 1;
      gsub(/NEXT/, "pc += " 1 + istream[opc], line);
    }
    code("%s", line);
  }
  if (!saw_next)
    code("pc += %d;", 1 + istream[opc]);
  if (pushing[opc] != popping[opc] && 0 <= pushing[opc] && 0 <= popping[opc])
    code("sp += %d;", pushing[opc] - popping[opc]);
  if (gnu_c) {
    code("{");
    code("cplabel next_label = pc[0].lbl;");
  }
  for (j = pushing[opc]; 1 <= j; --j) {
    outp = outputs[opc, pushing[opc] - j + 1];
    code("sp[-%d].i32 = %s;", j-1, outp);
  }
  if (gnu_c) {
    code("goto *next_label;");
    code("}");
  } else {
    code("break;");
  }
  code("}");
}

function type(v)
{
  if (v ~ /^n/) return "i32";
  if (v ~ /^u/) return "u32";
  if (v ~ /^p/) return "u32";    # todo: add in range-checking code
  if (v ~ /^c/) return "const cell *";
  error("Huh?");
}

function code(format, a, b, c, d, e, out)
{
  out = sprintf(format, a, b, c, d, e);
  if (out ~ /}/) --level;
  printf("%*s%s\n", level*2, "", out) >code_file;
  if (out ~ /{/) ++level;
}

# FIXME: I suspect we want more distinctions
function is_internal(opc)
{
  return range[opc] != "123";
}

function min(x, y)
{
  return x < y ? x : y;
}

function error(message)
{
  print message >"/dev/stderr";
  exit(1);
}
