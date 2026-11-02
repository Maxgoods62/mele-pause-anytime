# Building Pause Anytime

`build.sh` builds Pause Anytime from a clone of this repo: `LE1PauseAnytime.asi`, `LE2PauseAnytime.asi` and
`LE3PauseAnytime.asi` ([Games](#games)). It downloads the other sources it needs, LExASIs and Dear ImGui, by itself. It
runs in WSL and calls the Windows build tools through interop.

## What you need

| What | Notes |
|---|---|
| Windows with WSL | `build.sh` is a bash script. In WSL it uses `rsync` and `flock` when they are present. |
| Git for Windows | `git.exe` must be on the `PATH` that WSL sees. WSL adds the Windows `PATH` to its own by default. |
| Visual Studio 18 (2026), or its Build Tools | With the C++ x64 build tools and the "C++ CMake tools for Windows" component. |
| Network on the first build | `build.sh` clones [ME3Tweaks/LExASIs](https://github.com/ME3Tweaks/LExASIs) with its submodules and downloads [Dear ImGui](https://github.com/ocornut/imgui). |
| Mass Effect Legendary Edition | Only for `--install` and `--uninstall`. `--install` also needs the ASI loader in the game. |

On the Windows side, `build.sh` calls `powershell.exe`, `reg.exe`, `tasklist.exe`, `vswhere.exe`, `git.exe` and CMake.
The LExASIs and Dear ImGui checkouts live on the Windows disk, so `build.sh` runs Windows' git on them. The `git clone`
under [Build](#build) and `package.sh` use the git of WSL: `package.sh` names the commit in the zip's `README.txt`.

Git Bash also works, and GitHub's build uses it ([docs/releasing.md](docs/releasing.md)). Without `rsync`, each build
copies all of `src/` again. Without `flock`, the build takes no lock, so two builds of the same tree do not wait for
each other.

The sources use the DirectX headers and libraries of the Windows SDK. LExASIs' CMake preset uses the "Visual Studio 18
2026" generator, which older versions of Visual Studio do not know.

`build.sh` finds Visual Studio with `vswhere.exe`: the newest install of version 18 with the C++ x64 tools
(`Microsoft.VisualStudio.Component.VC.Tools.x86.x64`). It checks for that component only, not for the SDK. Set `MELE_VS`
to use another install.

The ASI uses the shared C++ runtime (`MSVCP140.dll`, `VCRUNTIME140.dll`). Anyone who runs your build needs a
[Microsoft Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe) no older than the MSVC tools you built
with. An older `msvcp140.dll` can make the ASI crash.

## Build

```bash
git clone https://github.com/Maxgoods62/mele-pause-anytime.git
cd mele-pause-anytime
./build.sh
```

The result is `dist/LE1PauseAnytime.asi`, `dist/LE2PauseAnytime.asi` and `dist/LE3PauseAnytime.asi`, each with its
`.pdb`. To install them, run `./build.sh --install`, or copy them by hand as [docs/install.md](docs/install.md) says.

| Command | What it does |
|---|---|
| `./build.sh` | Builds `LE<n>PauseAnytime.asi` and its `.pdb` for each game picked, and copies both to `MELE_STAGE`. |
| `./build.sh --game LE1` | Builds only the games named, one `--game` each ([Games](#games)). Also with `--install` and `--uninstall`. |
| `./build.sh --install` | Builds, then copies `LE<n>PauseAnytime.asi` into each game ([Install checks](#install-checks)). |
| `./build.sh --uninstall` | Removes `LE<n>PauseAnytime.asi` from each game. Builds nothing and needs no Visual Studio. |
| `./package.sh` | Builds into `dist/stage`, then packs one zip and `dist/SHA256SUMS` ([Package](#package)). |

## Package

`./package.sh` builds with `build.sh` into `dist/stage`. Then it packs one zip, `dist/PauseAnytime-<label>.zip`, and
writes `dist/SHA256SUMS` over it. It installs nothing into the game.

- The label is `v<N>` by default, where N is `ASI_VERSION`. `--label latest` names the zip `PauseAnytime-latest.zip`.
- A `v<N>` label must match `ASI_VERSION`, and its zip must hold every released game.
- `--game` picks the games as in `build.sh`.
- The zip holds one `README.txt` for all the games, a folder per game with its `.asi` (`LE1\LE1PauseAnytime.asi`, ...),
  and a `LICENSE` folder with the license texts. The `.pdb` files stay in `dist/stage`.
- `package.sh` needs `python3`.
- [docs/releasing.md](docs/releasing.md) says how GitHub publishes the zip.

## Games

Each game gets its own ASI from the same sources, built against that game's SDK. The CMake target `PauseAnytime-LE<n>`
makes `LE<n>PauseAnytime.asi` and `LE<n>PauseAnytime.pdb`. One version number covers every game.

A build covers the games named with `--game`. Without `--game`, it covers those in `MELE_GAMES`, and without that, the
released games (`RELEASED_GAMES` in `scripts/games.sh`: LE1, LE2 and LE3). A name is `LE1`, `LE2`, `LE3` or `all`, in
upper or lower case. `all` means the games on the `GAMES` line of `asi_target` in `src/CMakeLists.txt`: LE1, LE2 and
LE3. A game missing from that line has no project, and the build stops after the configure step: "no PauseAnytime-LE<n>
project: src/CMakeLists.txt does not build LE<n> yet (asi_target's GAMES)".

A plain `./build.sh --install` installs all three released games. If any of them has no ASI loader or is running,
`build.sh` stops before it builds anything. To install one game only, add `--game LE1` (or `LE2`, `LE3`), or set
`MELE_GAMES`.

### Adding a game

What differs per game is in `src/game/LE<n>.hpp`. `src/game/Game.hpp` includes the right file for the build's SDK
target, and stops the build for any other game. Before you add a game to the `GAMES` line of `asi_target`:

1. Write its `src/game/LE<n>.hpp`, with every name that `LE1.hpp` defines.
2. Add its branch to `Game.hpp`.
3. Add its `README.txt` text to `game_readme` in `scripts/games.sh`. `package.sh` packs no game without that text.

Do steps 1 and 2 first. The tree's `VisualStudio` preset turns every `LEASI_BUILD_LE<n>` on, so a listed game that does
not compile breaks the whole tree's build in Visual Studio.

## Install checks

`--install` and `--uninstall` find the Legendary Edition folder (`MELE_LE_DIR`, else the registry: see
[Settings](#settings)). Then they check each game picked, before anything in it changes:

- The ASI loader, `bink2w64_original.dll` in `Game\ME<n>\Binaries\Win64` (`--install` only). `build.sh` never writes
  the loader: ME3Tweaks Mod Manager installs it with any mod.
- The `ASI` folder in `Game\ME<n>\Binaries\Win64` (`--install` only). Without it, `--uninstall` has nothing to remove
  there and goes on to the next game.
- The game is closed: `tasklist.exe` does not list `MassEffect<n>.exe`. A failed `tasklist` counts as "running".

`--install` runs the checks twice. The first time is before the build, so a missing loader or a running game stops
`build.sh` before any build time is spent. The second time is after the build, just before the first copy: if a game
was started during the build, `build.sh` stops with "(built, not installed)".

Then `--install` copies `LE<n>PauseAnytime.asi` into each game's `ASI` folder, and `--uninstall` deletes it from there.
Inside the game install, `build.sh` writes and deletes only that file.

## Settings

Every setting is optional. Set one in the environment, or copy `build.env.example` to `build.env` (ignored by git) and
uncomment the lines you need. A variable already set in the environment wins over `build.env`. `package.sh` reads the
same settings, but sets `MELE_STAGE` itself.

| Variable | Default | What it is |
|---|---|---|
| `MELE_GAMES` | the released games (`LE1 LE2 LE3`) | The games to build when no `--game` is given, separated by spaces: `LE1`, `LE2`, `LE3` or `all`. |
| `MELE_LE_DIR` | the install folder from the registry | The Legendary Edition folder, as a Windows path. Only `--install` and `--uninstall` use it. |
| `MELE_VS` | found with `vswhere.exe` | The Visual Studio or Build Tools folder, as a WSL path. |
| `MELE_LEXASIS` | `%LOCALAPPDATA%\mele-asi-build\PauseAnytime\LExASIs` | The LExASIs checkout, as a WSL path, on the Windows disk. |
| `MELE_STAGE` | `dist/` next to `build.sh` | Where `LE<n>PauseAnytime.asi` and its `.pdb` are copied. |

Keep `MELE_LEXASIS` on the Windows disk: CMake and MSBuild do not work well on `\\wsl.localhost` paths.

Without `MELE_LE_DIR`, `build.sh` reads the `Install Dir` value under `HKLM\SOFTWARE\BioWare` (the key whose name ends
in "Legendary Edition"), then Steam's uninstall entry for app 1328670. The folder must contain `Game`.

## What a build does

With `--install` or `--uninstall`, the [install checks](#install-checks) come first. `--uninstall` then removes the
files and stops. A build:

1. Finds Visual Studio and the CMake that ships with it.
2. Takes a lock on the build tree, `.lexasis-build.lock` next to the LExASIs checkout. A second build of the same tree
   (another clone or worktree with the same `MELE_LEXASIS`) waits for the first.
3. Checks the LExASIs checkout. It clones it on the first build, then checks out the pinned commit `aaad03a` with all
   its submodules (LExSDKv2, spdlog, simdutf). This check runs on every build, so it repairs an interrupted clone or a
   new pin.
4. Downloads Dear ImGui at the pinned commit `c1aaac65e896a45f81f47a4d68f9959e0f86c859` (v1.93.0) into
   `third_party\imgui` next to the checkout. It downloads it again if the pin moves.
5. Copies `src/` into the checkout as `LExASIs\PauseAnytime`. If the tree's `CMakeLists.txt` does not have these lines
   yet, it adds them at the end. Deleting `LExASIs\PauseAnytime` takes the project out of the tree again.

   ```cmake
   if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PauseAnytime")
     add_subdirectory ("PauseAnytime")
   endif ()
   ```

6. Configures with the tree's `VisualStudio` preset: `LEASI_BUILD_LE<n>` on for each game picked and off for the others,
   and `LEASI_BUILD_LEL` off. A `LEASI_GAME<n>_WIN64` variable would make that game's build copy itself into the game,
   so `build.sh` unsets all four of them for this step: `LEASI_GAME1_WIN64`, `LEASI_GAME2_WIN64`, `LEASI_GAME3_WIN64`
   and `LEASI_GAMEL_WIN64`. A variable set in the Windows environment still reaches CMake, so `build.sh` stops if the
   configure output shows one.
7. Checks that each game picked has its `PauseAnytime-LE<n>` project, both in `Build\PauseAnytime` and in the solution
   `Build\LExASIs.slnx`. A project from an earlier build, for a game that was listed then, can still be in `Build`.
8. Builds each `PauseAnytime-LE<n>` target in the `RELEASE` configuration, with the tree's flags (`/W4 /WX`). Dear ImGui
   builds as a static library with its own defaults. On a failure, `build.sh` prints the error and warning lines and
   names the target.
9. Copies `LE<n>PauseAnytime.asi` and `LE<n>PauseAnytime.pdb` into `MELE_STAGE`. `--install` then checks each game again
   and copies `LE<n>PauseAnytime.asi` into it. The `.pdb` stays in `MELE_STAGE`.

With the defaults, the build tree looks like this:

```
%LOCALAPPDATA%\mele-asi-build\PauseAnytime\
  LExASIs\             ME3Tweaks/LExASIs at aaad03a; PauseAnytime\ is the copy of src/
  third_party\imgui\   Dear ImGui at c1aaac65
  .lexasis-build.lock
```

## Your own edits in the build tree

The repair in step 3 runs when the checkout is not at the pinned commit, or when a submodule is missing or at another
commit. It keeps your own work:

- First it removes from `CMakeLists.txt` the `add_subdirectory` lines that this build and similar builds add: those
  for `PauseAnytime` and `Mele...` folders, with or without the `if (EXISTS ...)` guard. Step 5 adds this build's
  lines back.
- If `CMakeLists.txt` still differs from the commit after that, the build stops: "CMakeLists.txt in ... has changes of
  your own: commit or stash them, then build again".
- Then git checks out the pinned commit. Your changes to files that the checkout does not touch are kept. If a change
  would be overwritten, git refuses and the build stops with "could not check out LExASIs ...". Commit or stash your
  changes in that checkout. If you have no changes to keep, delete the folder and build again.

Edit the sources in this repo's `src/`, not in `LExASIs\PauseAnytime`: each build replaces that folder with a copy of
`src/`.

## When something fails

| Message | What to do |
|---|---|
| `no vswhere.exe` or `vswhere found no Visual Studio 18 (2026) with the C++ x64 tools` | Install the Visual Studio 18 (2026) Build Tools with the C++ x64 tools, or set `MELE_VS`. |
| `no CMake in ...` | Add the "C++ CMake tools for Windows" component, or set `MELE_VS` to a Visual Studio that has it. |
| `no %LOCALAPPDATA% from PowerShell` | Set `MELE_LEXASIS` to a folder on the Windows disk. |
| `cmake configure failed: run it by hand in ...` | `build.sh` hides the configure output. Run the same step in that folder to see it (command below). |
| `a LEASI_GAME*_WIN64 variable is set in the Windows environment` | Remove that variable in System Properties, Environment Variables, then open a new terminal. |
| `unknown game '...': LE1, LE2, LE3 or all` | Use one of those names in `--game` and in `MELE_GAMES`. |
| `no PauseAnytime-LE<n> project: src/CMakeLists.txt does not build LE<n> yet` | That game has no build ([Games](#games)): leave it out of `--game` and `MELE_GAMES`. |
| `PauseAnytime-LE<n> did not build` | The lines above it are the compiler's errors and warnings. |
| `Legendary Edition not found in the registry` or `no Game folder in ...` | Set `MELE_LE_DIR` to the Legendary Edition folder, as a Windows path. |
| `no ASI loader in Game\ME<n>` or `no ASI folder in Game\ME<n>\Binaries\Win64` | Add the ASI loader with ME3Tweaks Mod Manager (any mod installs it), then run the command again. |
| `MassEffect<n>.exe is running: close the game first` | Close the game and run the command again. After "(built, not installed)", the build is in `MELE_STAGE`. |

To see the configure output, run this command in the LExASIs folder. It is the command `build.sh` runs for an LE1-only
build: use `=ON` for each game you build and `=OFF` for the others. `<VS folder>` is the Visual Studio folder as a WSL
path. `<third_party folder>` is the `third_party` folder next to the checkout, as a Windows path with forward slashes.

```bash
env -u LEASI_GAME1_WIN64 -u LEASI_GAME2_WIN64 -u LEASI_GAME3_WIN64 -u LEASI_GAMEL_WIN64 \
    '<VS folder>/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' --preset VisualStudio \
    -DLEASI_BUILD_LE1=ON -DLEASI_BUILD_LE2=OFF -DLEASI_BUILD_LE3=OFF -DLEASI_BUILD_LEL=OFF \
    '-DMELE_THIRD_PARTY=<third_party folder>'
```

## Version

The version is `ASI_VERSION` in `src/SharedVersion.h`, one number for every game. `VERSION_STRING_A` and
`VERSION_STRING_W` next to it hold the same number as text. The ASI loader logs the wide one, and the log's
"LE<n>PauseAnytime N ready" line prints the other. `package.sh` stops if they differ.

`src/PauseAnytime.rc` gives each file the Windows version `N.0.<game id>.<group id>`, as the LExASIs projects do:

- The game id is ME3Tweaks' id for the game: 4, 5 and 6 for LE1, LE2 and LE3.
- The group id is 0 until ME3Tweaks assigns one.

Changes per version are in [CHANGELOG.md](CHANGELOG.md).
