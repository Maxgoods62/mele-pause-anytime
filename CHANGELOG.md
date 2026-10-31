# Changelog

Versions are `ASI_VERSION` in `src/SharedVersion.h`, one for every game. The git tags `v2` to `v5` mark them; none is a
published release yet. The first line of each entry names the games it covers. Every version needs an ASI loader.

## v16 (unreleased)

Games: LE1, LE2, LE3

LE1's pause screen is set in the game's own fonts. In LE2 and LE3 the ASI does the same as v15, apart from the title's
letters, which sit under a pixel differently at heights other than 1080 lines.

- In LE1 the title is in the game's Aero Light, as its unused paused box sets this word, with kerning and no more wide
  letter spacing. The labels are in its BioMass, as its pause wheel sets "Resume" and "Photo Mode", and the keycaps in Aero
  Light, as its key bindings set a key's name. The colours and the band stay.
- The fonts are read from the game's memory as it runs and turned into a small TrueType font in memory, with only the
  letters the screen needs. Nothing of them is in the ASI, and nothing is written to disk. If they can't be read, Exo 2 stays
  and the log says so.
- In LE1 in Japanese the title and the labels are now the game's words, which Exo 2 could not draw.
- `package.sh` writes `dist/LE1MelePause-v16.zip`, `dist/LE2MelePause-v16.zip` and `dist/LE3MelePause-v16.zip`.

## v15 (unreleased)

Games: LE1, LE2, LE3

LE1's pause screen shows the game's own gamepad buttons. In LE2 and LE3 the ASI does the same as v14.

- In LE1 the A and Y of the gamepad prompts are the game's own pictures (`GUI_SF_SharedAssets`, the ones its wheel and
  message boxes show), read from its renderer as the game runs. Nothing of them is in the ASI. If they can't be had (another
  build of the game, another D3D11 device), the discs of v14 are drawn instead, and the log says so.
- The keyboard's keys stay drawn: the game has no F7 key picture, and its space bar has no word on it.
- `package.sh` writes `dist/LE1MelePause-v15.zip`, `dist/LE2MelePause-v15.zip` and `dist/LE3MelePause-v15.zip`.

## v14 (unreleased)

Games: LE1, LE2, LE3

LE1's pause screen takes the look, the words and the sound of the game's menus. In LE2 and LE3 the ASI does the same as
v13, apart from the log.

- In LE1 the title stands on a band like the game's menu bars (dark blue, a light blue rule along each edge, fading out
  at both ends), in the game's pale cyan. Each key is drawn left of its label as the game draws its buttons: green A and
  yellow Y discs with a dark letter, grey keycaps for Space and F7. The labels are in the game's label blue, and the
  blur is a little bluer.
- In LE1 the title and the two labels are the game's own words, read from its string table in its language: "PAUSED",
  "Resume" and "Photo Mode" in English, "PAUSE", "Continuer" and "Mode photo" in French. A word Exo 2 cannot draw
  (Japanese) keeps MelePause's English; the space bar's keycap keeps MelePause's name for it.
- In LE1 the pause plays the game's menu sound (`snd_gui.c_snd_guiswish`, the GUI manager's `InGameGuiEnter`), at once
  under the pause, as photo mode's sounds do. A key that resumes plays its `InGameGuiExit` as the world runs again. The
  movie pause plays none.
- The font is still Exo 2: the game's own fonts and button pictures are left for later.
- `package.sh` writes `dist/LE1MelePause-v14.zip`, `dist/LE2MelePause-v14.zip` and `dist/LE3MelePause-v14.zip`.

## v13 (unreleased)

Games: LE1, LE2, LE3

LE3 gets the movie pause, as LE2 has it. In LE1 and LE2 the ASI does the same as v12, apart from the log.

- In LE3, Esc or Start pauses a fullscreen movie that a level starts when MelePause knows its sound: the Wwise event the
  Kismet movie action names is paused with the picture, as in LE2. 38 of the 71 movies the levels start qualify, among
  them the mass relay jump, the first arrival at the Citadel, Omega's intro and the Leviathan arrivals. Esc, Start,
  Space or A resumes, under the pause screen.
