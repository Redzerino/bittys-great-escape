# Startup animation

The startup screen appears before the editor and does not change level progress.

Sequence (7.5 seconds): power-on checks, Bitty waking up, a short circuit run,
then a title reveal. The title waits for player input. Bitty uses the same
3D drawing and saved appearance as gameplay. The chime is generated locally;
no external media assets are required. The intro continues without audio when
an audio device is unavailable.

Controls:
- Enter, Space or Escape during the intro: skip to the title.
- Enter, Space or clicking the Start button on the title: enter the game.
- R on the title: replay the intro.
- M during startup/title: toggle the chime's sound.

Build normally with build.bat (Windows) or make (Linux).

Files:
- src/startup.c and src/startup.h: animation, title screen, input and audio.
- src/main.c: startup lifecycle and transition into the existing game.
- src/game_api.h and src/world.c: shared drawing entry point for Bitty.

Verification: Windows GCC/raylib compilation and git diff whitespace check
passed. The user built and ran the startup animation locally and confirmed its appearance.
Additional checks for future changes: skip-to-title, replay, Start, muted audio,
and saved accessories.
