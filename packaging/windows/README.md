# Build Windows x64 natif

Le runtime produit est un exécutable Windows 10/11 x64 natif. MSYS2 sert uniquement de chaîne
de compilation et n'est pas nécessaire sur la machine du joueur après empaquetage.

## Pourquoi pas MSVC / le générateur Visual Studio

Le code invité PS4 utilise l'ABI x86-64 System V. Le chargeur dépend de
`__attribute__((sysv_abi))`, d'assembleur GNU et d'un `setjmp` Win64 spécialisé qui traverse des
trames invitées sans informations SEH. MSVC ne fournit pas ces mécanismes. Le port utilise donc
CMake/Ninja avec clang/MinGW-w64 dans **MSYS2 CLANG64**. Visual Studio peut servir d'éditeur, mais
son compilateur/linker ne doit pas être sélectionné pour cette cible.

## Dépendances de compilation

Activez d'abord les chemins longs Git (`git config --global core.longpaths true`) ; les fichiers
générés de FSR 3 dépassent sinon la limite classique de Windows. Installez MSYS2, ouvrez le terminal
**MSYS2 CLANG64**, puis exécutez :

```bash
pacman -S --needed git patch \
  mingw-w64-clang-x86_64-{toolchain,cmake,ninja,pkgconf,python} \
  mingw-w64-clang-x86_64-{vulkan-headers,vulkan-loader,vulkan-memory-allocator,glslang} \
  mingw-w64-clang-x86_64-{sdl3,fmt,boost,robin-map,xxhash,ffmpeg,zydis} \
  mingw-w64-clang-x86_64-{spirv-tools,spirv-headers,spirv-cross}
```

Depuis la racine du dépôt :

```bash
bash build.sh
bash build.sh --test
```

Ou depuis PowerShell :

```powershell
.\build-windows.ps1
.\build-windows.ps1 -Test
```

Le binaire Release est `out\ps4rt.exe`. Pour lancer avec une extraction personnelle :

```powershell
.\run.ps1 -Game "D:\Jeux\CUSA03219"
```

Le lanceur ajoute automatiquement `C:\msys64\clang64\bin` (ou
`$env:MSYS2_ROOT\clang64\bin`) au `PATH` d'un build source. Un paquet utilise en priorité les DLL
placées à côté de `ps4rt.exe`.

## Paquet autonome des bibliothèques

Dans MSYS2 CLANG64 :

```bash
bash packaging/windows/package.sh
```

Le ZIP versionné est créé sous `dist/` (par exemple
`ps4rt-star-ocean-5-windows-x64-v0.3.5.zip`). Il contient le runtime, toutes ses DLL CLANG64,
le préparateur C++ natif, l'extracteur PKG intégré avec ses bibliothèques privées, les ressources
visuelles du launcher et les assets de FSR 4. Le joueur n'a besoin ni de Python, ni de MSYS2,
ni de PowerShell : le runtime, le launcher et leurs bibliothèques sont fournis dans le ZIP.

Testez le ZIP depuis PowerShell avec un `PATH` dépourvu de MSYS2 :

```powershell
.\packaging\windows\smoke-test.ps1 .\dist\ps4rt-star-ocean-5-windows-x64-v0.3.5.zip
```

## Différences de plate-forme

- `mmap`/`mprotect` deviennent `VirtualAlloc2`, `MapViewOfFile3` et `VirtualProtect`.
- Les fautes mémoire et instructions non prises en charge passent par un gestionnaire d'exception
  vectorisé au lieu de `SIGSEGV`/`SIGILL`.
- Le TCB invité occupe un slot TLS du TEB Windows ; le chargeur réécrit l'accès `fs:[0]`.
- Les threads restent fournis par winpthreads, avec piles entièrement validées ; les temporisations
  courtes utilisent des waitable timers haute résolution.
- Les sockets utilisent WinSock2, l'aléatoire BCrypt, et les opérations de fichiers ont des
  équivalents Win32/CRT. Le manifeste active UTF-8, les chemins longs et le DPI PerMonitorV2.
- Le watchdog et les dumps par signaux restent Linux uniquement.

Ce port ne fournit ni fichiers de jeu, ni firmware, ni clés. L'extracteur PKG sous licence GPL-2.0
est intégré uniquement pour importer localement les paquets PS4 appartenant à l'utilisateur. Le
démarrage, le rendu Vulkan, l'audio WASAPI, l'entrée DualSense et la sauvegarde ont été validés
avec CUSA03219 ; une partie complète reste à valider.
