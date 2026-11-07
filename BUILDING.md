# Building Pause Anytime

`build.sh` builds `LE1PauseAnytime.asi`, `LE2PauseAnytime.asi` and `LE3PauseAnytime.asi`, one file per game from the
same sources. It is a bash script for Windows: it runs in WSL or in Git Bash and calls the Windows build tools. It
downloads LExASIs and Dear ImGui by itself, each at a pinned commit ([THIRD_PARTY.md](THIRD_PARTY.md)).

## What you need

| What | Why |
|---|---|
| Windows, with WSL or Git Bash | `build.sh` and `package.sh` are bash scripts. GitHub's build runs them in Git Bash. |
| Git for Windows | `build.sh` runs `git.exe` on the build tree, which lives on the Windows disk. |
| Visual Studio 18 (2026) or its Build Tools | With the C++ x64 build tools and the "C++ CMake tools for Windows" component. |
| The Windows SDK | For the DirectX headers and libraries. `build.sh` does not check for it. |
| Network on the first build | To clone LExASIs with its submodules and fetch Dear ImGui. Again when a pin moves. |
| `git`, `python3` or `python`, and `sha256sum` in your shell | Only for `package.sh`. |
| `rsync` and `flock` | Optional: used when present. Git Bash has neither. |
| Mass Effect Legendary Edition | Only for `--install` and `--uninstall`. `--install` also needs the ASI loader. |

`git.exe`, `powershell.exe`, `reg.exe` and `tasklist.exe` must run from your shell. WSL adds the Windows `PATH` to its
own by default.

`build.sh` finds Visual Studio with `vswhere.exe`: the newest version 18 install with the component
`Microsoft.VisualStudio.Component.VC.Tools.x86.x64`. It checks only that component. Older versions can't build,
because the LExASIs preset uses the "Visual Studio 18 2026" generator and their CMake does not know it. `MELE_VS`
picks another install, and then nothing checks the version.

The ASI uses the shared Visual C++ runtime. Whoever runs your build needs the
[Microsoft Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe), at least as new as the compiler you
built with. An older `msvcp140.dll` can make the ASI crash.

## Build

```bash
git clone https://github.com/Maxgoods62/mele-pause-anytime.git
cd mele-pause-anytime
./build.sh
```

For each game, the build puts `LE<n>PauseAnytime.asi` and its `.pdb` in `dist/` and prints `built <path>`.