- LE3's engine movie input is hooked as LE2's (its first bytes are LE2's). Space and X reach it as they are: in LE3 they
  are its skip keys, and Enter is not.
- Most of LE3's movies play inside a cutscene or a conversation (Matinee tracks), and the loading screens play movies
  too: they keep vanilla Esc and Start.
- LE2's Kismet movie rule moves to `src/game/KismetMovie.hpp`, shared by LE2 and LE3. In LE3 a movie action's `Prime`
  input and `Primed` output (a preload) don't count as starting the movie.
- The movie lines in LE3's log show the movie player's flags and skippable flag. In every game, the Esc lines show every
  game mode that is on.
- `package.sh` writes `dist/LE1MelePause-v13.zip`, `dist/LE2MelePause-v13.zip` and `dist/LE3MelePause-v13.zip`.

## v12 (unreleased)

Games: LE1, LE2, LE3

LE3 gets photo mode under the pause. In LE1 and LE2 the ASI does the same as v11.

- In LE3, in a conversation or an in-engine cutscene that MelePause paused, a gamepad's Y or F7 opens the game's photo
  mode on the paused shot, as in LE1 and LE2. Three native hooks, checked against the game's code first, let its input
  and its panel past the world pause. LE3's GUI movies advance on the game's clock, which stands still under the pause:
  while photo mode is on, their tick sees the real time go by, and only their tick.
- In LE3 the reply wheel stays hidden under photo mode. The game can show it again on its own; MelePause hides it again
  on the next frame.
- Photo mode's GUI side is split per game: `src/PhotoGuiSF.cpp` for LE1's and LE2's ScaleForm GUI manager,
  `src/PhotoGuiGFx.cpp` for LE3's GFx movies (`MELEPAUSE_PHOTO_GFX` in `game/LE<n>.hpp`).
- Still not in LE3: the movie pause. Fullscreen movies keep their vanilla Esc and Start.
- `package.sh` writes `dist/LE1MelePause-v12.zip`, `dist/LE2MelePause-v12.zip` and `dist/LE3MelePause-v12.zip`.

## v11 (unreleased)

Games: LE1, LE2, LE3

LE3 gets its own file, `LE3MelePause.asi`. In LE1 and LE2 the ASI does the same as v10.

- In LE3, Esc or Start pauses a conversation or an in-engine cutscene with the game's world pause, and Esc, Start, Space
  or A resumes it, under the same pause screen as in LE1 and LE2. Everywhere else Esc and Start keep their vanilla jobs.
- LE3 lets Esc skip a skippable cutscene. With MelePause, Esc pauses it instead; Space, or a gamepad's X, still skips it
  when nothing is paused.
- Not in LE3: the movie pause (125 of LE3's 126 movies play their sound through Wwise) and photo mode under the pause.
  Fullscreen movies, Y and F7 keep their vanilla jobs ([docs/games/LE3.md](docs/games/LE3.md)).
- `./build.sh` and `./package.sh` cover LE1, LE2 and LE3 by default, so `./build.sh --install` also copies into ME3, and
  stops before building when ME3 has no ASI loader.
- `package.sh` writes `dist/LE1MelePause-v11.zip`, `dist/LE2MelePause-v11.zip` and `dist/LE3MelePause-v11.zip`.

## v10 (unreleased)

Games: LE1, LE2

LE2 gets photo mode under the pause. In LE1 the camera's entry and exit change; the rest is as in v9.

- In LE2, in a conversation or an in-engine cutscene that MelePause paused, a gamepad's Y or F7 opens the game's photo
  mode on the paused shot, as in LE1. Three native hooks, checked against the game's code first, let its input and its
  panel past the world pause. Its sounds play without a hook.
- Photo mode's camera, both games. On entry, the game's switch to the photo camera turns the controller to the camera
  mode left behind; MelePause now puts it back on the paused shot. In LE2 the first entry of a pause in a director's
  shot looked away. On exit, the switch back now ends at once on the paused shot; in LE2 the view came back about half
  a second after the resume.
- Photo mode opened in a cutscene's first second, while the camera still blends into the director's camera, holds that
  blend until it ends: the blend took the view away from photo mode, which then did not answer. At the exit the blend
  goes on from where it stood.
- `src/PhotoLE1.cpp` is now `src/Photo.cpp`, for every game; the per-game natives and names are in
  `src/game/LE<n>.hpp`.
- `package.sh` writes `dist/LE1MelePause-v10.zip` and `dist/LE2MelePause-v10.zip`.

## v9 (unreleased)

Games: LE1, LE2

LE2 gets the movie pause. In LE1 the ASI does the same as v8.

- In LE2, Esc or Start pauses a fullscreen movie whose sound MelePause knows, and Esc, Start, Space or A resumes it,
  under the pause screen. Its sound is the Wwise event that the movie's Kismet action starts beside the picture:
  MelePause pauses and resumes that event with the picture, through the game's own `ExecuteActionOnEvent`. The sound is
  known when that action starts nothing else that may make a sound beside the movie: 71 of the 117 actions with an
  event, the mass relay jump among them. Any other movie keeps vanilla Esc and Start.
- LE2's engine skips a skippable movie on the press of Esc or Start before any script sees it. MelePause hooks that
  native input, after checking the game's code: in a movie it can pause, Esc and Start pause instead, and no key skips
  a paused movie. On another build of the game the hook is left out and the log says so.
- In LE2, Space and a gamepad's X skip a fullscreen movie the game lets you skip. Their `SkipMovie` binding does nothing
  in the vanilla game.
- `package.sh` writes `dist/LE1MelePause-v9.zip` and `dist/LE2MelePause-v9.zip`.

## v8 (unreleased)

Games: LE1, LE2

LE2 gets its own file, `LE2MelePause.asi`. In LE1 the ASI does the same as v7.

- In LE2, Esc or Start pauses a conversation or an in-engine cutscene with the game's world pause, and Esc, Start, Space
  or A resumes it, under the same pause screen as in LE1. Everywhere else Esc and Start keep their vanilla jobs.
- LE2 lets Esc skip a skippable cutscene. With MelePause, Esc pauses it instead; Space, or a gamepad's X, still skips it
  when nothing is paused. The skips are held back while a press that paused or resumed is down, and for a quarter
  second after its release.
- Not in LE2: the movie pause (LE2's movies play their sound through Wwise, which a Bink pause does not stop) and photo
  mode under the pause. Fullscreen movies, Y and F7 keep their vanilla jobs ([docs/games/LE2.md](docs/games/LE2.md)).
- `./build.sh` and `./package.sh` cover LE1 and LE2 by default, so `./build.sh --install` also copies into ME2, and
  stops before building when ME2 has no ASI loader. To install LE1 alone, name `--game LE1`, or set `MELE_GAMES=LE1`.
- `package.sh` also writes `dist/LE2MelePause-v8.zip`.

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
  they build the released games, LE1. `src/CMakeLists.txt` also builds `LE2MelePause.asi`, with photo mode and the
  movie pause off; it is not released ([docs/games/LE2.md](docs/games/LE2.md)). See
  [BUILDING.md](BUILDING.md#games).
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
