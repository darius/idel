"""
An example task.  Meant only as a demo and for rough performance
comparison.  In other words, it's buggy!  I suck.

See http://www.dcs.ex.ac.uk/~aba/hashcash/
"""

import dealer


class HashcashTask:
    """Use borednet to mint hashcash.  The search for collisions is
    split up into fixed-size chunks."""
    
    def __init__(self, string_to_collide, bits):
        self.n = string_to_collide
	self.bits = bits
        self.lo = 0L
        self.chunk_size = 2L**bits

    def get_job(self, jobber_info):
	"""Return a new job that will contribute to completing the
	task.  jobber_info is an object supplied by the jobber saying
	what it claims to be able to do.  If there are no more jobs
	to issue, return None."""
	new_lo = self.lo + self.chunk_size
        job = HashcashJob(self.n, self.bits, self.lo, new_lo)
        self.lo = new_lo
        return job

    def take_answer(self, job):
	"""Called when a job has completed."""
        if job.answer:
            print 'New hashcash: %s' % (job.answer)

    def take_failure(self, job, host, reason):
	"""Called when a host tried but failed to run a job created 
	by this task.  What's in `reason' is currently unspecified."""
        pass

    def clean_up(self):
	"""Called when all jobs have completed and there are no more
	jobs to issue."""
        pass


