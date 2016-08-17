b := examples/hello

examples-hello-test: $b/hello
	bin/idelvm $b/hello >tmp-output
	echo 'Hello, world!' | diff tmp-output -

$b/hello: $b/hello.idel bin/idelasm
	cpp $< | bin/idelasm >$@


EXAMPLES += $b/hello
TESTS += examples-hello-test
CLEAN += $b/hello tmp-output
