FTL Speed {VERSION}
===================

Fast-forward for FTL: Faster Than Light on Windows. In game, press:

  PageUp     faster (1.5x, 2x, 3x, 4x, 6x, 8x)
  PageDown   slower
  Home       switch between 1x and the last fast speed

A small badge in the corner shows the current speed.


Install
-------

1. Extract this zip (right-click it, "Extract All..."). Don't run the files
   from inside the zip.
2. Double-click Install.bat. It finds FTL through Steam; if it can't, it asks
   for the game folder (the one with FTLGame.exe).
3. Start FTL normally.

Or by hand: copy dbghelp.dll and ftl_speed.ini into the FTL game folder.
In Steam: right-click FTL, Manage, Browse local files.


Settings
--------

Keys, speeds and the badge position are set in ftl_speed.ini in the game
folder. Changes apply the next time FTL starts. ftl_speed.log in the same
folder records what the plugin did.


Remove
------

Double-click Uninstall.bat, or delete dbghelp.dll, ftl_speed.ini and
ftl_speed.log from the game folder.


Notes
-----

- The keys only act while the FTL window is focused. Pausing with Space still
  works at any speed.
- If the badge turns amber (e.g. ">> 5.1/8x"), your PC can't draw enough
  frames for the full speed. If it stays amber at 60 fps, set V-sync to
  "application controlled" or off for FTL in the NVIDIA / AMD / Intel
  control panel.
- No game files, saves or settings are changed.
- Some antivirus programs distrust unsigned DLLs that hook a game, so they
  may flag dbghelp.dll. The source is at
  https://github.com/BahadirAydin/ftl-speed if you'd rather build it yourself.

Tested with the Steam release of FTL 1.6 on Windows 10.

MIT license, see LICENSE.txt. FTL: Faster Than Light is a game by Subset
Games; this project is a fan mod and is not affiliated with or endorsed by
Subset Games.
