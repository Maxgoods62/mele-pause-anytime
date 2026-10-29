# Changelog

Versions are `ASI_VERSION` in `src/SharedVersion.h`, one for every game. The git tags `v2` to `v5` mark them; none is a
published release yet. The first line of each entry names the games it covers. Every version needs an ASI loader.

## v7 (unreleased)

Games: LE1

The file is renamed and the build makes one file per game. In LE1 the ASI does the same as v6, except for its place
among other ASIs' hooks (below).

- The file is now `LE1MelePause.asi`: the CMake target `MelePause-LE<n>` builds `LE<n>MelePause.asi` for each game.
  Delete the old `MelePause.asi` from the ASI folder; `./build.sh --install` and `--uninstall` do. While it is loaded,
  `LE1MelePause.asi` stays off and leaves the log to it (the loader refuses the second copy's hooks anyway, but both
  would rewrite the log). It says so to a debugger (DebugView), and the loader logs an attach error in
  `Logs\bink2w64_proxy.log`. The log stays `Logs\MelePause.log`.
- The loader loads the ASI folder in file-name order, and a hook installed later runs before the earlier ones. The new
  name sorts before files such as `LE1StreamingLevelsHUD-v5.asi` or `MeleBridge.asi` that `MelePause.asi` sorted after,
  so their `ProcessEvent` and `Present` hooks now run before MelePause's: what such an ASI draws now lies under the
  pause screen's blur.
- The log's ready line reads `LE1MelePause 7 ready`.
- The file has Windows version info: version `7.0.4.0`, a description, and its original file name.
- `build.sh` and `package.sh` take `--game LE1|LE2|LE3|all`, once per game, or the setting `MELE_GAMES`; by default
  they build the released games, LE1. `src/CMakeLists.txt` builds LE1 only. See [BUILDING.md](BUILDING.md#games).
- `--install` checks every game before the build: the ASI loader, the ASI folder, and the game closed. It checks again
  before it copies.
- `package.sh` writes one zip per game, `dist/LE<n>MelePause-v<N>.zip`.
- `build.sh` stops when a `LEASI_GAME*_WIN64` variable reaches the configure step (one set in the Windows environment
  would make the build copy itself into the game), refuses an argument it does not know, and copies `src/` into the
  LExASIs tree by content, so a file changed there is always recompiled.
- The pause screen also reads the language codes the LE2 and LE3 launchers pass (`-OVERRIDELANGUAGE=` for LE2,
  `-language=` for LE3). LE1's are unchanged.
- The code is split for more games. What differs per game is in `src/game/LE<n>.hpp`, with two switches that turn photo
  mode and the movie pause off for a game without them; LE1 keeps both on. Photo mode moved to `src/PhotoLE1.cpp`.

## v6 (unreleased)

Games: LE1

Build changes only. The ASI does the same as v5 in the game.

- `build.sh` builds in its own LExASIs checkout, `%LOCALAPPDATA%\mele-asi-build\MelePause\LExASIs` by default, with Dear
  ImGui next to it. It no longer shares a tree with mele-bridge. See [BUILDING.md](BUILDING.md).
- Optional settings come from the environment or from `build.env`, which is ignored by git (`build.env.example` is the
  template): `MELE_LE_DIR`, `MELE_VS`, `MELE_LEXASIS`, `MELE_STAGE`.
- Visual Studio 18 (2026) is found with `vswhere.exe`, and the game folder in the registry.
- The build is staged in `dist/` by default.
- Each build checks the LExASIs checkout against its pin and repairs it, keeping your own edits there.

## v5 (2026-10-28)

Games: LE1

- A pause screen replaces the "PAUSE" banner. The frame is blurred (a dual filter blur), darkened and a little
  desaturated. "PAUSE" stands in a bracketed frame at the centre. At the bottom left are the keys: Resume, and Photo
  mode where it opens, as gamepad icons or keycaps for whichever device was used last.
- The text is set in Exo 2 (SIL Open Font License), compiled into the ASI, in the text language the launcher passes.
  Dear ImGui draws it from the ASI's own `IDXGISwapChain::Present` hook.
- Photo mode and the open console hide the pause screen at once, so screenshots never contain it.
- The pause screen also shows over a paused movie. A paused Bink movie presents nothing, so MelePause keeps the movie's
  last frame and presents it with the pause screen over it, 60 times a second at most.
- Space and a gamepad's A resume a pause that MelePause started. They never pause, and the skips they are bound to are
  held back while the resuming press is down.
- The banner stays as the fallback when the hook or the overlay fails.
- `d3dcompiler_47.dll`, which Dear ImGui's DirectX 11 backend uses, is delay-loaded and bound to the copy in System32.

## v4 (2026-10-28)

Games: LE1

- Photo mode over a paused conversation or in-engine cutscene: a gamepad's Y or F7 opens the game's photo mode on the
  shot on screen, at its field of view. Y, F7, Esc or Start leave it, and the scene is still paused.
- Under the pause, photo mode's camera, panel and sounds work: native hooks let its input, its GUI ticks and its GUI
  sounds past the world pause while it is open. Each hook checks the function's first bytes before it patches anything.
- Photo mode's button prompts follow the key that opened it: Y shows the gamepad's, F7 the keyboard's.
- The subtitle line stays through photo mode and is there again when you leave it.
- The banner also names the photo mode key: "Y: photo mode" after "Start: resume", or "F7: photo mode" after "Esc:
  resume".
- Tested with a gamepad and with keyboard and mouse, in a conversation and in an in-engine cutscene.

## v3 (2026-10-28)

Games: LE1

- A gamepad's Start (`XboxTypeS_Start`) pauses and resumes conversations, in-engine cutscenes and the galaxy map's
  travel movie, as Esc does. The two keys can be mixed. Everywhere else Start keeps its vanilla job, such as the pause
  menu in gameplay.
- During a fullscreen movie the engine takes Start's press, so a tap acts on its release and a held press on its first
  repeat.
- The banner reads "Start: resume" when Start paused or the game uses the gamepad.
- Tested with an Xbox pad.

## v2 (2026-10-28)

Games: LE1

The first version in git.

- Esc pauses a conversation or an in-engine cutscene with the game's own world pause, and Esc again resumes it. Picture,
  voice-over, music and lip sync stop and resume together. A "PAUSE" banner is drawn on the HUD.
- Esc pauses and resumes a fullscreen Bink movie opened from a file, such as the galaxy map's travel movie. The engine
  takes Esc's press during such a movie, so the movie's own thread reads Esc in a hooked `BinkWait` and pauses it there.
- Everywhere else Esc keeps its vanilla job: the pause menu, closing the console, loading screens.
- The movie and cutscene skips bound to Esc are dropped while a press MelePause acted on is down.
- The log is `Logs\MelePause.log` next to the game's executable.