| Command | What it does |
|---|---|
| `./build.sh` | Builds the games in `MELE_GAMES`, else the released games: LE1, LE2 and LE3. |
| `./build.sh --game LE2` | Builds only the games named, one `--game` for each. Works with `--install` and `--uninstall` too. |
| `./build.sh --install` | Checks the games, builds, then copies each `.asi` into its game ([Install checks](#install-checks)). |
| `./build.sh --uninstall` | Deletes each `LE<n>PauseAnytime.asi` from its game. Builds nothing and needs no Visual Studio. |
| `./package.sh` | Builds into `dist/stage`, then packs one zip and `dist/SHA256SUMS` ([Package](#package)). |

A game name is `LE1`, `LE2`, `LE3` or `all`, in upper or lower case. `all` means the games on the `GAMES` line of
`asi_target` in `src/CMakeLists.txt`. Each game is built once, in the order LE1, LE2, LE3. Give `--install` or
`--uninstall` once, never both. To copy the files into the game by hand, see [docs/install.md](docs/install.md).

## Package

`./package.sh` checks the version, empties `dist/stage` and builds there with `build.sh`. Then it packs
`dist/PauseAnytime-<label>.zip` and writes the hash of that zip to `dist/SHA256SUMS`. It never installs anything.

- The label is `v<N>` by default, where N is `ASI_VERSION`. `--label latest` makes `PauseAnytime-latest.zip`.
- A label made of `v` and a number must be `v<ASI_VERSION>`, and its zip must hold every released game. For a zip of
  one game, use another label: `./package.sh --game LE2 --label test`.
- Each run deletes this label's zip and `SHA256SUMS` first. Zips with other labels stay, but `SHA256SUMS` lists only
  the new zip.

```
PauseAnytime-<label>.zip
  README.txt                  one text for all the games packed, with the commit it was built from
  LE1\LE1PauseAnytime.asi
  LE2\LE2PauseAnytime.asi
  LE3\LE3PauseAnytime.asi
  LICENSE\LICENSE             GPL-3.0
  LICENSE\THIRD_PARTY.md
  LICENSE\OFL.txt             the license of the Exo 2 fonts
  LICENSE\LICENSE-spdlog.txt
  LICENSE\LICENSE-imgui.txt
```

The `.pdb` files stay in `dist/stage`. The `README.txt` text is in `package.sh`.

GitHub builds and publishes the zips with this same script. The steps to publish a version, and the rules for every
release, are on the toolkit's [releasing page](https://github.com/Maxgoods62/mele-maxgoods-toolkit/blob/main/docs/releasing.md#publish-a-version).

## Install checks

`--install` and `--uninstall` first find the Legendary Edition folder: `MELE_LE_DIR`, else the registry
([Settings](#settings)). Then they check every game picked, before anything in the game folder changes:

- `--install` only: the ASI loader is there, as `bink2w64_original.dll` and an `ASI` folder in
  `Game\ME<n>\Binaries\Win64`. `build.sh` never writes the loader. ME3Tweaks Mod Manager installs it with any mod.
- Both: the game is closed. `tasklist.exe` must answer and must not list `MassEffect<n>.exe`.

One failed check stops the whole run, and no game is built or changed. `--install` checks again after the build,
before the first copy, and stops with "(built, not installed)" if a game was started in the meantime.

Inside the game folder, `build.sh` writes and deletes only `LE<n>PauseAnytime.asi` in `Game\ME<n>\Binaries\Win64\ASI`.
It does not look at the other files there.

## Settings

Every setting is optional. Set it in the environment, or copy `build.env.example` to `build.env` (a bash file that git
ignores) and uncomment the lines you need. A variable already set in the environment wins over `build.env`.

| Variable | Default | What it is |
|---|---|---|
| `MELE_GAMES` | `LE1 LE2 LE3`, the released games | The games to build when no `--game` is given, separated by spaces, or `all`. |
| `MELE_LE_DIR` | from the registry | The Legendary Edition folder, as a Windows path. Only `--install` and `--uninstall` use it. |
| `MELE_VS` | found with `vswhere.exe` | The Visual Studio or Build Tools folder. |
| `MELE_LEXASIS` | `%LOCALAPPDATA%\mele-asi-build\PauseAnytime\LExASIs` | The LExASIs checkout. Dear ImGui goes next to it, in `third_party`. |
| `MELE_STAGE` | `dist/` in this repo | Where each built `.asi` and `.pdb` is copied. |

The folders other than `MELE_LE_DIR` are paths of your shell: WSL paths in WSL, Git Bash paths in Git Bash. Keep
`MELE_LEXASIS` on the Windows disk: CMake and MSBuild do not work well on `\\wsl.localhost` paths. `package.sh` reads
`MELE_GAMES` and `MELE_LEXASIS` the same way, and sets `MELE_STAGE` to `dist/stage` itself.

Without `MELE_LE_DIR`, `build.sh` reads the `Install Dir` value under `HKLM\SOFTWARE\BioWare`, in the key whose name
ends in `Legendary Edition`. Then it tries Steam's uninstall entry for app 1328670. The folder must contain `Game`.

`LEASI_GAME1_WIN64`, `LEASI_GAME2_WIN64`, `LEASI_GAME3_WIN64` and `LEASI_GAMEL_WIN64` make LExASIs copy each build into
the game. `build.sh` removes them for the configure step, and stops if one still reaches CMake from the Windows side.

## What a build does

With `--install` or `--uninstall`, the [install checks](#install-checks) come first. `--uninstall` stops once it has
deleted the files. Then `build.sh`:

1. Finds Visual Studio and the CMake that comes with it.
2. Takes a lock with `flock`, `.lexasis-build.lock` next to the LExASIs checkout. A second build of the same tree, from
   another clone or worktree, waits for the first.
3. Clones LExASIs on the first build. On every build it checks that the checkout and its submodules (LExSDKv2, spdlog,
   simdutf) are at their pinned commits, and checks them out again if not. This repairs an interrupted clone too.
4. Fetches Dear ImGui into `third_party\imgui` when that folder is not at its pinned commit.
5. Copies `src/` into the checkout as `LExASIs\PauseAnytime`. With `rsync`, it copies only the files whose content
   changed. Then it adds these lines to the end of the tree's `CMakeLists.txt` if they are missing:

   ```cmake
   if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PauseAnytime")
     add_subdirectory ("PauseAnytime")
   endif ()
   ```

6. Configures with the tree's `VisualStudio` preset: `LEASI_BUILD_LE<n>` on for each game picked and off for the others,
   and `LEASI_BUILD_LEL` off. The output is hidden ([When something fails](#when-something-fails) shows it).
7. Checks that each game picked has its `PauseAnytime-LE<n>` project, in `Build\PauseAnytime` and in
   `Build\LExASIs.slnx`.
8. Builds each `PauseAnytime-LE<n>` target in `RELEASE`, one game after the other. On a failure it prints the
   compiler's error and warning lines and stops.
9. Once every game has built, copies each `.asi` and `.pdb` from `Build\RELEASE` into `MELE_STAGE`. It does not empty
   the stage first. `--install` then checks the games again and copies each `.asi` into its game.

Edit the sources in this repo's `src/`, never in `LExASIs\PauseAnytime`: each build replaces that folder. With the
defaults, the build tree looks like this:

```
%LOCALAPPDATA%\mele-asi-build\PauseAnytime\
  LExASIs\                ME3Tweaks/LExASIs at the pinned commit
    PauseAnytime\         the copy of src/
    Build\RELEASE\        the built .asi and .pdb files
  third_party\imgui\      Dear ImGui at the pinned commit
  .lexasis-build.lock     only where flock exists
```

### Your own changes in the tree

The repair in step 3 keeps your own work in the LExASIs checkout. First it removes the project lines that builds like
this one add to `CMakeLists.txt`: those for `PauseAnytime` and for folders whose names start with `Mele`. If the file
still differs from the pinned commit, the build stops. Then git checks out the pinned commit, which keeps your changes
to other files, or refuses if it would overwrite one.

A line that another project added to `CMakeLists.txt` counts as your change. So give each project its own tree, as
the defaults do. Your changes in `third_party\imgui` stay while that folder is at its pinned commit.

### Compiler and linker options

The LExASIs tree sets C++20, the shared Visual C++ runtime (`/MD`), `/W4 /WX` and a `.pdb` for its ASI targets. Dear
ImGui builds as a static library of ours, `PauseAnytimeImGui`, with its own defaults. `src/CMakeLists.txt` adds:

- `/DELAYLOAD:d3dcompiler_47.dll`: the ASI loads without that DLL. `src/Overlay.cpp` loads the copy in System32
  first, so Dear ImGui's DirectX 11 backend never binds to one next to the game.
- `/PDBALTPATH`: the ASI names its `.pdb` by file name only.
- `/d1trimfile`: file paths in the ASI (log macros, fatal errors) start below the build folder, so no user folder name
  gets in.

## Adding a game

LE1, LE2 and LE3 are all built and released. The scripts know only these three names, so another target would also
need changes in `scripts/games.sh` and `build.sh`. A game needs these steps, in this order:

1. `src/game/LE<n>.hpp`, with every name that `src/game/LE1.hpp` defines and the two photo mode switches
   `PAUSEANYTIME_PHOTO_GUI_SOUNDS` and `PAUSEANYTIME_PHOTO_GFX`.
2. A branch in `src/game/Game.hpp`. Any other SDK target stops the compile.
3. Its block in `src/SharedVersion.h`: file prefix, ME3Tweaks game id, group id and description.
4. Its name on the `GAMES` line of `asi_target` in `src/CMakeLists.txt`. Do steps 1 to 3 first: the `VisualStudio`
   preset turns every `LEASI_BUILD_LE<n>` on, so a listed game that does not compile breaks the tree's build in Visual
   Studio.
5. Its name in `RELEASED_GAMES` in `scripts/games.sh`. A released game builds by default, and every release zip must
   hold it. A game that is only on the `GAMES` line builds when you name it.
6. Its entry in the Game list of `.github/ISSUE_TEMPLATE/bug_report.yml`.

## When something fails

Each error from `build.sh` starts with `build.sh:` and stops the script. `package.sh` runs `build.sh`, so it can show
the same errors, and its own start with `package.sh:`. Below, `<n>` is 1, 2 or 3, and `...` is a path or a name.

| `build.sh` says | What to do |
|---|---|
| `--game needs LE1, LE2, LE3 or all` | Put a game name after `--game`. |
| `--install or --uninstall, not both` | Give one of them, once. |
| `unknown argument '...'` | Use only the flags in the usage line of the message. |
| `unknown game '...': LE1, LE2, LE3 or all` | Fix the name in `--game` or in `MELE_GAMES`. |
| `no GAMES line in src/CMakeLists.txt, so no games for 'all'` | Put back the `GAMES` line of `asi_target`. |
| `Legendary Edition not found in the registry` | Set `MELE_LE_DIR`. This also shows when `reg.exe` does not run. |
| `no Game folder in '...'` | Set `MELE_LE_DIR` to the folder that holds `Game`. |
| `no ASI loader in Game\ME<n>` or `no ASI folder in Game\ME<n>\Binaries\Win64` | Install any mod in that game with ME3Tweaks Mod Manager. It adds the loader. |
| `MassEffect<n>.exe is running: close the game first` | Close the game. If it is closed, check that `tasklist.exe` runs: a failed one counts as running. |
| `... (built, not installed)` | The game started during the build. The files are in `MELE_STAGE`: close the game and run again. |
| `no vswhere.exe` | Install the Visual Studio 18 Build Tools with C++, or set `MELE_VS`. This also shows when `powershell.exe` does not run. |
| `vswhere found no Visual Studio 18 (2026) with the C++ x64 tools` | Add the C++ x64 build tools in the Visual Studio Installer, or set `MELE_VS`. |
| `no CMake in ...` | Add the "C++ CMake tools for Windows" component, or set `MELE_VS` to an install that has it. |
| `no %LOCALAPPDATA% from PowerShell` | Set `MELE_LEXASIS` to a folder on the Windows disk. |
| `CMakeLists.txt in ... has changes of your own` | Commit or stash them in that checkout ([Your own changes](#your-own-changes-in-the-tree)). |
| `could not check out LExASIs ...` | Read git's lines above it. Commit or stash your changes there. With none, delete the folder. |
| A git error with no `build.sh:` line | The clone of LExASIs or the fetch of Dear ImGui failed. Check the network and `git.exe`. |
| `cmake configure failed: run it by hand in ...` | Run the configure command below in that folder to see its output. |
| `a LEASI_GAME*_WIN64 variable is set in the Windows environment` | Remove it in System Properties, Environment Variables. Then open a new terminal. |
| `no PauseAnytime-LE<n> project` | That game is not on the `GAMES` line. Leave it out, or [add it](#adding-a-game). |
| `PauseAnytime-LE<n> did not build` | The lines above it are the compiler's errors and warnings, each shown once. |

Two lines are not errors: `another build of ... is running: waiting for it` (another run holds the lock on that tree),
and `checking out LExASIs ... and its submodules in ...` (the first build, a new pin, or a repair).

| `package.sh` says | What to do |
|---|---|
| `--label needs a name` | Put a name after `--label`. |
| `no ASI_VERSION in ...` | Put back the `#define ASI_VERSION <number>` line in `src/SharedVersion.h`. |
| `VERSION_STRING_A "..." differs from ASI_VERSION ...` (or `VERSION_STRING_W`) | Set both strings to the version number. |
| `label v... differs from ASI_VERSION ...` | Use `v<ASI_VERSION>`, or a label that is not `v` and a number. |
| `no git commit in ...` | Run it in a git clone, with `git` in your shell. The zip's `README.txt` names the commit. |
| `no python that runs: install python3` | Install Python. On Windows, `python3` can be the Microsoft Store stub, which does not run. |
| `label v... packs ...: a release zip needs every released game` | Leave out `--game`, or use another label. |
| `missing ...` | A file to pack is not at that path. |

To see the configure output, run in the LExASIs folder what `build.sh` runs, with `=ON` for each game picked and
`=OFF` for the others. `<VS folder>` is the Visual Studio folder as a path of your shell. `<third_party folder>` is the
`third_party` folder next to the checkout, as a Windows path with forward slashes. The second command is the build
step, and it shows all of the MSBuild output.

```bash
CMAKE='<VS folder>/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
env -u LEASI_GAME1_WIN64 -u LEASI_GAME2_WIN64 -u LEASI_GAME3_WIN64 -u LEASI_GAMEL_WIN64 "$CMAKE" --preset VisualStudio \
    -DLEASI_BUILD_LE1=ON -DLEASI_BUILD_LE2=ON -DLEASI_BUILD_LE3=ON -DLEASI_BUILD_LEL=OFF \
    '-DMELE_THIRD_PARTY=<third_party folder>'
"$CMAKE" --build Build --config RELEASE --target PauseAnytime-LE1 -- /m /v:minimal /nologo
```

## Version

The version is `ASI_VERSION` in `src/SharedVersion.h`, one number for every game. `VERSION_STRING_A` and
`VERSION_STRING_W` next to it hold the same number as text. The log's `LE<n>PauseAnytime <N> ready` line prints the
first, and the ASI loader logs the second. `package.sh` stops if they differ.

`src/PauseAnytime.rc` gives each file the Windows version `<N>.0.<game id>.<group id>`, as the LExASIs projects do. The
game id is the one ME3Tweaks uses: 4 for LE1, 5 for LE2 and 6 for LE3. The group id is 0 until ME3Tweaks assigns one.

The changes in each version are in [CHANGELOG.md](CHANGELOG.md).
