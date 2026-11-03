# Bitty's Great Escape

An interactive C/C++ learning game built in C with raylib.

Bitty, a tiny bot, is trapped inside a computer's memory and must escape before shutdown.
Players can't control Bitty directly: they write real C code, which runs live in the game.
Each region of memory teaches a C concept, and real C bugs (segfaults, memory leaks,
off-by-one errors) appear as in-game hazards.

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
