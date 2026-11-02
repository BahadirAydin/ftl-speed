Fast-forward for FTL: Faster Than Light (Steam, Windows). Press **PageUp** / **PageDown** in game to run at 1.5x, 2x, 3x, 4x, 6x or 8x; **Home** switches between 1x and the last fast speed.

### Install

1. Download **ftl-speed-{VERSION}.zip** below and extract it.
2. Double-click **Install.bat**. It finds FTL through Steam, or asks for the game folder (the one with `FTLGame.exe`).
3. Start FTL normally.

Or by hand: copy `dbghelp.dll` and `ftl_speed.ini` into the FTL folder (in Steam: right-click FTL > Manage > Browse local files).

To remove it, double-click **Uninstall.bat**, or delete `dbghelp.dll`, `ftl_speed.ini` and `ftl_speed.log` from the game folder.

Keys, speeds and the badge position are set in `ftl_speed.ini`. Tested with the Steam release of FTL 1.6 on Windows 10.

Some antivirus programs distrust unsigned DLLs that hook a game, so they may flag the plugin. This build was made and tested by GitHub Actions from the tagged source; see the README to build it yourself.

SHA-256 of `ftl-speed-{VERSION}.zip`: `{SHA256}`
