PS4RT - Star Ocean 5 - Windows x64 - Version 0.3.5
==================================================

No game files are included. Use your own decrypted and extracted CUSA03219 directory containing
eboot.bin, sce_module, and sce_sys.

Player requirements
- Windows 10/11 x64 and an x86-64 processor supporting the instructions checked at startup.
- A recent graphics driver with Vulkan 1.3 support.
- No Python, MSYS2, PowerShell, or other external runtime is required.
- The required C/C++ DLLs are included next to the executables.

Recommended launch method
  Double-click PS4RT-Launcher.exe.

The Cockpit launcher can select and remember CUSA03219, prepare the game, and start it without a
visible PowerShell window. It also controls display settings, FSR, render resolution, frame-rate
limits, VSync, anisotropic filtering, and asynchronous shaders. It shows the latest save, can create
a backup before every launch, supports a custom save directory, and can export diagnostics.

The PKG selection button uses PS4RT-PkgExtractor.exe and the integrated ShadPS4Plus extractor.
By default, the extracted content is placed in:
  %LOCALAPPDATA%\PS4RT\Games\Imports\<package-name>-<date>
The launcher asks for confirmation, lets the user choose and remember the extraction directory,
shows timed progress, and can cancel while cleaning up partial files. The extracted CUSA03219
directory is detected and remembered automatically. Extraction runs without a visible console,
and its English messages are available under Logs & diagnostics.
Only use packages for which you legally own a copy.

By default, prepared files, settings, saves, and caches are stored under
out\CUSA03219\. The save directory can be customized on the Game tab.
The integrated native C++ preparer creates the working image without modifying the original game
directory. PS4RT-Launcher.exe, ps4rt.exe, and PS4RT-PkgExtractor.exe must remain together in the
same directory. The tools\pkg-extractor subdirectory must also remain in the package.
The program is experimental and has not been validated across the entire game or on every GPU.
