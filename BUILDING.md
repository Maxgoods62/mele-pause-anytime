# Building MelePause

`build.sh` builds `MelePause.asi` from a clone of this repo and fetches the rest of the sources it needs (LExASIs and
Dear ImGui) itself. It runs in WSL and drives the Windows build tools through interop.

## What you need

| What | Notes |
|---|---|
| Windows with WSL | `build.sh` is a bash script. In WSL it uses `rsync` and `flock`; it calls `powershell.exe`, `reg.exe`, `tasklist.exe`, `vswhere.exe`, `git.exe` and CMake on the Windows side. |
| Git for Windows | `git.exe` must be on the `PATH` that WSL sees (WSL adds the Windows `PATH` to its own by default). The LExASIs and Dear ImGui checkouts live on the Windows disk, so `build.sh` runs Windows' git on them. |
| Visual Studio 18 (2026), or its Build Tools | With the C++ x64 build tools (`Microsoft.VisualStudio.Component.VC.Tools.x86.x64`) and the "C++ CMake tools for Windows" component. LExASIs' CMake preset uses the "Visual Studio 18 2026" generator, which older versions do not know. |
| Network on the first build | `build.sh` clones [ME3Tweaks/LExASIs](https://github.com/ME3Tweaks/LExASIs) with its submodules and fetches [Dear ImGui](https://github.com/ocornut/imgui). |
| Mass Effect Legendary Edition | Only for `--install` and `--uninstall`. |

`build.sh` finds Visual Studio with `vswhere.exe`: the newest install of version 18 with the C++ x64 tools. Set
`MELE_VS` to use another one.

## Build

```bash
git clone https://github.com/Maxgoods62/mele-pause.git
cd mele-pause
./build.sh
```

The result is `dist/MelePause.asi`, with its `MelePause.pdb`. To install it, see [Install](README.md#install).

| Command | What it does |
|---|---|
| `./build.sh` | Builds and stages `MelePause.asi` and `MelePause.pdb` in `MELE_STAGE`. |
| `./build.sh --install` | Builds, stages, then copies `MelePause.asi` into `Game\ME1\Binaries\Win64\ASI` of the game. Refuses while `MassEffect1.exe` runs ("built, not installed"). |
| `./build.sh --uninstall` | Removes `MelePause.asi` from that folder. Refuses while `MassEffect1.exe` runs. Builds nothing and needs no Visual Studio. |
| `./package.sh` | Builds into `dist/stage`, then writes `dist/MelePause-v<N>.zip` (the `.asi`, a `README.txt` and the license texts) and `dist/SHA256SUMS`. Needs `python3`. The zip is not to be published yet (see [THIRD_PARTY.md](THIRD_PARTY.md)). |

The game check uses `tasklist.exe`, and a failed `tasklist` counts as "running". `--install` copies into the existing
`ASI` folder, which the ASI loader uses; install the loader first.

## Settings

Every setting is optional. Set one in the environment, or copy `build.env.example` to `build.env` (ignored by git) and
uncomment the lines you need. A variable already set in the environment wins over `build.env`.

| Variable | Default | What it is |
|---|---|---|
| `MELE_LE_DIR` | the install folder from the registry | The Legendary Edition folder, as a Windows path. Only `--install` and `--uninstall` use it. |
| `MELE_VS` | found with `vswhere.exe` | The Visual Studio or Build Tools folder, as a WSL path. |
| `MELE_LEXASIS` | `%LOCALAPPDATA%\mele-asi-build\MelePause\LExASIs` | The LExASIs checkout, as a WSL path. Keep it on the Windows disk: CMake and MSBuild do not work well on `\\wsl.localhost` paths. |
| `MELE_STAGE` | `dist/` next to `build.sh` | Where `MelePause.asi` and its `.pdb` are copied. |

Without `MELE_LE_DIR`, `build.sh` reads the `Install Dir` value under `HKLM\SOFTWARE\BioWare` (the key whose name ends
in "Legendary Edition"), then Steam's uninstall entry for app 1328670. The folder must contain `Game\ME1`.

## What a build does

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

6. Configures with the tree's `VisualStudio` preset, LE1 only (`LEASI_BUILD_LE1` on, `LEASI_BUILD_LE2`,
   `LEASI_BUILD_LE3` and `LEASI_BUILD_LEL` off). A `LEASI_GAME1_WIN64` variable would make the LE1 build copy itself
   into the game. `build.sh` clears it for that step, and stops when the configure output says one is still set: one
   set in the Windows environment reaches CMake all the same.
7. Builds the `MelePause-LE1` target in the `RELEASE` configuration, with the tree's flags (`/W4 /WX`); Dear ImGui
   builds as a static library with its own defaults. On a failure it prints the error and warning lines.
8. Copies `MelePause.asi` and `MelePause.pdb` into `MELE_STAGE`.

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
| `Legendary Edition not found in the registry` or `no Game\ME1 in ...` | Set `MELE_LE_DIR` to the Legendary Edition folder, as a Windows path. |
| `MassEffect1.exe is running: close the game first` | Close the game and run the command again. |

The configure step, as `build.sh` runs it from the LExASIs folder. `<VS folder>` is the Visual Studio folder as a WSL
path, and `<third_party folder>` is the `third_party` folder next to the checkout as a Windows path with forward
slashes:

```bash
env -u LEASI_GAME1_WIN64 -u LEASI_GAME2_WIN64 -u LEASI_GAME3_WIN64 -u LEASI_GAMEL_WIN64 \
    '<VS folder>/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' --preset VisualStudio \
    -DLEASI_BUILD_LE1=ON -DLEASI_BUILD_LE2=OFF -DLEASI_BUILD_LE3=OFF -DLEASI_BUILD_LEL=OFF \
    '-DMELE_THIRD_PARTY=<third_party folder>'
```

## Version

The version is `ASI_VERSION` in `src/SharedVersion.h`. `VERSION_STRING_A` and `VERSION_STRING_W` next to it hold the
same number as text: the ASI loader logs the wide one, and the log's "MelePause N ready" line prints the other. Changes
per version are in [CHANGELOG.md](CHANGELOG.md).
