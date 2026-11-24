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

## Accounts and cloud saves

After the boot screen there is a sign-in screen:

- **NEW ACCOUNT** makes an account. It starts with whatever progress is already on this PC.
- **LOG IN** loads your progress, on any PC that has the game.
- **PLAY OFFLINE** skips accounts and uses `bitty_save.txt` like before.

Saves follow you between PCs through a free Firebase Realtime Database. The game talks to it
with `curl`, which comes with Windows 10 and 11, so there's nothing extra to install.
Passwords are never stored, only a SHA-256 hash of them.

To set it up (about 5 minutes, done once):

1. Go to https://console.firebase.google.com, sign in with a Google account and click **Create a project**
   (Google Analytics isn't needed).
2. In the left menu: **Build > Realtime Database > Create Database**. Pick the location, then **Start in test mode**.
3. Copy the database address shown at the top, e.g. `https://bitty-12345-default-rtdb.europe-west1.firebasedatabase.app`.
4. Copy `bitty_cloud.example.txt` to `bitty_cloud.txt` next to the game and put your address on the `url` line.

Test mode lets anyone who knows the address read and write, and Firebase turns it off after 30 days.
That's fine for a class project, but don't reuse a password you use anywhere else.
To keep it running, replace the rules (Realtime Database > Rules) with:

```json
{ "rules": { "users": { "$name": { ".read": true, ".write": true } } } }
```

Without `bitty_cloud.txt`, accounts still work but are kept on that PC only.
`release.bat` copies `bitty_cloud.txt` into the zip, so classmates get cloud saves automatically.

## Class labs (limited time)

For the rest of the semester, a **CLASS LABS** screen opens after sign-in (and from the CLASS LABS button
on the map). You pick between two cards, **C (Lab 1)** and **C++ (Lab 2)**. Each card lists the questions
from that Advanced Programming lab sheet, each one linked to the Bitty level that teaches it.
The offer ends on the `labs_end` date in `bitty_cloud.txt` (19 Dec 2026 if not set), and then the button disappears.

## Sound

Every sound effect is generated in code when the game starts (`src/sfx.c`), so there are no audio files.
**Ctrl+M** mutes and unmutes.

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
