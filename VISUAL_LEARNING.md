# Visual learning prototype (local, uncommitted)

Array, string and pointer/reference repair tests now use an explorable 3D data
scene in the Memory panel. Bitty stands inside the array and moves to the real
addresses read or written by the player's VM. Memory cells show values and
zero-based indexes; strings display characters and their null terminator.

The animation observes accesses already collected by Vm_TakeAccesses. It does
not run or substitute a solution, invent student variables, or change expected
results. Execution waits for the visible visit queue. Data tests retain line
pacing instead of switching to fast-forward after 32 lines. The queue shows
up to the VM's existing 16 most recent accesses per line; compound expressions
are therefore line-level visualisations, not a full instruction trace.

Power Audit (6.3): gold outlines mark the largest value among visited cells.
The student's actual max variable is shown separately. This exposes the faulty
max=0 starter on negative arrays. A passing test displays Bitty's short success
message, but the level still requires every existing test and the exit sequence.

Other scenes are data-driven: array traversal follows accesses, reverse shows
writes, strings show characters, and pointer tasks follow dereferenced memory.
Multiple test-data arguments can be viewed with DATA buttons while not running.
Pause/Play holds both the walk and further test execution during inspection.
The inspector continues to show actual variables and pointer arrows.

Long introductory speech and notes no longer open automatically. LESSON and its
ASK BITTY button keep those explanations available on demand. Existing movement
levels retain their computer-world scenes. Scalar-only repair tests retain the
existing register/gate representation; this prototype has not redesigned every
one of the 45 levels.

Files: src/visual_learning.c, src/visual_learning.h, src/main.c.
Checkpoint styling in src/circuit.c is a separate pre-existing local change.

Validation: full preview build passed; existing level suite 106 passed, 0 failed.
The preview was opened and Power Audit's code-driven movement and live max/index
were observed. No Git commit, push, or merge has been performed.

Try: build.bat, then RAM Banks -> stage 6.3 -> RUN. Watch the starter fail the
negative-number test. Change max's initial value to arr[0], run again, and verify
all four tests. Also try stage 6.5 for writes and 7.3 for string traversal.

Graphics update: data scenes now call World_DrawDataScene in world.c, sharing the original lighting shader, themed fog, stone material, hanging columns, environment towers/chips and additive glows. Values remain readable on outlined plates. The styled build compiled without warnings; launching bitty-visual-styled.exe through the automation tool was blocked by Windows, so this visual revision needs a local appearance check.

Challenge-flow revision: Bitty approaches index 0 on a stone runway and briefly introduces the task before coding. Power Audit's starter is now a function skeleton. The first case animates the student's memory accesses; remaining cases run in bounded per-frame batches and report compact badges. All cases must pass before completion. Saved student code is preserved; use the starter code button to load the new skeleton. Full build passed. The level suite could not be rerun after this revision because Windows blocked levels_test.exe. No commit, push or merge.

## Expanded scene coverage (local review required)

All 37 repair stages are now routed to visual-learning scenes instead of the
generic leap-year-style corridor. This is a shared-system first pass, not a
claim that every stage has received individual visual QA.

Memory operations: simultaneous rows, full destination capacity, free cells,
source/destination labels, actual pointer wires (including one-past markers),
write-value tokens, temporary/counter/total/max cards, and real last-operation
and branch displays. String comparisons and copies no longer hide one buffer.

Scalar operations: digit blocks and binary place values, grade output lanes,
month selection, clamp boundaries, character/code conversion, function input
and actual-return terminals, actual recursive call frames, and live registers.
Arithmetic and branch displays come from interpreter telemetry, not reference
solutions. Recursion views show the six most recent live frames and total depth.

Long traces are compressed after 32 scalar lines or 128 data lines so infinite
loops and very large prime checks can reach their instruction safety limits.
The visual trace remains line-level; it is not an instruction-by-instruction
animation of every intermediate assignment. Write tokens show observed writes,
not an invented provenance between equal-valued cells.

The 15 existing movement stages retain their suitable physical layouts.
Their optional sensor/loop/variable-transfer enhancements are not implemented
in this expansion. A polished swap choreography and full true/false flowchart
for every expression remain further refinement work.

Checks: every C source and the new VM visual regression test pass syntax checks
with -Wall -Wextra. Full linking and the VM regression run were blocked by the
Windows collect2.exe policy. New runtime/visual checks remain pending.
Use test_vm_visuals.bat, test_vm.bat, test_levels.bat and test_escape.bat on a
working compiler setup, then inspect representative stages 2.2, 2.3, 3.3, 3.5,
5.2, 5.5, 6.3, 6.5, 7.4, 7.5, 8.1, 8.3, 8.4 and 9.4 in game.

No commit, push, or merge has been performed.

Unified completion: movement exits, gate-based repairs and compact array validation now call CompleteStage, persist completion, reset victory time and unlock celebration feedback. The shared banner appears over all gameplay renderers. The data-scene avatar receives WORLD_WON and the real victory timer so it uses the original jump/spin/hop animation only after all checks pass. Next pulses when a successor exists; the final stage instead directs players to CIRCUIT. MSYS2 full build passed. No commit, push or merge.
