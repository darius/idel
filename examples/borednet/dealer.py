"""
Borednet dealers.

A dealer keeps a list of tasks to work on (each task being divisible
into jobs), plus a list of jobs sent out but not yet completed.  The
dealer code needs more work to be realistically useful -- keeping
track of which jobs failed and need to be reissued, and which ones
should have finished by now but haven't, communicating with other
dealers about available tasks in order to get referrals, sending out
jobs redundantly for cross-checking, and so on.
"""


import asynchat
import asyncore
import os
import popen2
import socket
import string

import swiss

this_dir = os.path.dirname(swiss.__file__)
if this_dir == '': this_dir = '.'

# This must point to idelasm either through a pathname or the $PATH.
# invoke_assembler = "idelasm"
invoke_assembler = this_dir + "/../../bin/idelasm"

BORED_PORT = 48751
localhost = socket.gethostbyaddr(socket.gethostname())[0]


# TODO: stop when all tasks are done.  or something.

def loop():
    """Deal out jobs from the task list to jobbers, forever."""
    Dealer()
    asyncore.loop()


tasks = []
next_task = 0

def add_task(task):
    """Add task to the list of active tasks."""
    global tasks, pending_tasks
    tasks.append(task)
    if not pending_tasks.has_key(task):
	pending_tasks[task] = {}

def get_next_task():
    """Return the next task in round-robin order, if any."""
    global tasks, next_task
    if tasks == []:
	return None
    next_task = next_task % len(tasks)
    task = tasks[next_task]
    next_task = next_task + 1
    return task


pending_jobs = {}
pending_tasks = {}
exhausted_tasks = {}

def remove_pending_job(job_id, task, job):
    """Record that `job' (belonging to `task' and having id `job_id')
    has been done."""
    del pending_jobs[job_id]
    del pending_tasks[task][job]
    if pending_tasks[task] == {} and exhausted_tasks.has_key(task):
	del pending_tasks[task]
	task.clean_up()

def get_next_job(jobber_description):
    """Get a new job off the task list and return it as a pair of a
    job-id and corresponding job, if possible; else (None, None)."""
#    if 0 < len(pending_jobs) and next_task == len(tasks):
#	job_id, (task, job) = pending_jobs.items()[0] #TODO: cycle through them
#	return job_id, job
    task = get_next_task()
    if not task:
	return None, None
    job = task.get_job(jobber_description)
    if job:
	job_id = swiss.generate()
	pending_jobs[job_id] = (task, job)
	pending_tasks[task][job] = 1
    else:
	job_id = None
	global exhausted_tasks
	exhausted_tasks[task] = 1
    return job_id, job


class Dealer(asyncore.dispatcher):
    """A server that distributes jobs from a task list to jobbers."""
    
    def __init__(self, host=localhost, port=BORED_PORT):
        asyncore.dispatcher.__init__(self)
        self.create_socket(socket.AF_INET, socket.SOCK_STREAM)
        self.set_reuse_addr()
        self.bind(('', port))
        self.listen(5)

    def handle_accept(self):
	Session(self.accept())


class Session(asynchat.async_chat):
    """A session with a jobber."""

    def __init__(self, (conn, addr)):
        asynchat.async_chat.__init__(self, conn)
	self.addr = addr
        self.set_terminator('\n')
        self.buffer = ''
	self.collecting_output = 0
	self.output = ''

    def collect_incoming_data(self, data):
        self.buffer = self.buffer + data
        
    def found_terminator(self):
	global pending_jobs
        data = self.buffer
        self.buffer = ''
        print '<== received [%s]' % repr(data)
	if self.collecting_output:
	    self.output = self.output + data + '\n'    # ugh
	elif data[:9] == "I'm bored":
	    job_id, job = get_next_job(data[10:])
	    if not job:
		self.push("I can't help you\n")
		self.close_when_done()
		return
	    self.push("Here's a job: %s\n" % job_id)
	    job.push_code(self)
	    self.close_when_done()
	elif data[:9] == "It's done":
	    job_id = data[10:]
	    if pending_jobs.has_key(job_id):
		self.job_id = job_id
		self.task, self.job = pending_jobs[job_id]
		self.collecting_output = 1
	    else:
		print 'Bad key [%s]' % job_id
	else:
	    print 'Huh?'
	    self.push('Huh?\n')

    def handle_close(self):
        self.close()
	if self.collecting_output:
	    if self.job.answer_seems_ok(self.output):
		self.task.take_answer(self.job)
		remove_pending_job(self.job_id, self.task, self.job)
	    else:
		self.task.take_failure(self.job, self.addr, None)
	    
    def assemble_and_push(self, source_code):
	"""Assemble source_code (a string with idel assembly) and push
	the object code onto self, then close the session."""
	fromchild, tochild = popen2.popen2(invoke_assembler)
	tochild.write(source_code)
	tochild.close()
	assembled = fromchild.read()
	fromchild.close()
	self.push(assembled)
	self.close_when_done()
    #    os.waitpid(-1, os.WNOHANG)     # FIXME: or something


class JobberDescription:
    """A description of a jobber's advertised performance."""

    defaults = {
	'cycle_time'   : 1.0e-7,
	'data_space'   : 2**16,
	'stack_space'  : 2**12,
	'code_space'   : 2**16,
	'fuel'         : 1.0e8,
	'output_limit' : 2**16,
	'bandwidth'    : 2**15,
	'half_life'    : 300,       # typical seconds till interruption
    }

    def __init__(self, str):
	self.__dict__.update(self.defaults)
	for key, value in parse_properties(str):
	    if self.defaults.has_key(key):
		self.__dict__[key] = value
	    else:
		pass     # let it go, don't complain...

    def cycle_time(self):
	"""Return roughly the amount of time this jobber takes
	to execute one instruction."""
	return self.cycle_time


def parse_properties(str):
    """Convert a string of the form 'foo=great bar=awful' into a table
    like {'foo':'great', 'bar':'awful'}."""
    properties = {}
    for pair in string.split(str):
        x = string.split(pair, '=')
        if len(x) == 2:
            properties[x[0]] = x[1]
        else:
            pass     # let it go, don't complain...
    return properties
