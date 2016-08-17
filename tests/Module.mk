SUCCEED_TESTS := basics data even fact factor fib-test hello hotpo-test \
                 primes rev-test tak-test big-token
FAIL_TESTS := bounds1 bounds2 recurse blow-stack initial-stack unaligned \
              stack-bug

tests-test:
	tests/try-each try $(SUCCEED_TESTS)
	tests/try-each try-vm-fail $(FAIL_TESTS)
	tests/try-each try-asm-fail undef
	rm -f frozen; tests/try-each try-devel save1
	(bin/idelvm -development frozen | diff - tests/frozen1.ok) && echo passed: frozen1
	tests/try-each try beer
	tests/try-each try wc <tests/wc.in
	tests/try-loop
	tests/try-each try copy <tests/copy.in


TESTS += tests-test
CLEAN += output tmp
