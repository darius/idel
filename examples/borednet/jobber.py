"""
A jobber runs jobs it gets from a dealer.
"""

import asyncore
import asynchat
import os
import popen2
import socket
import string
import time

import pq


this_dir = os.path.dirname(pq.__file__)
if this_dir == '': this_dir = '.'

# This must point to idelvm either through a pathname or the $PATH.
# invoke_idelvm = "idelvm"
invoke_idelvm = this_dir + "/../../bin/idelvm"

BORED_PORT = 48751
POLL_INTERVAL = 0.1            # seconds
MAX_POLL_INTERVAL = 300        # seconds
MAX_FAILURES = 175             # max consecutive failures before dropping a host
OUTPUT_THROTTLE = 16384        # bytes per job

agenda = pq.PriorityQueue()

intervals = {}
fail_count = {}

def register(host):
    global intervals
    if not intervals.has_key(host):
	good_host(host)      # provisionally

def good_host(host):
    global intervals, fail_count
    intervals[host] = POLL_INTERVAL
    fail_count[host] = 0
    add_host(host)

def bad_host(host):
    global intervals
    intervals[host] = min(2 * intervals[host], MAX_POLL_INTERVAL)
    fail_count[host] = fail_count[host] + 1
    if fail_count[host] < MAX_FAILURES:
	add_host(host)


def add_hosts(list):
    for h in list:
	register(h)

def add_host(h):
    global agenda, intervals
    if not agenda.contains(h):
	you_first = POLL_INTERVAL * agenda.population()
	agenda.append(h, time.time() + you_first + intervals[h])

def beg_next_host():
    global agenda
    priority, host = agenda.pop(1)
    time.sleep(max(0, priority - time.time()))
    beg(host)

def beg(host):
    JobClient(host, IdelJob())

counter = 0

class JobClient(asynchat.async_chat):
    """Asks a dealer for a job and sends back the result."""

    def __init__(self, host, runner):
        asynchat.async_chat.__init__(self)
        self.create_socket(socket.AF_INET, socket.SOCK_STREAM)
	self.connect((host, BORED_PORT))
	self.set_terminator('\n')
	self.host = host
	self.buffer = ''
        self.push("I'm bored\n")
	self.runner = runner
	self.state = 0
	global counter
	self.count = counter
	counter = counter + 1

    def handle_connect(self):
        pass

    def handle_error(self):
	self.state = 0
	t,v,tb = sys.exc_info()
	# FIXME: there's gotta be a better way to test this:
	if t == socket.error and v[1] in ['Connection refused', 'Broken pipe']:
	    # TODO: log the error
	    self.close()
	else:
	    asynchat.async_chat.handle_error(self)

    def collect_incoming_data(self, data):
	# print '<== data [%s]' % data
        self.buffer = self.buffer + data

    def found_terminator(self):
	data = self.buffer
	# print '<-- received [%s]' % data
	self.buffer = ''
	if data[:13] == "Here's a job:":
	    self.job_tag = data[14:]
	    self.set_terminator(None)
	    self.state = 1
	elif data[:7] == "Go bug ":
	    add_hosts(string.split(data[7:]))
	    self.handle_close()
	elif data == "I can't help you":
	    self.handle_close()
	else:
	    self.push("Huh?\n")
	    self.close_when_done()

    # We need to override close() instead of handle_close() because
    # asynchat's close_when_done() method doesn't call handle_close()
    # when done.  Fooey.
    def close(self):
        asynchat.async_chat.close(self)
	if self.state:
	    good_host(self.host)
	    self.runner.run(self.host, self.job_tag, self.buffer)
	else:
	    bad_host(self.host)
	beg_next_host()


class IdelJob:

    def run(self, host, job_tag, data):
	print 'run'
	fromchild, tochild = popen2.popen2(invoke_idelvm)
	tochild.write(data)
	tochild.close()
	output = fromchild.read()    # FIXME: how to get the error code?
	fromchild.close()
	conn = Reply(host)
	conn.push("It's done %s\n" % job_tag)
	conn.push(output[:OUTPUT_THROTTLE])
	conn.close_when_done()
    #    os.waitpid(-1, os.WNOHANG)     # FIXME: or something


class Reply(asynchat.async_chat):

    def __init__(self, host):
	asynchat.async_chat.__init__(self)
	self.create_socket(socket.AF_INET, socket.SOCK_STREAM)
	self.connect((host, BORED_PORT))

    def handle_connect(self):
	pass


if __name__ == '__main__':
    import os
    try:
	os.nice(19)     # only works in unix
    except:
	pass
    import sys
    add_hosts(sys.argv[1:])
    if agenda.population() != 0:
	beg_next_host()
        asyncore.loop()