class HashcashJob:
    """A job searching for a hashcash collision in a range."""

    def __init__(self, str, bits, lo, hi):
	"""bits must be >= 8"""
        self.n = str
	self.bits = bits
        self.lo = lo
        self.hi = hi

    def push_code(self, session):
	"""Push onto session an idel object file that will compute 
	this job's result."""
        source_code = """
ints: H
0 0 0 0 0
;ints

ints: W
0 0 0 0
0 0 0 0
0 0 0 0
0 0 0 0
;ints

ints: hbits  0  ;ints
ints: lbits  0  ;ints

ints: SHA1-IV
0x67452301
0xEFCDAB89
0x98BADCFE
0x10325476
0xC3D2E1F0
;ints


bss-data: collision 100

string: target "%s"
bss-data: target-digest 20

bss-data: try 100

def 0 1 main
  %d %d find-collision if
    try  target.size 16 +  type  '\\n' emit
  then
  0 ;


def 2 1 find-collision
  { tries trial --
    try target.addr target.size memcpy
    try target.size +  7  trial  sprintf-hex
    8  7 and
    { partial-byte --
      %d 3 >>> 
      1 %d partial-byte - <<  1 -  -1 xor
      { partial-byte-index partial-byte-mask --
        partial-byte-index  partial-byte if 1 + then
        { check-bytes --
          target-digest target.addr target.size digest
          partial-byte if
            target-digest partial-byte-index +
            { a -- a c@  partial-byte-mask and  a c! }
          then
          try target.size + 8 +  7  0  sprintf-hex
          target.size 16 +
          tries 8 + 4 >>>
          { try-strlen tries2 --
            partial-byte partial-byte-index partial-byte-mask check-bytes
            try-strlen tries2 trial 0 outer-loop } } } } } ;

bss-data: precomputed-ctx 92

def 8 1 outer-loop
  { try-strlen tries2 trial i --
    i tries2 < if
      SHA1-init
      try target.size + 8 +  7  trial  sprintf-hex
      try  try-strlen 1 -  SHA1-update
      precomputed-ctx H 92 wordcpy
      try-strlen tries2 trial i 0 inner-loop
    else
      { partial-byte partial-byte-index partial-byte-mask check-bytes -- 0 }
    then } ;

bss-data: last-char 4

string: hex "0123456789abcdef"

def 9 1 inner-loop
  { j --
    j 16 < if
      H precomputed-ctx 92 wordcpy
      hex.addr j + c@  last-char c!
      last-char 1 SHA1-update
      SHA1-final
      H c@  target-digest c@ - if
        j 1 +  inner-loop
      else
        { partial-byte partial-byte-index partial-byte-mask check-bytes
          try-strlen tries2 trial i --
          partial-byte if
            H partial-byte-index + 
            { a -- a c@  partial-byte-mask and  a c! }
          then
          target-digest H check-bytes memcmp if
            partial-byte partial-byte-index partial-byte-mask check-bytes
            try-strlen tries2 trial i  j 1 +  inner-loop
          else
            hex.addr j + c@  try try-strlen + 1 - c!
            collision try try-strlen memcpy
            i 4 << j + 1 +
          then }
      then
    else
      { trial i --
        trial 16 +  i 1 +  outer-loop }
    then } ;


def 3 0 sprintf-hex
  { addr j x --
    j if  addr  j 1 -  x 4 >>>  sprintf-hex  then 
    x 15 and  hex.addr + c@  addr j + c! } ;



def 3 0 digest
  SHA1-init SHA1-update SHA1-final
  H 20 wordcpy ;

def 0 0 SHA1-init
  0 hbits !  0 lbits !
  H SHA1-IV 20 wordcpy ;

def 2 0 SHA1-update
  { pdata data-len --
    lbits @  3 >>>  63 and
    data-len 29 >>>  hbits +!
    data-len 3 <<
    { mlen low-bits --
      low-bits lbits +!
      lbits @  low-bits < if  1 hbits +!  then
      64 mlen -  data-len  { x y -- x y u< if x else y then }
      { use --
        W mlen +  pdata  use  memcpy
        pdata use +  data-len use -  mlen use +  SHA1-update-loop } } } ;

def 3 0 SHA1-update-loop
  { data data-len mlen --
    mlen 64 = if
      SHA1-transform
      64 data-len { x y -- x y u< if x else y then }
      { use --
        W data use memcpy
        data use +  data-len use -  use  SHA1-update-loop }
    then } ;

def 0 0 SHA1-final
  lbits @  3 >>>  63 and
  { mlen -- 0x80 W mlen + c!  mlen 1 + }
  { mlen --
    64 mlen -
    { padding --
      padding 8 < if
        W mlen +  0  padding  memset
        SHA1-transform
        W  0  64 8 -  memset
      else
        W mlen +  0  padding 8 -  memset
      then } }
  hbits @  W 64 + 8 - !
  lbits @  W 64 + 4 - !
  SHA1-transform ;

def 0 0 SHA1-transform
  0 @  4 @  8 @  12 @  16 @
  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 20 @ + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 24 @ + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 28 @ + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 32 @ + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 36 @ + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 40 @ + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 44 @ + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 48 @ + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 52 @ + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 56 @ + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 60 @ + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 64 @ + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 68 @ + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 72 @ + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 76 @ + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 80 @ + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 72 @ 52 @ xor 28 @ xor 20 @ xor { x -- x 1 << x 31 >>> or } { x -- x 20 !  x } + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 76 @ 56 @ xor 32 @ xor 24 @ xor { x -- x 1 << x 31 >>> or } { x -- x 24 !  x } + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 80 @ 60 @ xor 36 @ xor 28 @ xor { x -- x 1 << x 31 >>> or } { x -- x 28 !  x } + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B and D xor + E + 20 @ 64 @ xor 40 @ xor 32 @ xor { x -- x 1 << x 31 >>> or } { x -- x 32 !  x } + 1518500249 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 24 @ 68 @ xor 44 @ xor 36 @ xor { x -- x 1 << x 31 >>> or } { x -- x 36 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 28 @ 72 @ xor 48 @ xor 40 @ xor { x -- x 1 << x 31 >>> or } { x -- x 40 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 32 @ 76 @ xor 52 @ xor 44 @ xor { x -- x 1 << x 31 >>> or } { x -- x 44 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 36 @ 80 @ xor 56 @ xor 48 @ xor { x -- x 1 << x 31 >>> or } { x -- x 48 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 40 @ 20 @ xor 60 @ xor 52 @ xor { x -- x 1 << x 31 >>> or } { x -- x 52 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 44 @ 24 @ xor 64 @ xor 56 @ xor { x -- x 1 << x 31 >>> or } { x -- x 56 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 48 @ 28 @ xor 68 @ xor 60 @ xor { x -- x 1 << x 31 >>> or } { x -- x 60 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 52 @ 32 @ xor 72 @ xor 64 @ xor { x -- x 1 << x 31 >>> or } { x -- x 64 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 56 @ 36 @ xor 76 @ xor 68 @ xor { x -- x 1 << x 31 >>> or } { x -- x 68 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 60 @ 40 @ xor 80 @ xor 72 @ xor { x -- x 1 << x 31 >>> or } { x -- x 72 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 64 @ 44 @ xor 20 @ xor 76 @ xor { x -- x 1 << x 31 >>> or } { x -- x 76 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 68 @ 48 @ xor 24 @ xor 80 @ xor { x -- x 1 << x 31 >>> or } { x -- x 80 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 72 @ 52 @ xor 28 @ xor 20 @ xor { x -- x 1 << x 31 >>> or } { x -- x 20 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 76 @ 56 @ xor 32 @ xor 24 @ xor { x -- x 1 << x 31 >>> or } { x -- x 24 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 80 @ 60 @ xor 36 @ xor 28 @ xor { x -- x 1 << x 31 >>> or } { x -- x 28 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 20 @ 64 @ xor 40 @ xor 32 @ xor { x -- x 1 << x 31 >>> or } { x -- x 32 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 24 @ 68 @ xor 44 @ xor 36 @ xor { x -- x 1 << x 31 >>> or } { x -- x 36 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 28 @ 72 @ xor 48 @ xor 40 @ xor { x -- x 1 << x 31 >>> or } { x -- x 40 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 32 @ 76 @ xor 52 @ xor 44 @ xor { x -- x 1 << x 31 >>> or } { x -- x 44 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 36 @ 80 @ xor 56 @ xor 48 @ xor { x -- x 1 << x 31 >>> or } { x -- x 48 !  x } + 1859775393 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 40 @ 20 @ xor 60 @ xor 52 @ xor { x -- x 1 << x 31 >>> or } { x -- x 52 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 44 @ 24 @ xor 64 @ xor 56 @ xor { x -- x 1 << x 31 >>> or } { x -- x 56 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 48 @ 28 @ xor 68 @ xor 60 @ xor { x -- x 1 << x 31 >>> or } { x -- x 60 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 52 @ 32 @ xor 72 @ xor 64 @ xor { x -- x 1 << x 31 >>> or } { x -- x 64 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 56 @ 36 @ xor 76 @ xor 68 @ xor { x -- x 1 << x 31 >>> or } { x -- x 68 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 60 @ 40 @ xor 80 @ xor 72 @ xor { x -- x 1 << x 31 >>> or } { x -- x 72 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 64 @ 44 @ xor 20 @ xor 76 @ xor { x -- x 1 << x 31 >>> or } { x -- x 76 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 68 @ 48 @ xor 24 @ xor 80 @ xor { x -- x 1 << x 31 >>> or } { x -- x 80 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 72 @ 52 @ xor 28 @ xor 20 @ xor { x -- x 1 << x 31 >>> or } { x -- x 20 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 76 @ 56 @ xor 32 @ xor 24 @ xor { x -- x 1 << x 31 >>> or } { x -- x 24 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 80 @ 60 @ xor 36 @ xor 28 @ xor { x -- x 1 << x 31 >>> or } { x -- x 28 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 20 @ 64 @ xor 40 @ xor 32 @ xor { x -- x 1 << x 31 >>> or } { x -- x 32 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 24 @ 68 @ xor 44 @ xor 36 @ xor { x -- x 1 << x 31 >>> or } { x -- x 36 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 28 @ 72 @ xor 48 @ xor 40 @ xor { x -- x 1 << x 31 >>> or } { x -- x 40 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 32 @ 76 @ xor 52 @ xor 44 @ xor { x -- x 1 << x 31 >>> or } { x -- x 44 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 36 @ 80 @ xor 56 @ xor 48 @ xor { x -- x 1 << x 31 >>> or } { x -- x 48 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 40 @ 20 @ xor 60 @ xor 52 @ xor { x -- x 1 << x 31 >>> or } { x -- x 52 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 44 @ 24 @ xor 64 @ xor 56 @ xor { x -- x 1 << x 31 >>> or } { x -- x 56 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 48 @ 28 @ xor 68 @ xor 60 @ xor { x -- x 1 << x 31 >>> or } { x -- x 60 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D or B and C D and or + E + 52 @ 32 @ xor 72 @ xor 64 @ xor { x -- x 1 << x 31 >>> or } { x -- x 64 !  x } + -1894007588 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 56 @ 36 @ xor 76 @ xor 68 @ xor { x -- x 1 << x 31 >>> or } { x -- x 68 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 60 @ 40 @ xor 80 @ xor 72 @ xor { x -- x 1 << x 31 >>> or } { x -- x 72 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 64 @ 44 @ xor 20 @ xor 76 @ xor { x -- x 1 << x 31 >>> or } { x -- x 76 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 68 @ 48 @ xor 24 @ xor 80 @ xor { x -- x 1 << x 31 >>> or } { x -- x 80 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 72 @ 52 @ xor 28 @ xor 20 @ xor { x -- x 1 << x 31 >>> or } { x -- x 20 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 76 @ 56 @ xor 32 @ xor 24 @ xor { x -- x 1 << x 31 >>> or } { x -- x 24 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 80 @ 60 @ xor 36 @ xor 28 @ xor { x -- x 1 << x 31 >>> or } { x -- x 28 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 20 @ 64 @ xor 40 @ xor 32 @ xor { x -- x 1 << x 31 >>> or } { x -- x 32 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 24 @ 68 @ xor 44 @ xor 36 @ xor { x -- x 1 << x 31 >>> or } { x -- x 36 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 28 @ 72 @ xor 48 @ xor 40 @ xor { x -- x 1 << x 31 >>> or } { x -- x 40 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 32 @ 76 @ xor 52 @ xor 44 @ xor { x -- x 1 << x 31 >>> or } { x -- x 44 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 36 @ 80 @ xor 56 @ xor 48 @ xor { x -- x 1 << x 31 >>> or } { x -- x 48 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 40 @ 20 @ xor 60 @ xor 52 @ xor { x -- x 1 << x 31 >>> or } { x -- x 52 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 44 @ 24 @ xor 64 @ xor 56 @ xor { x -- x 1 << x 31 >>> or } { x -- x 56 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 48 @ 28 @ xor 68 @ xor 60 @ xor { x -- x 1 << x 31 >>> or } { x -- x 60 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 52 @ 32 @ xor 72 @ xor 64 @ xor { x -- x 1 << x 31 >>> or } { x -- x 64 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 56 @ 36 @ xor 76 @ xor 68 @ xor { x -- x 1 << x 31 >>> or } { x -- x 68 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 60 @ 40 @ xor 80 @ xor 72 @ xor { x -- x 1 << x 31 >>> or } { x -- x 72 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 64 @ 44 @ xor 20 @ xor 76 @ xor { x -- x 1 << x 31 >>> or } { x -- x 76 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }  { A B C D E -- A 5 << A 27 >>> or C D xor B xor + E + 68 @ 48 @ xor 24 @ xor 80 @ xor { x -- x 1 << x 31 >>> or } { x -- x 80 !  x } + -899497514 +  A  B 30 << B 2 >>> or  C  D }   16 +!  12 +!  8 +!  4 +!  0 +! ;

def 2 0 dump-loop
  { a n -- n if  a @ x. a separator emit  a 4 +  n 1 -  dump-loop then } ;
def 1 1 separator
  4 +  31 and if  ' '  else  '\\n'  then ;

def 1 0 x.  7 x.loop ;
def 2 0 x.loop 
  { u n --  n if  u 4 >>>  n 1 -   x.loop  then
            u 15 and  hex-digit emit } ;
def 1 1 hex-digit
  { u -- u 10 < if  '0'  else  'a' 10 -  then  u + } ;


def 2 0 ascii-dump
  { addr u --
    u if
      addr c@ emit
      addr 1 +  u 1 -  ascii-dump
    then } ;

def 2 1 u/     u/mod { quot rem -- quot } ;
def 2 1 umod   u/mod { quot rem -- rem } ;

def 1 0 u.
  { u --  9 u u< if  u 10 u/ u.  then
          u 10 umod  '0' + emit } ;

def 2 0 type 
  { a n --  0 n < if  a c@ emit  a 1 +  n 1 -  type  then } ;


\\ more stuff

def 0 0 dump-H  
  H 5 dump-loop  '\\n' emit ;

def 2 0 +! { a -- a @  +  a ! } ;

def 3 0 memcpy
  { dst src n --
    dst src n
    dst 3 and if
      bytecpy
    else src 3 and if
      bytecpy
    else n 3 and if
      bytecpy
    else
      wordcpy
    then then then } ;

def 3 0 wordcpy
  { dst src n --
    n if
      src @ dst !
      dst 4 +  src 4 +  n 4 -  wordcpy
    then } ;

def 3 0 bytecpy
  { dst src n --
    n if
      src c@ dst c!
      dst 1 +  src 1 +  n 1 -  bytecpy
    then } ;

def 3 0 memset
  { dst byte n --
    n if
      byte dst c!
      dst 1 +  byte  n 1 -  memset
    then } ;

def 3 1 memcmp
  { dst src n --
    n if
      src c@  dst c@ = if
        dst 1 +  src 1 +  n 1 -  memcmp
      else
        -1
      then
    else
      0
    then } ;


          """ % (self.n, self.hi - self.lo, self.lo, self.bits, self.bits)
        session.assemble_and_push(source_code)

    def answer_seems_ok(self, output):
	"""Return true iff output (a string) appears to be a correct result
	of the computation sent out."""
	self.answer = output
	return 1

    def estimated_time(self, jobber_info):
	"""Return a guess at how long the described jobber should take to
	compute this job.  Return None if the jobber lacks required
	resources."""
	a_lot = 10000
	return jobber_info.cycle_time() * a_lot * (self.hi - self.lo)


if __name__ == '__main__':
    dealer.add_task(HashcashTask('09948flame', 18))
    dealer.loop()
