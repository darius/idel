e := examples/huffman

examples-huffman-test: $e/huff bin/idelasm bin/idelvm
	$e/huff $e/test-input >puff.idel
	bin/idelasm puff.idel >puff
	bin/idelvm puff >tmp-output
	diff $e/test-input tmp-output

$e/huff: $e/huff.o


EXAMPLES += $e/huff
TESTS += examples-huffman-test
CLEAN += $e/huff $e/huff.o puff.idel puff tmp-output
