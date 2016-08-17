"""
Swiss number generator.  A Swiss number is an unguessable token to
be handed out privately.  

Fake implementation.  For real, we should swap it with something
using /dev/random or EGD or some such.
"""


counter = 42

def generate():
    """Return a new Swiss `number' as a string."""
    global counter
    counter = counter + 1
    return `counter`
