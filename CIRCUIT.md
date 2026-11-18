# Bitty's Escape Circuit

Flow: intro -> title -> motherboard map -> click a playable stage -> coding.
The CIRCUIT toolbar button saves current code and returns to the map.
The map scales to the window, including F11 borderless mode.

Twelve worlds in four rows of three, up to eight stages each (a chip shows its
stages up to the last one written):
1. Boot Sector (5)
2. Register Bank (6)
3. CPU Control Unit (5)
4. Processing Core (8): loops, then nested-loop patterns (Exam 1 Q6, Q7, Q19)
5. System Library (6): + Palindrome Gate (Exam 1 Q2)
6. RAM Banks (8): + runner-up, frequency, duplicates (Exam 1 Q9, Q13, Q14)
7. Data Bus (8): + word counter, variable finder, scope scanner (Exam 1 Q18, Q21, Q22)
8. Address Network (7): + swap_chars, pointer count_vowels (Exam 2)
9. Array Workshop (5): copy, rotate, insert, delete, two movies (Exam 1 Q10-Q12, Q16, Q17)
10. Video Memory (4): 2D grids, PrintArray, clusters, gravity (Exam 1 Q19-Q21)
11. The Heap (7): structs, new, ->, linked lists, doubly linked lists (Exam 2 Part 2)
12. USB Gateway (5): the final escape (was world 9; RENAMED in save.c moves old saves)

Completed nodes show +, playable nodes are coloured, and unwritten stages have
padlocks. A white pointer marks the last played level. The right panel shows
the selected stage, and Enter opens it. Clicking an existing node opens it
directly. All existing stages remain available for revision; unwritten stages
cannot be opened. The top counter measures completion against all the stages
on the map and separately shows how many are currently playable.

Progress and code use the team's save.h/save.c system keyed by level filename.
Every level file is named after its slot: W-S_name.txt is world W, stage S
(bootcamp files are 0-S_name.txt). When the levels were put in teaching order
some files were renamed; save.c translates the old names in old save files
(see RENAMED in save.c), so nobody loses progress. Avoid giving two files the
same slot; Circuit_Init logs a warning on collisions.

Implementation: src/circuit.h, src/circuit.c, and the integration in src/main.c.

Bootcamp (optional): a strip at the top right with 7 lessons for people who have
never coded. Files 0-1 ... 0-7. They are not counted in the total, and a new player
starts on stage 1.1, with Bitty pointing them at the Bootcamp in his first talk.

All 74 stages now have a level. levels/levels.txt lists them in map order, so
NEXT > follows the same route as the map. Each level's 3D world uses the colour
of the chip it sits on (Circuit_WorldOf in circuit.c, themes in world.c).

Validation: test_levels.bat (repair levels: every solution passes, every starter
doesn't) and test_escape.bat (escape levels: every solution reaches the EXIT).
