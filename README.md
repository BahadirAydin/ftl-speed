# FTL Speed

Fast-forward for **FTL: Faster Than Light**. Press a key while you play to
speed the game up to 1.5x, 2x, 3x, 4x, 6x or 8x, and press it again to go back
to normal. A small badge in the corner of the screen shows the current speed.

For the Steam version of FTL on Windows.

## How to install

1. Open the **[download page](https://github.com/BahadirAydin/ftl-speed/releases/latest)**
   and click the zip file under **Assets** (for example `ftl-speed-1.0.zip`)
   to download it.
2. Open your Downloads folder, right-click the zip file, choose
   **Extract All...** and click **Extract**.
3. In the folder that opens, double-click **Install.bat**.
   - If Windows asks whether you really want to run it, say yes. On a blue
     "Windows protected your PC" screen, click **More info**, then
     **Run anyway**.
   - It finds FTL by itself. If it can't, it asks where FTL is installed
     (see [Where is my FTL folder?](#where-is-my-ftl-folder) below).
4. When it says **FTL Speed installed**, press any key to close the window.
5. Start FTL from Steam as usual.

## How to use

While playing:

| Key       | What it does                                         |
|-----------|------------------------------------------------------|
| Page Up   | faster                                               |
| Page Down | slower                                               |
| Home      | switch between normal speed and the last fast speed  |

The keys only work while the FTL window is in front. Space still pauses the
game at any speed. Music plays at its normal speed; only the game speeds up.

## How to uninstall

Double-click **Uninstall.bat** in the folder you extracted.

Or delete these three files from your FTL folder: `dbghelp.dll`,
`ftl_speed.ini` and `ftl_speed.log`.

## Questions

### Where is my FTL folder?

In Steam, right-click FTL in your library and choose **Manage** >
**Browse local files**. The folder that opens (the one with `FTLGame.exe` in
it) is your FTL folder. If the installer asks for it, click the address bar at
the top of that folder window, copy the address and paste it into the
installer.

### Can I install it without Install.bat?

Yes. Copy `dbghelp.dll` and `ftl_speed.ini` from the zip into your FTL folder.

### Can I change the keys or the speeds?

Yes. Open `ftl_speed.ini` in your FTL folder with Notepad. It explains each
setting. Save it and restart FTL.

### The badge is orange and shows two numbers, like `>> 5.1/8x`

Your computer can't draw the game fast enough for the speed you picked. The
first number is the speed you are actually getting. Choose a lower speed.

If it turns orange even at 2x, your graphics card settings are probably
limiting the frame rate. Open your graphics card's settings (NVIDIA Control
Panel, AMD Software or Intel Graphics), find **Vertical sync** (V-sync) for
FTL and set it to **Off**, or to the option that lets the game decide.

### Will it change my saves or game files?

No. It only speeds up the game while it is running. Uninstalling it leaves FTL
exactly as it was.

### My antivirus warns about it

Some antivirus programs are suspicious of anything that changes how a game
runs. The release is built automatically by GitHub from the code in this
repository. If you'd rather not trust a downloaded file, you can skip it or
build it yourself (see below).

### Does it work on Mac or Linux?

No, only on Windows. It was tested with FTL 1.6 from Steam on Windows 10.

## For developers

### Build from source

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

This copies `build\dbghelp.dll` and `ftl_speed.ini` into the FTL folder.
`ftl_speed.log` in the game folder records what the plugin did.

### How it works

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

The game can run at most (frames per second / 32) times faster. If the PC can't
draw enough frames, the badge turns amber and shows the real speed, e.g.
`>> 5.1/8x`. If the badge stays amber at 60 fps, the graphics driver may be
forcing V-sync on.

The speed readout is measured from the game itself on the Steam 1.6 build; on
other builds the plugin still works, but the readout is an estimate.

### Testing

`build\harness.exe` loads the plugin into a test process and checks the clock
speed, the sleep scaling and the dbghelp forwarding. `harness.exe --host-check`
checks that nothing is hooked in programs other than FTLGame.exe.

### Releasing

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
