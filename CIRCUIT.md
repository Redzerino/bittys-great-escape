# Bitty's Escape Circuit

Flow: intro -> title -> motherboard map -> click a playable stage -> coding.
The CIRCUIT toolbar button saves current code and returns to the map.
The map scales to the window, including F11 borderless mode.

Nine worlds, five slots each:
1. Boot Sector
2. Register Bank
3. CPU Control Unit
4. Processing Core
5. System Library
6. RAM Banks
7. Data Bus
8. Address Network
9. USB Gateway

Completed nodes show +, playable nodes are coloured, and unwritten stages have
padlocks. A white pointer marks the last played level. The right panel shows
the selected stage, and Enter opens it. Clicking an existing node opens it
directly. All existing stages remain available for revision; unwritten stages
cannot be opened. The top counter measures completion against all 45 planned
stages and separately shows how many stages are currently playable.

Progress and code use the team's save.h/save.c system keyed by level filename.
No level files or saves are renamed. The map groups older levels by topic:
- 2-0_first_function -> System Library slot 1
- 2-1_digit_peeler, 2-2_binary_decoder -> Register Bank slots 2 and 3
- 3-1_leap_year_gate -> CPU Control Unit slot 4
- 4-1_prime_lock -> Processing Core slot 5
- 5-1_search_the_bay, 5-2_mirror_mirror -> RAM Banks slots 4 and 5
- 8-1_swap_wires, 8-2_measure_the_string -> Address Network slots 3 and 5
Other files named W-S_name.txt use world W, stage S. Avoid allocating a new
file to an occupied slot; Circuit_Init logs a warning on collisions.

Implementation: src/circuit.h, src/circuit.c, and the integration in src/main.c.

Validation: all C sources pass GCC -fsyntax-only -Wall -Wextra; all 14 files
listed in levels/levels.txt exist and have distinct map positions. Full linking
is still blocked by Windows Application Control rejecting collect2.exe.
Visual verification remains necessary on a working build setup.
