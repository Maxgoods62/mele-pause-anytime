# Building MelePause

`build.sh` builds `LE1MelePause.asi`, MelePause for LE1, from a clone of this repo and fetches the rest of the sources
it needs (LExASIs and Dear ImGui) itself. It runs in WSL and drives the Windows build tools through interop.

## What you need

| What | Notes |
|---|---|
| Windows with WSL | `build.sh` is a bash script. In WSL it uses `rsync` and `flock`; it calls `powershell.exe`, `reg.exe`, `tasklist.exe`, `vswhere.exe`, `git.exe` and CMake on the Windows side. |
| Git for Windows | `git.exe` must be on the `PATH` that WSL sees (WSL adds the Windows `PATH` to its own by default). The LExASIs and Dear ImGui checkouts live on the Windows disk, so `build.sh` runs Windows' git on them. |
| Visual Studio 18 (2026), or its Build Tools | With the C++ x64 build tools (`Microsoft.VisualStudio.Component.VC.Tools.x86.x64`) and the "C++ CMake tools for Windows" component. LExASIs' CMake preset uses the "Visual Studio 18 2026" generator, which older versions do not know. |
| Network on the first build | `build.sh` clones [ME3Tweaks/LExASIs](https://github.com/ME3Tweaks/LExASIs) with its submodules and fetches [Dear ImGui](https://github.com/ocornut/imgui). |
| Mass Effect Legendary Edition | Only for `--install`, which also needs the ASI loader in the game, and `--uninstall`. |

`build.sh` finds Visual Studio with `vswhere.exe`: the newest install of version 18 with the C++ x64 tools. Set
`MELE_VS` to use another one.

## Build

```bash
git clone https://github.com/Maxgoods62/mele-pause.git
cd mele-pause
./build.sh
```

The result is `dist/LE1MelePause.asi`, with its `LE1MelePause.pdb`. To install it, see [Install](README.md#install).

| Command | What it does |
|---|---|
| `./build.sh` | Builds and stages `LE<n>MelePause.asi` and `LE<n>MelePause.pdb` in `MELE_STAGE`, for each game picked ([Games](#games)). |
| `./build.sh --game LE1` | The same for the games named, one `--game` each: `LE1`, `LE2`, `LE3` or `all`. It goes with `--install` and `--uninstall` too. |
| `./build.sh --install` | Checks each game ([Install checks](#install-checks)), builds and stages, checks again, then copies `LE<n>MelePause.asi` into `Game\ME<n>\Binaries\Win64\ASI` of the game. In ME1's folder it then deletes `MelePause.asi`, the file of version 6 and older. |
| `./build.sh --uninstall` | Removes `LE<n>MelePause.asi` from that folder, and `MelePause.asi` from ME1's. Refuses while the game runs. Builds nothing and needs no Visual Studio. |
| `./package.sh` | Builds into `dist/stage`, then writes `dist/LE<n>MelePause-v<N>.zip` for each game (the `.asi`, a `README.txt` and the license texts) and `dist/SHA256SUMS` over this version's zips in `dist/`. Takes `--game` as `build.sh` does. Needs `python3`. The zips are not to be published yet (see [THIRD_PARTY.md](THIRD_PARTY.md)). |

## Games

Each game gets its own ASI from the same sources, built against that game's SDK: the CMake target `MelePause-LE<n>`
makes `LE<n>MelePause.asi` and `LE<n>MelePause.pdb`. One version number covers every game.

A build covers the games named with `--game`, else those in `MELE_GAMES`, else the released ones (`RELEASED_GAMES` in
`scripts/games.sh`: LE1). A name is `LE1`, `LE2`, `LE3` or `all`, in any case. `all` means the games that
`src/CMakeLists.txt` lists on `asi_target`'s `GAMES` line, which is LE1 alone. A game that line does not list has no
project, and the build stops after the configure step: "no MelePause-LE2 project: src/CMakeLists.txt does not build LE2
yet (asi_target's GAMES)".

What differs from one game to the next is in `src/game/LE<n>.hpp`, included by `src/game/Game.hpp` for the build's SDK
target; today it has LE1's branch only and stops the build for any other target. A game goes on the `GAMES` line only
once it has its `LE<n>.hpp`, defining the names `LE1.hpp` defines (`LegacyModule` included: null for a game with no old
file name), and its branch in `Game.hpp`: the tree's `VisualStudio` preset turns every `LEASI_BUILD_LE<n>` on, so a
listed game that does not compile breaks the whole tree's build in Visual Studio.
`package.sh` packs a game only once `game_readme` in `scripts/games.sh` has its `README.txt` text.

## Install checks

`--install` and `--uninstall` find the Legendary Edition folder (`MELE_LE_DIR`, else the registry: see
[Settings](#settings)), then check each game picked before anything in it changes:

- The ASI loader, `bink2w64_original.dll` in `Game\ME<n>\Binaries\Win64` (`--install` only). `build.sh` never writes
  the loader: ME3Tweaks Mod Manager installs it with any mod.
- The `ASI` folder in `Game\ME<n>\Binaries\Win64` (`--install` only: without it `--uninstall` has nothing to remove
  there and goes on to the next game).
- The game is closed: `tasklist.exe` does not list `MassEffect<n>.exe`. A failed `tasklist` counts as "running".

`--install` runs the checks before the build, so a missing loader or a running game costs no build, and again after it,
before the first copy: a game started during the build stops it with "(built, not installed)". Then it copies
`LE<n>MelePause.asi` into each game's `ASI` folder and deletes `MelePause.asi` from ME1's: while that file is loaded,
`LE1MelePause.asi` stays off, since both would rewrite `Logs\MelePause.log`.

Inside the game install, `build.sh` writes only `LE<n>MelePause.asi`, and deletes only that file and ME1's
`MelePause.asi`.

## Settings

Every setting is optional. Set one in the environment, or copy `build.env.example` to `build.env` (ignored by git) and
uncomment the lines you need. A variable already set in the environment wins over `build.env`. `package.sh` reads the
same settings, but sets `MELE_STAGE` itself.

| Variable | Default | What it is |
|---|---|---|
| `MELE_GAMES` | the released games (`LE1`) | The games to build when no `--game` is given, separated by spaces: `LE1`, `LE2`, `LE3` or `all`. |
| `MELE_LE_DIR` | the install folder from the registry | The Legendary Edition folder, as a Windows path. Only `--install` and `--uninstall` use it. |
| `MELE_VS` | found with `vswhere.exe` | The Visual Studio or Build Tools folder, as a WSL path. |
| `MELE_LEXASIS` | `%LOCALAPPDATA%\mele-asi-build\MelePause\LExASIs` | The LExASIs checkout, as a WSL path. Keep it on the Windows disk: CMake and MSBuild do not work well on `\\wsl.localhost` paths. |
| `MELE_STAGE` | `dist/` next to `build.sh` | Where `LE<n>MelePause.asi` and its `.pdb` are copied. |

Without `MELE_LE_DIR`, `build.sh` reads the `Install Dir` value under `HKLM\SOFTWARE\BioWare` (the key whose name ends
in "Legendary Edition"), then Steam's uninstall entry for app 1328670. The folder must contain `Game`.

## What a build does

With `--install` or `--uninstall`, the [install checks](#install-checks) come first; `--uninstall` then removes the
files and stops. A build:

1. Finds Visual Studio and the CMake that ships with it.
2. Takes a lock on the build tree, `.lexasis-build.lock` next to the LExASIs checkout. A second build of the same tree
   (another clone or worktree with the same `MELE_LEXASIS`) waits for the first.
3. Checks the LExASIs checkout: it clones it on the first build, then checks out the pinned commit `aaad03a` with all
   its submodules (LExSDKv2, spdlog, simdutf). This check runs on every build, so an interrupted clone or a new pin is
   repaired.
4. Fetches Dear ImGui at the pinned commit `c1aaac65e896a45f81f47a4d68f9959e0f86c859` (v1.93.0) into `third_party\imgui`
   next to the checkout, and fetches it again if the pin moves.
5. Copies `src/` into the checkout as `LExASIs\MelePause` and, once, adds these lines to the end of the tree's
   `CMakeLists.txt`. Deleting `LExASIs\MelePause` takes the project out of the tree again.

   ```cmake
   if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/MelePause")
     add_subdirectory ("MelePause")
   endif ()
   ```

6. Configures with the tree's `VisualStudio` preset, with `LEASI_BUILD_LE<n>` on for each game picked and off for the
   others, and `LEASI_BUILD_LEL` off. A `LEASI_GAME<n>_WIN64` variable would make that game's build copy itself into
   the game. `build.sh` clears the four of them for that step, and stops when the configure output says one is still
   set: one set in the Windows environment reaches CMake all the same.
7. Checks that each game picked has its `MelePause-LE<n>` project, in `Build\MelePause` and in the solution
   `Build\LExASIs.slnx` (the project of a game listed before stays in `Build`).
8. Builds each `MelePause-LE<n>` target in the `RELEASE` configuration, with the tree's flags (`/W4 /WX`); Dear ImGui
   builds as a static library with its own defaults. On a failure it prints the error and warning lines and names the
   target.
9. Copies `LE<n>MelePause.asi` and `LE<n>MelePause.pdb` into `MELE_STAGE`. `--install` then checks each game again and
   copies `LE<n>MelePause.asi` into it; the `.pdb` stays in `MELE_STAGE`.

With the defaults, the build tree looks like this:

```
%LOCALAPPDATA%\mele-asi-build\MelePause\
  LExASIs\             ME3Tweaks/LExASIs at aaad03a; MelePause\ is the copy of src/
  third_party\imgui\   Dear ImGui at c1aaac65
  .lexasis-build.lock
```

## Your own edits in the build tree

The repair in step 3 runs when the checkout is not at the pinned commit or a submodule is missing or at another commit.
It keeps your own work:

- First it removes from `CMakeLists.txt` the `add_subdirectory` lines of `Mele...` folders, guarded or not, that builds
  like this one add. This build's lines come back in step 5.
- If `CMakeLists.txt` still differs from the commit after that, the build stops: "CMakeLists.txt in ... has changes of
  your own: commit or stash them, then build again".
- Then git checks out the pin. It keeps your changes to files the checkout does not touch, and refuses if they would be
  overwritten. The build then stops with "could not check out LExASIs ...". Commit or stash your changes there; with
  none, delete the folder and build again.

Edit the sources in this repo's `src/`, not in `LExASIs\MelePause`: each build replaces that folder with a copy of
`src/`.

## When something fails

| Message | What to do |
|---|---|
| `no vswhere.exe` or `vswhere found no Visual Studio 18 (2026) with the C++ x64 tools` | Install the Visual Studio 18 (2026) Build Tools with the C++ x64 tools, or set `MELE_VS`. |
| `no CMake in ...` | Add the "C++ CMake tools for Windows" component to that Visual Studio, or set `MELE_VS` to one that has it. |
| `no %LOCALAPPDATA% from PowerShell` | Set `MELE_LEXASIS` to a folder on the Windows disk. |
| `cmake configure failed: run it by hand in ...` | `build.sh` hides the configure output. Run the same step in that folder to see it (command below). |
| `a LEASI_GAME*_WIN64 variable is set in the Windows environment` | Remove that variable in System Properties, Environment Variables, then open a new terminal. |
| `unknown game '...': LE1, LE2, LE3 or all` | Use one of those names in `--game` and in `MELE_GAMES`. |
| `no MelePause-LE<n> project: src/CMakeLists.txt does not build LE<n> yet` | That game has no build ([Games](#games)): leave it out of `--game` and `MELE_GAMES`. |
| `MelePause-LE<n> did not build` | The lines above it are the compiler's errors and warnings. |
| `Legendary Edition not found in the registry` or `no Game folder in ...` | Set `MELE_LE_DIR` to the Legendary Edition folder, as a Windows path. |
| `no ASI loader in Game\ME<n>` or `no ASI folder in Game\ME<n>\Binaries\Win64` | Install the ASI loader into that game with ME3Tweaks Mod Manager (any mod installs it), then run the command again. |
| `MassEffect<n>.exe is running: close the game first` | Close the game and run the command again. After "(built, not installed)" the new build is already in `MELE_STAGE`. |

The configure step, as `build.sh` runs it from the LExASIs folder for LE1 (`=ON` for each game picked, `=OFF` for the
others). `<VS folder>` is the Visual Studio folder as a WSL path, and `<third_party folder>` is the `third_party` folder
next to the checkout as a Windows path with forward slashes:

```bash
env -u LEASI_GAME1_WIN64 -u LEASI_GAME2_WIN64 -u LEASI_GAME3_WIN64 -u LEASI_GAMEL_WIN64 \
    '<VS folder>/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' --preset VisualStudio \
    -DLEASI_BUILD_LE1=ON -DLEASI_BUILD_LE2=OFF -DLEASI_BUILD_LE3=OFF -DLEASI_BUILD_LEL=OFF \
    '-DMELE_THIRD_PARTY=<third_party folder>'
```

## Version

The version is `ASI_VERSION` in `src/SharedVersion.h`, one for every game. `VERSION_STRING_A` and `VERSION_STRING_W`
next to it hold the same number as text: the ASI loader logs the wide one, and the log's "LE<n>MelePause N ready" line
prints the other. `package.sh` stops if they differ. `src/MelePause.rc` gives each file the Windows version
`N.0.<game id>.<group id>`, as the LExASIs projects do: ME3Tweaks' game id (4, 5 and 6 for LE1, LE2 and LE3), and a
group id, 0 until ME3Tweaks assigns one. Changes per version are in [CHANGELOG.md](CHANGELOG.md).
