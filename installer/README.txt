FASTPHYSICS - Scrap Mechanic mod
================================

Less lag from big creations. Same physics.

Scrap Mechanic solves the joints (bearings, pistons) and the contacts
of a creation with an exact solver. On a large creation, a walking
mech, a flying machine built on suspensions, anything with many parts
pressing against each other, that solver alone can take 10 to 30 ms
every physics tick, and the game stutters. FastPhysics replaces that
one routine with a faster implementation of the same algorithm. The
result is the same (it is checked and corrected on every tick); it is
just computed about 5 to 10 times faster.

What changes in the game: nothing you can see, except the frame rate
near big creations. Creations behave exactly as before: nothing gets
softer, nothing sinks, nothing jitters. Multiplayer: the physics runs
on the host, so the host needs the mod; clients do not.

Measured on one 45-body mech (Creative, same scene):
  game's solver: 10 to 14 ms per tick       with FastPhysics: 1 to 3 ms
  physics step:  13 to 17 ms per tick       with FastPhysics: 6 to 8 ms
Six mechs: the physics step went from 13 ms to 7.6 ms per tick, and
the stutter was gone. Ten mechs: 12 ms per tick with the mod; in the
same scene the game's own solver needed 7 to 44 ms (median 11) for a
single mech, every tick.


INSTALL
-------
1. Close the game.
2. Extract the whole archive and run Install.bat. (The game is found
   through Steam; if it is somewhere unusual, pass its folder:
   Install.bat "D:\Games\Scrap Mechanic".)
3. Start the game. There is nothing to set up.

Install.bat copies FastPhysics.dll and FastPhysics.ini into
Release\DLLModules and, if no other mod of mine is installed yet, sets
up the SM Mod Loader (a vcruntime140_1.dll that loads the DLLs from
Release\DLLModules; the original is kept as vcruntime140_1_orig.dll).
If you already use the SM-DLL-Injector, the mod goes into its
DLLModules folder and nothing else is touched.

Uninstall: run Uninstall.bat. It removes the mod, and the loader too if
no other mod uses it. "Verify integrity of game files" in Steam also
removes everything.

Manual install: copy files\FastPhysics.dll and files\FastPhysics.ini
into Release\DLLModules of any loader that loads DLLs from there.


REQUIREMENTS
------------
- Scrap Mechanic 1.0 (Steam, 64-bit). Tested with 1.0.6.
- A CPU with AVX2 and FMA (any Intel from 2013 / AMD from 2015 on). On
  an older CPU the mod stays inactive and the game runs as without it
  (FastPhysics.log says so).
- Windows 10 or 11.


SETTINGS
--------
Release\DLLModules\FastPhysics.ini (changes apply on the next start):

  Enabled=1             0 turns the mod off without uninstalling it.
  WarmStart=1           Start each tick from the previous tick's
                        contact set (checked, so the result is the
                        same). 0 solves every tick from scratch; it is
                        slower, there is no reason to change it.
  LogIntervalSeconds=30 How often a statistics line goes to the log.

Troubleshooting settings (Verify, MinRows, DumpFailures) are described
in the ini file itself.


FAQ
---
Does it change how my creations behave?
  No. It computes the same forces as the game, with the same algorithm
  (Dantzig's method, from the Bullet physics library the game uses),
  only faster: vector instructions, a smarter start on every tick, and
  the joints handled as one block. The mod checks its own result on
  every tick; if the fast solver ever cannot solve a problem, that tick
  is handed back to the game's own solver.

Why does it help with some creations and not others?
  The slow part the mod replaces only runs for creations the game
  treats as "big": many joints and many parts pressing against each
  other in one connected pile. Small cars and buildings never reach it,
  so the mod cannot speed them up. Lag from scripts, logic, rendering
  or lots of separate creations is not touched either.

Can I see what it is doing?
  Release\DLLModules\FastPhysics.log. Every LogIntervalSeconds it
  writes how many problems were solved, their size and timings, and
  how many went back to the game's solver (normally none).

Does it work in Survival / Challenge / Creative?
  Everywhere; it is the physics engine, not a game mode.

Multiplayer?
  The host simulates the physics, so the host needs it. Clients run
  their own prediction and benefit a little from having it too.
  Nothing is sent over the network differently.

It did not install / the game does not start.
  Run Install.bat as administrator if the game folder is protected.
  If the game crashes at start, remove Release\DLLModules\FastPhysics.dll
  and send me FastPhysics.log and SMModLoader.log (next to the game's
  exe) in the mod's Bugs tab.


SOURCE
------
The source code (C++, builds with Visual Studio) is in the optional
file on the mod page. The solver is derived from Bullet Physics'
btDantzigLCP (zlib licence, itself from the Open Dynamics Engine, BSD
licence).
