#!/usr/bin/env python
## Roshambo game engine 1.0
## First cut by luke@bluetail.com

## TODO: Error handling

from sys import argv
from os import popen2, read

def game(rounds, program_a, program_b):
    """Run the Rock-Paper-Scissors game.

    `Rounds' is the number of rounds to play, and `program_{a,b}' are
    the names of the contestant programs.

    The engine uses the programs's standard input and output to
    communicate with them. The following protocol is repeated for each
    round:

    Each program writes a single character saying what it's move is
    ('r' for rock, 's' for scissors, or 'p' for paper).

    The engine then runs the round and gives the result to each
    process by writing two characters: the first is the result ('w'
    for win, 'l' for lose, 'd' for draw), and the second is the other
    player's move.

    When the game is over, engine closes the I/O pipes."""

    (a_out, a_in) = popen2(program_a)
    (b_out, b_in) = popen2(program_b)
    scores = { 'A': 0, 'B': 0, 'T': 0 }
    for i in range(rounds):
        move_a, move_b = a_in.read(1), b_in.read(1)
        result = winner(move_a, move_b)
        scores[result] += 1
        # Report result
        if   result == 'A': a_out.write('w'), b_out.write('l')
        elif result == 'B': a_out.write('l'), b_out.write('w')
        else:               a_out.write('d'), b_out.write('d')
        # Report other player's move
        a_out.write(move_b)
        b_out.write(move_a)
        a_out.flush(), b_out.flush()
        # Trace/debug
        print move_a+move_b, '=>', result

    print 'Results: %s: %s' % (program_a, scores['A']),
    print ' | %s: %s' % (program_b, scores['B']),
    print ' | Draws: %s' % scores['T']
    print

def winner(move_a, move_b):
    """Determine the winner, given the moves of A and B.

    Returns 'A', 'B', or 'T' (tie)"""
    # Table of move-pair -> winner (anything missing is a draw)
    table = { 'pr': 'A', 'rs': 'A', 'sp': 'A',
              'rp': 'B', 'sr': 'B', 'ps': 'B' }
    moves = move_a + move_b
    if moves in table:
        return table[moves]
    else:
        return 'T'

# Check arguments
if len(argv) != 4:
    print "Usage:", argv[0], "<rounds> <player1> <player2>"
    sys.exit(1)

game(int(argv[1]), argv[2], argv[3])

