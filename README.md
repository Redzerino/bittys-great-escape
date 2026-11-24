# Bitty's Great Escape

An interactive C/C++ learning game built in C with raylib.

Bitty, a tiny bot, is trapped inside a computer's memory and must escape before shutdown.
Players can't control Bitty directly: they write real C code, which runs live in the game.
Each region of memory teaches a C concept, and real C bugs (segfaults, memory leaks,
off-by-one errors) appear as in-game hazards.

## Endless mode

The ENDLESS MODE button on the circuit map deals out random challenges, one after another,
covering every topic on the exam coverage sheet (Exam 1 and Exam 2): leap-year rules, digits,
primes, number bases, nested-loop patterns, arrays, strings, pointers, 2D arrays and linked lists.
Each challenge is a remix (a different rule, the other end of the array, a new pattern, fresh test
data) and there are no hints or gold gaps: you write the function from scratch.

- Clearing a challenge adds 1 to your streak; NEXT skips to a new one (and resets the streak).
- The best streak is kept in `bitty_endless.txt` next to the game.
- How it works: `src/endless.c` has one generator per topic. Each writes a hidden reference
  solution and runs it in the game's VM to work out the expected answers, then checks the
  challenge is fair (the answer passes, the empty starter fails) before handing it over.
- `test_endless.bat` builds hundreds of challenges and checks every one of them.

## Build (Linux)

```bash
make        # build
make run    # build and run
make clean  # remove build output
```

Requires raylib installed system-wide.

## Branches

- `main` - always builds and runs; used for screencasts and the demo
- `feature/<name>` - one branch per feature, merged into `main` by pull request
- Tags mark each deliverable, e.g. `v0.1-nov06`
