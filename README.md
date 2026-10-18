# FastPhysics

Scrap Mechanic mod. Replaces the exact constraint solver of the game's physics (`btDantzigSolver::solveMLCP`,
Bullet) with a faster implementation of the same algorithm. Big creations with lots of bearings, pistons and parts
pressing against each other spend 10-30 ms per tick in that one routine; this does the same work in 1-3 ms.
Nothing else in the simulation changes.

Mod page: https://www.nexusmods.com/scrapmechanic/mods/63

## How it works

The game solves every simulation island with `btMLCPSolver` + `btDantzigSolver` (Bullet 2.8x). It builds a dense
matrix `A` for the island (about 6 rows per bearing, 1 per contact normal plus 2 friction rows) and calls
`solveMLCP(A, b, x, lo, hi, limitDependency)`. The mod patches that vtable slot (the class is found through MSVC RTTI,
`.?AVbtDantzigSolver@@`), reads the matrix and vectors from the Bullet objects (layout checked on every call; anything
odd goes to the game's own routine) and solves with `src/fastlcp.cpp`:

- Dantzig's principal pivoting, ported from Bullet's `btDantzigLCP.cpp` (itself from ODE)
- AVX2/FMA kernels for dot products, triangular solves and the LDL^T factorisation (`src/lcpkernels.h`);
  pivots accumulate in double, the float version stalled on closed kinematic loops
- the unbounded rows (joints) and, with the warm start, the rows that were strictly inside their bounds on the previous
  tick are factorised as one block, then checked; wrong guesses are evicted, so the solution is exact either way
- the warm-start cache keys a problem by its structure (row count, friction dependencies, bound kinds), not by the
  bound values, which drift every tick
- on a numerical stall: retry cold, then in the game's row order, then hand the problem back to the game's solver

On one 45-body workshop mech (569-583 rows, 265 of them joints): game 10-14 ms per solve, Bullet's own code rebuilt
with AVX2 7 ms, this 1.0-1.3 ms warm / 1.5-2 ms cold. With six mechs in view the physics step went from 13 ms to
7.6 ms per tick.

## Layout

- `src/FastPhysics.cpp` - the DLL: hook, settings, warm-start cache, stats, log
- `src/fastlcp.cpp`, `src/fastlcp.h`, `src/lcpkernels.h` - the solver, standalone
- `tests/SelfTest.cpp` - loads `ScrapMechanic.exe` as an image and checks the hook target; solves generated problems in
  every kernel variant and validates them against the LCP conditions; runs the hook on fake Bullet objects
- `installer/` - player package: `Install.bat` / `Uninstall.bat`, PowerShell scripts, `FastPhysics.ini`, README
- `SMModLoader/` - the small loader the mod ships with (replaces `vcruntime140_1.dll` and loads `Release\DLLModules\*.dll`)
- `docs/` - the Nexus page text and images

## Building

Visual Studio 2022 with the C++ workload (`build.bat` finds it through vswhere).

```
build.bat                 builds bin\FastPhysics.dll and build\SelfTest.exe
build\SelfTest.exe        needs the game installed; pass the path to ScrapMechanic.exe if it is not in the default Steam folder
release.bat               builds the loader and the mod, runs the self-test, packs dist\
```

The DLL needs AVX2 and FMA at run time. Without them it logs that and stays inactive.

The DLL imports nothing but KERNEL32. It does not touch the network, your saves or any game file.

## Settings

`FastPhysics.ini` next to the DLL: `Enabled`, `WarmStart`, `LogIntervalSeconds`, and for troubleshooting `Verify`
(runs the game's solver too and logs the difference), `MinRows`, `DumpFailures`.

## Licence

zlib (see `LICENSE`). `src/fastlcp.cpp` is derived from Bullet Physics' `btDantzigLCP.cpp` (zlib), which is a port
of the Open Dynamics Engine LCP solver (BSD / LGPL dual licence); the file keeps the original notices.
