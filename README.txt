# BattleTech: The Crescent Hawk's Inception (C64) Static Recomp 1.0.0

Native Windows frontend and game-specific static core for BattleTech: The Crescent Hawk's Inception on the Commodore 64.

The original game disk images are not included. Supply legally obtained copies with these exact names and SHA-256 hashes:

- `BATTLET0.D64`: `135ac55a444ed8dcf5648b61cf3e1953e4dc342b135bbfcc6c550dd55a3af862`
- `BATTLET1.D64`: `1806a717ee66e6b67652ba7210e82fa150a000986c3ec9234e71fa2fc9ef4769`

## Running the application

Place both disk images in the `Rom` folder beside `Launcher.exe`, then run `Launcher.exe`. You can also use Browse to select another folder containing the verified pair. The application validates both images before enabling Run. The original D64 files are read-only; writable game data is stored separately in `Saves`.

Escape switches between the running game and Launcher. The Launcher provides controls for audio, video, input bindings, quick-save snapshots and game-frame screenshots. Press F1 in the Launcher for the complete shortcut guide.

The first launch displays the Welcome window by itself. Close it with Escape or the Close button to open the Launcher.

## What "fully static recompilation" means

The game's executable paths were analysed before the application was built and emitted as a fixed generated C catalog of 21,637 exact blocks. Compact metadata retains every block's identity, machine view, expected PC and opcode, successors and target classification. One native exact-block dispatcher executes only records admitted by that catalog.

The production runtime does not fetch arbitrary guest opcodes into a general-purpose interpreter loop and has no dynamic recompiler, JIT compiler, learning system, VICE process or emulator fallback. Dedicated 6510 IRQ/NMI entry does not pass through a normal opcode-fetch path. Unknown identities, machine views or targets stop with a diagnostic error instead of switching to fallback execution.

The runtime includes native C64 memory mapping, VIC-II graphics, CIA timers and input, SID 6581 audio synthesis, and 1541/VIA/IEC/GCR disk handling. Gameplay, graphics and sound remain live and respond to player input; they are not prerecorded.

Development tests and reference comparisons are used while building the project. They are not included in or used by the production runtime.

## Building from source on Windows

Requirements:

- 64-bit Windows 10 or later
- PowerShell
- MSYS2 MinGW-w64 installed at `C:\msys64\mingw64` by default
- The bundled official SDL 2.32.10 headers and static library under `ThirdParty`

From the `Source` folder, run:

```powershell
.\build-windows.ps1
```

The script compiles and runs its development checks in a temporary staging directory, then removes those temporary files. The current script produces only one distributable application, the stripped native `Launcher.exe`, with SDL statically linked. `Launcher.exe` starts another instance of itself in game mode, so no second game executable or `SDL2.dll` is required.

Version 1.0.0 contains the compact generated C64 game-code authority, dedicated interrupt entry, the game-specific native C64 hardware runtime, SID audio synthesis, exact two-disk media validation and fail-closed dispatch.
