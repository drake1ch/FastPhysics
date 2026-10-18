# SM Mod Loader

A tiny loader for native (DLL) Scrap Mechanic mods, shared by Time Stop in Pause, Chest Hotkeys and
ComfortLogic. It is not released on its own: every mod package carries it and installs it.

It takes the place of `Release\vcruntime140_1.dll`, which `ScrapMechanic.exe` imports from its own
folder. Its three exports are forwarded to the renamed original `vcruntime140_1_orig.dll`
([src/SMModLoader.def](src/SMModLoader.def)), so the game works as before. When `ScrapMechanic.exe`
starts, the loader loads every `*.dll` from `Release\DLLModules` in alphabetical order and writes
what it loaded to `Release\SMModLoader.log`. In other programs that load the same DLL (the game's
editors) it does nothing, and it loads the mods only once per process.

`DLLModules` is the folder SM-DLL-Injector uses too, so a mod works with either loader.

## In the mod packages

Every mod package has `files\SMModLoader.dll` and [installer/modloader.ps1](installer/modloader.ps1)
as `files\modloader.ps1`; the mods' `package.ps1` take both from here. The mod's `install.ps1` calls
`Install-ModLoader`, which

- leaves SM-DLL-Injector alone if it is installed (`vcruntime140_1_.dll` exists);
- otherwise renames the original `vcruntime140_1.dll` to `vcruntime140_1_orig.dll` and puts the
  loader in its place, or replaces an installed loader only when the package has a newer version
  (`ProductVersion` from [src/version.h](src/version.h)), so the mods can be installed in any order
  and the newest loader wins;
- refuses to overwrite a `vcruntime140_1.dll` that is neither Microsoft's, nor the loader, nor a
  known old mod;
- moves the old standalone installs of Time Stop in Pause 1.0 (which was itself the
  `vcruntime140_1.dll` proxy) and Chest Hotkeys 1.0 (the `BugSplat64.dll` proxy) into `DLLModules`
  together with their ini files, and restores the original `BugSplat64.dll`. They are recognized by
  their version resource or, for builds without one, by strings in the file.

The mod's `uninstall.ps1` deletes only its own files and then calls `Uninstall-ModLoaderIfUnused`,
which removes the loader and restores the original `vcruntime140_1.dll` once no DLLs are left in
`DLLModules`.

## Project layout

| Path | What |
|---|---|
| [src/SMModLoader.cpp](src/SMModLoader.cpp) | the loader |
| [src/SMModLoader.def](src/SMModLoader.def) | the forwarded `vcruntime140_1.dll` exports |
| [src/SMModLoader.rc](src/SMModLoader.rc), [src/version.h](src/version.h) | version resource (the installers compare it) |
| [installer/modloader.ps1](installer/modloader.ps1) | install helpers shipped in every mod package |
| [tests/LoaderTest.cpp](tests/LoaderTest.cpp), [tests/TestMod.cpp](tests/TestMod.cpp) | loader test |
| [tests/InstallTest.ps1](tests/InstallTest.ps1) | installs the three mod packages together into fake game folders |
| [tools/make_nexus_images.py](tools/make_nexus_images.py) | draws the Nexus images of the three mods and of Trash Can (`docs/nexus/thumbnail.png` for the mod lists and `docs/nexus/cover.png` in each) in the game's style; reads the fonts and icons from the installed game, needs Pillow |

## Building and testing

Requires Visual Studio 2022 or newer with the "Desktop development with C++" workload.

```
build.bat
build\LoaderTest.exe bin\SMModLoader.dll build\TestMod.dll
```

`build.bat` produces `bin\SMModLoader.dll` and the tests; the mods' `release.bat` call it before
packing. The CRT is linked statically: the loader must not import `vcruntime140_1.dll`, which it
replaces.

`LoaderTest.exe` lays out a fake game folder in `%TEMP%` (a copy of itself named `ScrapMechanic.exe`
loads the loader) and checks the load order, that a broken file is skipped and logged, that a second
load changes nothing and that other programs are left alone.

After running `release.bat` of all three mods:

```
powershell -ExecutionPolicy Bypass -File tests\InstallTest.ps1
```

It installs the three packages from the mods' `dist\` folders into fake game folders in every order
and removes them in every order, checking after each step that the loader is in place exactly while
mods are installed and that each mod keeps its files and settings. It also covers installing a mod
again, a package with a newer loader (installed before or after the others), the old Time Stop in
Pause 1.0 / Chest Hotkeys 1.0, a file check in Steam between installs, another DLL mod in
`DLLModules`, SM-DLL-Injector and an unknown `vcruntime140_1.dll`. It also checks that every package
carries the current `SMModLoader.dll` and `modloader.ps1`. The real game folder is only read (to copy
the original `BugSplat64.dll`).

When the loader or `modloader.ps1` changes, raise the version in [src/version.h](src/version.h) (so
that installed copies get updated) and run `release.bat` of every mod.

Tested with the Steam version of the game, 1.0.6.889.
