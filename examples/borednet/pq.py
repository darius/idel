import bisect

class PriorityQueue:	
    def __init__(self):
	self.queue = []

    def append(self, data, priority):
	"""Append a new element to the queue according to its priority"""
	bisect.insort(self.queue, (priority, data))

    def pop(self, n):
	"""Pop the highest element of the queue. The n argument is
	here to follow the standard queue protocol """		
	return self.queue.pop(0)

    def contains(self, x):
	return x in self.queue

    def population(self):
	return len(self.queue)
