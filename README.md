# FTL Speed

Fast-forward for FTL: Faster Than Light (Steam, Windows). Press a key in game to
run at 1.5x, 2x, 3x, 4x, 6x or 8x. A small badge in the corner shows the speed.

| Key        | Action                                    |
|------------|-------------------------------------------|
| PageUp     | faster                                    |
| PageDown   | slower                                    |
| Home       | switch between 1x and the last fast speed |

Keys, speeds and badge position are set in `ftl_speed.ini` in the game folder.
The keys only act while the FTL window is focused. Pausing with Space still works
at any speed.

Tested with the Steam release of FTL 1.6 on Windows 10. It only touches the
running game: no game files, saves or settings are changed.

## Download and install

1. Download `ftl-speed-<version>.zip` from the
   [latest release](https://github.com/BahadirAydin/ftl-speed/releases/latest)
   and extract it.
2. Double-click `Install.bat`. It finds FTL through Steam, or asks for the game
   folder (the one with `FTLGame.exe`).
3. Start FTL normally from Steam.

To install by hand, copy `dbghelp.dll` and `ftl_speed.ini` into the FTL folder
(in Steam: right-click FTL > Manage > Browse local files). To remove the
plugin, double-click `Uninstall.bat`, or delete `dbghelp.dll`, `ftl_speed.ini`
and `ftl_speed.log` from the game folder. `ftl_speed.log` records what the
plugin did.

Some antivirus programs distrust unsigned DLLs that hook a game, so they may
flag the plugin. Release builds are made and tested by GitHub Actions from the
tagged source, and the whole plugin is `src/ftl_speed.c`; build it yourself if
you'd rather not trust a downloaded binary.

## Build from source

Needs [MSYS2](https://www.msys2.org/) with the UCRT64 GCC toolchain:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc
```

Then, in PowerShell from this folder:

```powershell
.\build.ps1        # writes build\dbghelp.dll and build\harness.exe
```

`build.ps1 -Toolchain <dir>` points it at a different `gcc.exe` folder.

Install or remove your build with:

```powershell
.\install.ps1              # finds FTL through Steam; or pass -GameDir "<folder with FTLGame.exe>"
.\install.ps1 -Uninstall
```

This copies `build\dbghelp.dll` and `ftl_speed.ini` into the FTL folder. Start
FTL normally from Steam afterwards.

## How it works

FTLGame.exe loads `dbghelp.dll` from its own folder first, so the plugin ships
under that name and forwards all real dbghelp calls to the Windows copy.

Each frame FTL advances the game by the time since the last frame, but never by
more than 31.25 ms. The plugin:

1. makes the game's clock (`QueryPerformanceCounter`) run N times faster,
2. shortens the game's `Sleep` calls by N so its frame limiter lets more frames through,
3. turns V-sync off while sped up (restored at 1x), so the monitor's refresh rate doesn't cap frames.

The game never sees a bigger time step than it would at a normal frame rate, so
fights play out the same, just faster. Music keeps its normal speed; only the
game simulation, animations and sound effect timing speed up.

The game can run at most (frames per second / 32) times faster. If your PC can't
draw enough frames, the badge turns amber and shows the real speed, e.g.
`>> 5.1/8x`. If the badge stays amber at 60 fps, your graphics driver may be
forcing V-sync on: set V-sync to "application controlled" / off for FTL in the
NVIDIA / AMD / Intel control panel.

The speed readout is measured from the game itself on the Steam 1.6 build; on
other builds the plugin still works, but the readout is an estimate.

## Testing

`build\harness.exe` loads the plugin into a test process and checks the clock
speed, the sleep scaling and the dbghelp forwarding. `harness.exe --host-check`
checks that nothing is hooked in programs other than FTLGame.exe.

## Releasing

GitHub Actions (`.github/workflows/build.yml`) builds and tests every push on
Windows and keeps the release zip as a run artifact. To publish a release:

1. Set the version in `PLUGIN_NAME` in `src/ftl_speed.c`, e.g.
   `"FTL Speed plugin 1.1"`, and push it.
2. Push the matching tag (`git tag v1.1`, `git push origin v1.1`), or run the
   Build workflow from the Actions tab with "Publish a release" checked.

The workflow then publishes `ftl-speed-<version>.zip` with install notes and a
SHA-256 checksum. The zip holds `dbghelp.dll`, `ftl_speed.ini`, `install.ps1`,
`Install.bat`, `Uninstall.bat`, `README.txt` (from `release/`) and the license.
`.\package.ps1` makes the same zip in `build\` after `build.ps1`.

## License

MIT. FTL: Faster Than Light is a game by Subset Games; this project is a fan
mod and is not affiliated with or endorsed by Subset Games.
