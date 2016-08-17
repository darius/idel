"""
An example task.

A task is an object you create to put your jobs onto borednet.
"""

import dealer
import math


class FactoringTask:
    """Use borednet to factor a number.  The search for factors is
    split up into fixed-size chunks."""
    
    def __init__(self, number_to_factor, chunk_size):
        self.n = number_to_factor
        self.lo = 2
        self.limit = int(math.sqrt(number_to_factor))
        self.chunk_size = chunk_size
	self.answer = None

    def get_job(self, jobber_info):
	"""Return a new job that will contribute to completing the
	task.  jobber_info is an object supplied by the jobber saying
	what it claims to be able to do.  If there are no more jobs
	to issue, return None."""
        if self.limit < self.lo:
            return None
	new_lo = min(self.limit + 1, self.lo + self.chunk_size)
        job = FactoringJob(self.n, self.lo, new_lo)
        self.lo = new_lo
        return job

    def take_answer(self, job):
	"""Called when a job has completed."""
        if job.answer:
            print '%d factors %d' % (job.answer, self.n)
	    self.answer = job.answer
            self.lo = self.limit + 1

    def take_failure(self, job, host, reason):
	"""Called when a host tried but failed to run a job created 
	by this task.  What's in `reason' is currently unspecified."""
        pass

    def clean_up(self):
	"""Called when all jobs have completed and there are no more
	jobs to issue."""
	if not self.answer:
	    print 'Barring malfeasance, %d is prime' % self.n


class FactoringJob:
    """A job searching a range of numbers for factors of n."""

    def __init__(self, n, lo, hi):
        self.n = n
        self.lo = lo
        self.hi = hi

    def push_code(self, session):
	"""Push onto session an idel object file that will compute 
	this job's result."""
        source_code = """
          def 0 1 main   %d %d testing u.  '\\n' emit  0 ;

          def 2 1 testing
            { n i --  %d i u< if
                        0
                      else   n i umod  0 = if  
                        i
                      else
                        n  i 1 +  testing
                      then then } ;

          def 2 1 u/     u/mod { quotient remainder -- quotient } ;
          def 2 1 umod   u/mod { quotient remainder -- remainder } ;

          def 1 0 u.
            { u --  9 u u< if  u 10 u/ u.  then
                    u 10 umod  '0' + emit } ;
          """ % (self.n, self.lo, self.hi - 1)
        session.assemble_and_push(source_code)

    def answer_seems_ok(self, output):
	"""Return true iff output (a string) appears to be a correct result
	of the computation sent out."""
	# TODO: return yes, no, or maybe
        self.answer = None
        try:
            v = int(output)
        except ValueError:
            return 0
        if 1 < v < self.n and self.n % v == 0:
            self.answer = v
            return 1
	else:
            return v == 0

    def estimated_time(self, jobber_info):
	"""Return a guess at how long the described jobber should take to
	compute this job.  Return None if the jobber lacks required
	resources."""
	# The inner loop is the `testing' function at around 20 cycles
	# per test:
	return jobber_info.cycle_time() * 20 * (self.hi - self.lo)


if __name__ == '__main__':
    dealer.add_task(FactoringTask(121, 5))
    dealer.loop()
