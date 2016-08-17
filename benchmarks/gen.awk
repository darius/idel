BEGIN {
  count = 20000;
  increment = 17;    # must be coprime to count

  printf("def 0 1 main  52527 foo%d  127 and emit  0 ;\n", count);
  printf("def 1 1 foo0 ;\n");
  j = 1;
  for (i = 1; i <= count; ++i) {
    printf("def 1 1 foo%d { n --  n 1 and if  n 3 * 1 +  else  n 1 >>  then  foo%d } ;\n", j, j - 1);
    j = (j + increment) % count;
    if (j == 0) j = count;
  }

  data = 5000;
  printf("ints: bar\n");
  for (i = 1; i <= data; ++i)
    printf(" %d\n", i);
  printf(";ints\n");
}
