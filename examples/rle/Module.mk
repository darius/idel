c := examples/rle

examples-rle-test: $c/rlencode $c/rldecode
	$c/rlencode <$c/test-input | \
             bin/idelvm $c/rldecode >tmp-output
	diff $c/test-input tmp-output

$c/rldecode: $c/rldecode.idel bin/idelasm
	cpp $< | bin/idelasm >$@

$c/rlencode: $c/rlencode.o


EXAMPLES += $c/rldecode $c/rlencode
TESTS += examples-rle-test
CLEAN += $c/rldecode tmp-output $c/rlencode $c/rlencode.o
