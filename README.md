# Pause Anytime

Pause Anytime (called MelePause up to version 17) is an ASI plugin for Mass Effect 1, 2 and 3 in the Legendary Edition
(LE1, LE2 and LE3), installed as `LE1PauseAnytime.asi`, `LE2PauseAnytime.asi` and `LE3PauseAnytime.asi`. Esc, or Start on
a gamepad, pauses a conversation or an
in-engine cutscene, and a fullscreen movie such as LE1's galaxy map travel movie or LE2's and LE3's mass relay jump, and the same
key resumes it. While the game is paused, the picture is blurred and darkened under a pause screen. In a paused
conversation or cutscene, a gamepad's Y or F7 opens the game's photo mode on the paused shot. Everywhere else these keys
do what they always did.

Version 18 (`ASI_VERSION` in [`src/SharedVersion.h`](src/SharedVersion.h)). Download it from
[Releases](https://github.com/Maxgoods62/mele-pause-anytime/releases), or build it from source, and copy it into the
game ([Install](#install)).

## Games

| Feature | LE1 | LE2 | LE3 |
|---|---|---|---|
| Esc pauses a conversation or an in-engine cutscene | since v2 | since v8 | since v11 |
| Esc pauses a fullscreen movie | since v2 | since v9, a movie whose sound Pause Anytime knows | since v13, a movie a level starts whose sound Pause Anytime knows |
| Start on a gamepad pauses what Esc pauses | since v3 | since v8 | since v11 |
| Photo mode on the paused shot (Y or F7) | since v4 | since v10 | since v12 |
| Pause screen; Space and A resume | since v5 | since v8 | since v11 |
| Pause screen in the game's menu colours, with its words and its menu sound | since v14 | since v17 | since v18 |
| The game's own gamepad buttons on the pause screen | since v15 | since v17 | since v18 |
| The game's own fonts on the pause screen | since v16 | since v17 | since v18 |

Each game has its own file, for its own ASI folder: `LE1PauseAnytime.asi` for ME1, `LE2PauseAnytime.asi` for ME2,
`LE3PauseAnytime.asi` for ME3. The sections below describe LE1; [In LE2](#in-le2) and [In LE3](#in-le3) say what differs
there.

## What Esc and Start do

| Where | In the vanilla game | With Pause Anytime |
|---|---|---|
| Conversation | Esc and Start do nothing. | Esc or Start pauses everything: picture, voice-over, music, lip sync. Esc, Start, Space or A resumes at the same word, in sync. The [pause screen](#pause-screen) comes up. |
| In-engine cutscene | Esc only plays an error sound (the game has its cutscene skip turned off). Start does nothing. | The same pause as in a conversation. |
| Fullscreen movie the engine does not skip on Esc, played from a file (the galaxy map's travel movie) | The movie plays to its end. | Esc or Start pauses the movie, picture and sound. Esc, Start, Space or A resumes it. The pause screen comes up, without the photo mode key. Start acts on its release, or after about 0.2 s when held, never on its press (see [Known issues](#known-issues)). |
| Fullscreen movie the engine skips on Esc | Skipped. | Still skipped: the engine acts on the press before Pause Anytime sees it. |
| Loading-screen movie | Nothing. | Unchanged. |
| Anywhere else (gameplay, menus, the galaxy map, the open console) | Their vanilla jobs: the pause menu, closing a menu or the console. | Unchanged. |

Start is the game's `XboxTypeS_Start`, which an Xbox pad sends. Through Steam Input or DS4Windows, a PlayStation pad's
Options button should send it too; only an Xbox pad has been tested.

Space and a gamepad's A (Cross on a PlayStation pad through Steam Input) only resume a pause that Pause Anytime started.
They never pause, and they do nothing in photo mode, where Space takes the picture, or in the open console. When nothing
is paused, Space keeps its vanilla job: it skips a conversation line, or a movie or cutscene that the game lets you
skip. While a press that Pause Anytime acted on is held down, and for a quarter second after its release, the skips
bound to Esc and Space are held back, so the press that resumes never skips as well.

The pause is the game's own world pause, the one the `pause` console command sets. The pause menu and photo mode use a
lighter pause, under which a conversation's clock and the music keep running.

## In LE2

- Esc or Start pauses a conversation or an in-engine cutscene, with the same world pause as in LE1, under
  Pause Anytime's own pause screen ([Pause screen](#pause-screen)). Esc, Start, Space or A resumes it.
- LE2 lets Esc skip a skippable cutscene. With Pause Anytime, Esc pauses it instead; Space, or a gamepad's X, still
  skips it when nothing is paused.
- A fullscreen movie pauses when Pause Anytime knows its sound. LE2's movies play their sound through a Wwise event that
  the level starts beside the picture; Pause Anytime pauses that event with the picture. It knows the sound of 71 of the
  117 movies the levels start with such an event, among them the mass relay jump and the Citadel's arrival and
  departure. Esc or Start pauses such a movie, and Esc, Start, Space or A resumes it.
- LE2's engine skips a skippable movie on the press of Esc or Start before Pause Anytime sees it. Pause Anytime keeps
  those presses from it in a movie it can pause, and no key skips a paused movie. Space, or a gamepad's X, skips any
  movie the game lets you skip (in the vanilla game they do nothing there).
- Any other movie keeps vanilla Esc and Start: Esc skips it when the game lets you.
- Photo mode under the pause works as in LE1. Its sounds play under the pause without the hook LE1 needs: LE2's GUI
  sounds go through Wwise.

The details, and the open questions: [docs/games/LE2.md](docs/games/LE2.md).

## In LE3

- Esc or Start pauses a conversation or an in-engine cutscene, with the same world pause as in LE1, under
  Pause Anytime's own pause screen ([Pause screen](#pause-screen)). Esc, Start, Space or A resumes it.
- LE3 lets Esc skip a skippable cutscene. With Pause Anytime, Esc pauses it instead; Space, or a gamepad's X, still
  skips it when nothing is paused.
- A fullscreen movie that a level starts pauses when Pause Anytime knows its sound, as in LE2: Pause Anytime pauses the
  Wwise event the movie plays with the picture. It knows the sound of 38 of the 71 movies the levels start, among them
  the mass relay jump, the first arrival at the Citadel, Omega's intro and the Leviathan arrivals. Esc or Start pauses
  such a movie, and Esc, Start, Space or A resumes it.
- Most of LE3's movies play inside an in-engine cutscene or a conversation instead, and the loading screens play movies
  too: those keep vanilla Esc and Start.
- In LE3, Space and a gamepad's X are the engine's own keys in a movie: they skip a movie the game lets you skip, as in
  the vanilla game.
- Photo mode under the pause works as in LE1. LE3's GUI movies run on the game's clock, which the pause stops: while
  photo mode is on, Pause Anytime lets them see the real time go by, so its panel loads and answers.

The details, and the open questions: [docs/games/LE3.md](docs/games/LE3.md).

## Photo mode

| Key, in a conversation or cutscene that Pause Anytime paused | What it does |
|---|---|
| Y (gamepad) or F7 | Opens the game's photo mode on the shot that was on screen, at its field of view. The dialogue wheel, the subtitle and the pause screen go away, and photo mode's own controls work: the sticks, LT and RT, X or Space for a screenshot, B or Left Shift for its options; with mouse and keyboard, hold the right button to look and move. The world stays paused under it. |
| Y, F7, Esc or Start, in that photo mode | Leaves it. The paused shot, the wheel, the subtitle and the pause screen come back, still paused. Esc or Start again resumes. |

The game opens photo mode only from the pause menu, and it refuses that menu in a conversation or a cutscene.
Pause Anytime opens photo mode under its own pause and sets aside what would break it there (see
[Photo mode, entering](docs/how-it-works.md#photo-mode-entering)). The camera moves at photo mode's own speed: 350 units
a second at full stick, reached over about a second. Below 70% of the stick it moves at no more than about 35% of that.

Y and F7 keep their vanilla job everywhere else: gameplay, menus, an unpaused conversation, a movie. Photo mode from the
pause menu is unchanged.

## Pause screen

| Part | How it looks |
|---|---|
| The picture | Blurred (about 24 pixels of radius at 1080p, and the same share of the screen at any resolution), half as bright, a little desaturated, darker in the corners; in LE1 and LE3 a little bluer, in LE2 a little warmer. It fades in over 0.2 s and out over 0.15 s. The wheel and the subtitle are blurred with it. |
| The centre | LE1: the title in the game's Aero Light and pale cyan, on a band like the game's menu bars (dark blue, a light blue rule along each edge, fading out at both ends). LE2: the same band in the game's brown and orange, the title in its Biomass and gold. LE3: the same band in the game's blue, the title in its Biomass and white. |
| The bottom left | "Resume" (A, or Space; Start and Esc resume too), and "Photo mode" (Y, or F7) where photo mode opens, with the keys of whichever device pressed one of those keys last. Each key stands left of its label, the labels in the game's BioMass and its label colour (LE1 blue, LE2 orange, LE3 pale cyan); the gamepad's keys the game's own A and Y pictures, read from its renderer (green and yellow discs drawn to match them if it can't give them), the keyboard's as grey keycaps in the game's light font, Aero Light or Myriad (a wide space bar named in the launcher's language, English in Japanese, and F7). |
| The words | LE1: the game's own, from its string table, in its text language ("PAUSED", "Resume", "Photo Mode" in English; "PAUSE", "Continuer", "Mode photo" in French). LE2 and LE3: the labels the game's own, the title the ASI's own (their string tables have no such word in every language), in the text language the game's launcher passes: English, French, German, Italian, Spanish, Polish or Russian. Pause Anytime's words are English for Japanese in LE2 and LE3 (Exo 2 has no kana), and when the game starts without the launcher's language switch. |
| The fonts | The game's own, read from its memory as it runs. Nothing of them is in the ASI or written to disk. If they can't be read: Exo 2, and Pause Anytime's English in Japanese. |
| The sound | The game's menu sound as the pause starts, and as a key resumes. None for the movie pause. |

The pause screen goes away at once, with no fade, when photo mode starts, so photo mode's screenshots never contain it.
It also hides while the console is open. It comes back three frames after photo mode ends.

If the pause screen cannot draw, Pause Anytime falls back to a plain "PAUSE" banner on the HUD with the keys under it.
That happens when the swap chain cannot be hooked, when the overlay fails to start or faults, and on a Windows without
`d3dcompiler_47.dll` in System32. A back buffer format the blur does not handle gets a plain darkening.

## Requirements

- Mass Effect Legendary Edition. Pause Anytime supports LE1, LE2 and LE3 ([Games](#games)).
- An ASI loader: [LEBinkProxy](https://github.com/ME3Tweaks/LEBinkProxy), the `bink2w64.dll` proxy that loads every
  `.asi` file in `Game\ME<n>\Binaries\Win64\ASI`, one per game. ME3Tweaks Mod Manager installs it with any mod.
- The latest [Microsoft Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe), no older than the MSVC
  tools that built your copy. Pause Anytime is built with Visual Studio 2026 against the shared runtime
  (`MSVCP140.dll`, `VCRUNTIME140.dll`), and an older `msvcp140.dll` can make it crash.

Pause Anytime needs no other mod, MeleBridge included.

## Install

Download your game's zip from [Releases](https://github.com/Maxgoods62/mele-pause-anytime/releases). You can
[check the download](docs/releasing.md#check-a-download) against the published hash. Close the game, then copy
`LE<n>PauseAnytime.asi` from the zip into `<LE folder>\Game\ME<n>\Binaries\Win64\ASI\`, LE1's into ME1's, LE2's into ME2's
and LE3's into ME3's. The newest code on `main` is at <https://maxgoods62.github.io/mele-pause-anytime/latest/>, less tested than
a release.

Or build it yourself ([BUILDING.md](BUILDING.md)). Close the game, then install it one of two ways:

- `./build.sh --install` builds `LE1PauseAnytime.asi`, `LE2PauseAnytime.asi` and `LE3PauseAnytime.asi` and copies each
  into its game's ASI folder.
  It checks first, for each game, that the ASI loader is there and that the game is not running, and builds nothing
  when a check fails. To install one game alone, name it: `./build.sh --game LE1 --install`.
- A copy by hand does the same: put `LE<n>PauseAnytime.asi` from `dist/` (or your `MELE_STAGE`) into
  `<LE folder>\Game\ME<n>\Binaries\Win64\ASI\`, LE1's into ME1's, LE2's into ME2's and LE3's into ME3's.

`<LE folder>` is the Legendary Edition folder, the one that holds `Game`. To update, install the new version the same
way: the file name carries no version, so the new copy replaces the old one.

Versions 17 and older were called MelePause, in files named `LE<n>MelePause.asi`, and in LE1 `MelePause.asi` for
versions 6 and older. `./build.sh --install` deletes them from the ASI folders. After a copy by hand, delete them
yourself: while one is there, the ASI loader loads both files, the old version runs and `LE<n>PauseAnytime.asi` stays
off.

To check that it loaded, start the game and open `<LE folder>\Game\ME<n>\Binaries\Win64\Logs\PauseAnytime.log`. It has a
line that ends in `LE<n>PauseAnytime N ready`, where N is the version. If that log is missing or older than the game's
start, and `Logs\MelePause.log` is new, an old file is still in the ASI folder.

## Uninstall

With the game closed, run `./build.sh --uninstall` (or `--game LE1 --uninstall` for one game), or delete
`LE<n>PauseAnytime.asi` from `<LE folder>\Game\ME<n>\Binaries\Win64\ASI\`. `--uninstall` also deletes an old
`LE<n>MelePause.asi`, and `MelePause.asi` in ME1's. The log, `Logs\PauseAnytime.log` in `Binaries\Win64`, stays, as does
the old versions' `Logs\MelePause.log`; delete them too if you like.

## Compatibility

| With | Result |
|---|---|
| [MeleBridge](https://github.com/Maxgoods62/mele-maxgoods-toolkit/blob/main/tools/mele-bridge/README.md) | Runs side by side. Both hook `ProcessEvent` and the swap chain's `Present` through the ASI loader's shared hook manager. |
| The Steam overlay, ReShade, MeleBridge's editor | Each draws over the frame too. Whichever hook runs later draws on top. |
| Any resolution or aspect ratio | Sizes follow the height of the back buffer, laid out for 1080 lines. |
| Gamepads | Tested with an Xbox pad. A PlayStation pad would go through Steam Input or DS4Windows, which present it to the game as an Xbox pad; this is untested. |
| Another build of the game | The native hooks for photo mode check the game's code before they patch it. If it differs, the log has a warning, photo mode still opens, and its camera, its panel or its sounds may not answer. If the camera's or the panel's hook is missing, the pause screen leaves out the photo mode key. LE2's and LE3's movie input hooks check it too: if it differs, Esc and Start skip a skippable movie as in the vanilla game (and in LE2, Space and X skip nothing). The pause screen's A and Y are then drawn discs, and the log has a warning. |

## Known issues

- Loading-screen movies do not pause.
- In LE1, Esc skips a fullscreen movie that the engine lets the player skip, and Pause Anytime cannot pause such a
  movie. In LE2 and LE3, a movie whose sound Pause Anytime does not know keeps vanilla Esc and Start. In LE3 that
  includes every movie played inside a cutscene or a conversation.
- Esc in the open console closes the console, as in the vanilla game.
- During a movie, Esc is read from the keyboard. A press while another program's window is in front is ignored. Any Esc
  pressed while one of the game's windows is in front toggles the movie pause, also one meant to close the console or
  the Steam overlay.
- A key pressed in the first quarter second of a movie is not taken.
- During a fullscreen movie, the Esc and Start events that get past the engine no longer reach the game's own key
  bindings, outside the console.
- During a movie, the engine takes Start's press, so Start acts on its release (a tap) or its first repeat (held). If a
  Start release is lost, for example to a focus change, a Start event more than half a second after the last one counts
  as a new press.
- In a conversation or a cutscene, a menu with input focus can take Esc before Pause Anytime sees it. Then nothing
  pauses.
- With a menu open over a fullscreen movie, Esc and Start go to the movie.
- A window resized while a movie is paused keeps the old frame until the movie resumes.
- The fallback banner is not drawn during a movie pause: the HUD does not draw over a fullscreen movie.

To report a problem, open an [issue](https://github.com/Maxgoods62/mele-pause-anytime/issues) with
`Logs\PauseAnytime.log` attached. Pause Anytime rewrites it each time the game starts, so copy it before you start the
game again.

## How it works

Pause Anytime hooks the game's UnrealScript calls (`ProcessEvent`), a few of its native functions, the Bink 2 movie
library and the DXGI swap chain. In a conversation or a cutscene it sets the game's own world pause, which stops the
picture, the voice-over, the music and the lip sync together. A Bink movie keeps playing under that pause, so
Pause Anytime pauses the movie through Bink and reads Esc on the movie's own thread. The pause screen is drawn into the
game's back buffer just before each frame is shown.

The details, hook by hook: [docs/how-it-works.md](docs/how-it-works.md). A checklist for testing a build in the game,
with the log lines to look for: [docs/testing.md](docs/testing.md). Changes per version: [CHANGELOG.md](CHANGELOG.md).

## License

GPL-3.0 ([`LICENSE`](LICENSE)). The Exo 2 font is under the SIL Open Font License 1.1 ([`src/fonts/OFL.txt`](src/fonts/OFL.txt)). Third-party code and
fonts, and what Pause Anytime uses them for: [`THIRD_PARTY.md`](THIRD_PARTY.md).
