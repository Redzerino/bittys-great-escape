# Writing levels

Every level is a text file in this folder. `levels.txt` lists them in play order.
To add a level: write the file, add its name to `levels.txt`, then run `test_levels.bat`
(repair levels) and `test_escape.bat` (escape levels) to check it loads and that its
solution works.

## File names and the map

The file name decides where the level sits on the Escape Circuit map:

```
W-S_name.txt     world W (1-9), stage S (1-6)     e.g. 3-2_choose_a_path.txt = stage 3.2
0-S_name.txt     the optional BOOTCAMP, lesson S (1-7)
```

A chip shows a 6th stage only if a W-6 file exists. Every file is named after its
slot. If you ever rename one, add the old name to RENAMED in save.c so old saves
still find it.
Start the title with the map number so it matches: `title: 3.2  Choose a Path`.

## Header

```
title: 3.4  Leap Year Gate
type: repair          escape = the player moves Bitty with code
                      repair = the player writes a function; each test is a gate
ban_indexing: yes     optional: arr[i] not allowed (Pointer Wires levels)
max_steps: 5000000    optional: instruction limit per test (default 2,000,000)
door_code: 7          optional, escape levels: G tiles are locked doors that open when
                      Bitty write()s this number while standing next to them
fog: 1.4              optional, escape levels: Bitty can only see this far (in tiles),
                      so the player can't count how long the corridor is (use for while loops)
sign: 4 0 EXIT|11 TILES >>
                      optional, escape levels: a sign on the wall tile at column 4, row 0
                      (columns and rows count from 0; | starts a new line). Up to 6 signs.
buttons: EAST | NORTH | ==
                      optional: code buttons under the editor. Tapping one types it (into the
                      selected gold gap, if there is one, then jumps to the next gap).
```

Fill-in-the-blank: write ___ (three or more underscores) in the [code] starter
wherever the player should fill something in. Gaps are drawn in gold; clicking one
selects it, and Tab jumps to the next. A starter with gaps doesn't have to compile
(the level tests skip it), but make sure filling the gaps gives a working answer.
Use small gaps the first time an idea appears, and bigger ones (or none) later.

Repair levels need nothing extra for the memory room: while a test runs, Bitty is
beamed into the function's memory and acts out every line (see src/room.c).

## Sections

| Section      | What it's for |
|--------------|---------------|
| `[story]`    | Shown in the console when the level loads. Bitty talking + the goal. |
| `[talk]`     | What Bitty says out loud when the level starts, one speech bubble per line. `@walk EAST 2` makes him walk by himself first. Put C code in `` `backticks` `` to show it in gold. |
| `[lesson]`   | The "Memory Dump" card: the C concept this level teaches. Keep it to about 14 short lines. |
| `[hint]`     | One line per step: a nudge, a bigger nudge, then the answer. The first step shows after the first failed run; the HINT button shows the next. |
| `[solution]` (escape) | A working answer. `test_escape.bat` plays it and checks Bitty reaches the EXIT. |
| `[map]`      | Escape levels. Max 16 wide, 12 tall. Repair levels can leave it out and get a corridor of gates. |
| `[code]`     | Starter code in the editor. Must compile, and must NOT already pass the tests. |
| `[solution]` | A working answer. Never shown to the player; `test_levels.bat` checks it passes every test. |
| `[tests]`    | Repair levels. One test per line (see below). |

## Map letters

```
#  wall (protected memory)      .  empty memory
B  Bitty's start                E  exit
b  where Bitty arrives for his walk-in (optional; pair it with @walk lines in [talk] that end on B)
x  void (step in = segfault)    G  gate (repair levels) / locked door (with door_code)
0-9  a data tile holding that number (read() returns it)
```

## Tests

```
isLeapYear(1700) == false                      return value
compare_strings("apple", "banana") < 0         also != < >
find(5, {4, 9, 2, 8, 5}, 8) == 3               arrays in { }
strlen_ptr("Hello") == 5                       strings in " "
concat_strings("Hello, "[20], "World!") then #1 == "Hello, World!"
                                               [20] = room for 20 chars
reverse(5, {1, 2, 3, 4, 5}) then #2 == {5, 4, 3, 2, 1}
                                               then #n checks argument n afterwards
swap_ints(&5, &10) then #1 == 10, #2 == 5      &5 = a variable holding 5, passed by
                                               address (works for int* and int&)
```

Values: numbers, `true`/`false`, chars like `'A'`, strings, `{ }` arrays, `&number`.
Up to 12 tests per level. With more than 5 tests, the gates are shared out between them.
