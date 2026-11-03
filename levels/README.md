# Writing levels

Every level is a text file in this folder. `levels.txt` lists them in play order.
To add a level: write the file, add its name to `levels.txt`, then run `test_levels.bat`
to check it loads and that its solution passes its own tests.

## Header

```
title: 3-1  Leap Year Gate
type: repair          escape = the player moves Bitty with code
                      repair = the player writes a function; each test is a gate
ban_indexing: yes     optional: arr[i] not allowed (Pointer Wires levels)
max_steps: 5000000    optional: instruction limit per test (default 2,000,000)
```

## Sections

| Section      | What it's for |
|--------------|---------------|
| `[story]`    | Shown in the console when the level loads. Bitty talking + the goal. |
| `[lesson]`   | The "Memory Dump" card: the C concept this level teaches. Keep it to about 14 short lines. |
| `[hint]`     | Shown in the console after the first failed run. |
| `[map]`      | Escape levels. Max 16 wide, 12 tall. Repair levels can leave it out and get a corridor of gates. |
| `[code]`     | Starter code in the editor. Must compile, and must NOT already pass the tests. |
| `[solution]` | A working answer. Never shown to the player; `test_levels.bat` checks it passes every test. |
| `[tests]`    | Repair levels. One test per line (see below). |

## Map letters

```
#  wall (protected memory)      .  empty memory
B  Bitty's start                E  exit
x  void (step in = segfault)    G  gate (repair levels)
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
