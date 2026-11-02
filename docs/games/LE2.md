# Pause Anytime in LE2

Status: from v8, LE2 is in `RELEASED_GAMES` (`scripts/games.sh`), so a plain `./build.sh` or `./package.sh` covers it.
From v9 it has the movie pause, and from v10 photo mode under the pause. `./build.sh --game LE2` builds
`LE2PauseAnytime.asi` from the same sources as LE1, against LE2's SDK, with the movie pause limited to movies whose
sound Pause Anytime knows. On 2026-10-29 the v7 build passed the sheet below with keyboard and mouse and with a gamepad
([testing.md](../testing.md#le2)). On 2026-10-30 a test build of the movie pause passed S3 in the relay movie and the
Citadel arrival, and a test build of photo mode passed S6 in conversations ([testing.md](../testing.md#le2)). The game's
look ([Pause screen](#pause-screen)) came with v17, which passed in a conversation on 2026-10-31
([testing.md](../testing.md#le2)).

## What the build has

| Part | LE2 build | LE1 build |
|---|---|---|
| Esc and Start pause a conversation or an in-engine cutscene; skip guard; Space and A resume | compiled in | on |
| Pause screen, and the Canvas banner where it cannot draw | on, from v17 in the game's menu colours, with its words for the labels, its menu sound, its gamepad buttons and its fonts | on, from v14 in the game's menu colours, with the game's words and its menu sound, from v15 with its gamepad buttons, from v16 in its fonts |
| Movie pause | on for a movie whose sound Pause Anytime knows (below); Esc and Start in any other movie stay the game's. Space and X skip a movie the game lets skip | on |
| Photo mode under the pause | on, with three MassEffect2.exe natives ([Photo mode](#photo-mode)) | on |
| Photo mode's GUI sounds made realtime (`PAUSEANYTIME_PHOTO_GUI_SOUNDS`) | off: they are Wwise events and play under the world pause as they are | on |
| File | `LE2PauseAnytime.asi`, Windows version `18.0.5.0`, for `Game\ME2\Binaries\Win64\ASI` | `LE1PauseAnytime.asi`, `18.0.4.0` |

Why the movie pause takes only some movies:

- Movie pause: 164 of the 166 movies in `BioGame\Movies` have no Bink audio track, and none of the DLC's 45 has one.
  Their sound is a Wwise event, which `BinkPause` does not stop: the picture would hold while the sound plays on. Only
  `load_f01_a.bik` and `visionavi.bik` carry their own sound. So a movie takes Esc and Start only when Pause Anytime
  knows its sound (`MoviePolicy::BinkAndEvent` in `src/game/LE2.hpp`): the Wwise event of the Kismet
  `BioSeqAct_MovieBink` that plays it, paused with the exe's `AK::SoundEngine::ExecuteActionOnEvent` right after
  `BinkPause`, or Bink audio (`visionavi`). Any other movie keeps vanilla Esc and Start: Esc skips a movie the game lets
  you skip.
- A movie's sound is known when the Kismet action that plays it posts an event and runs nothing else beside the movie
  that may make a sound (`MovieActionOf` in `src/game/KismetMovie.hpp`, shared with LE3): its `Out` and `HasStarted` outputs, the ops started at
  the same moment, and the Matinee or conversation line whose event starts it. Of the 117 movie actions with an event,
  71 pass, among them the relay jump and movies of the Citadel, Omega, Tuchanka and Illium hubs.
- In LE2 the engine's movie input skips a skippable movie on Esc and Start, before Pause Anytime sees the press, and
  Space and X skip nothing ([Movie keys](#movie-keys)). `src/MovieInput.cpp` hooks that input: on a movie whose sound is
  known, Esc and Start pause it, and Space and X skip a movie the game lets you skip, as Esc did.

## Facts

`src/game/LE2.hpp` holds these. The file and line are in the LE2 SDK of the LExASIs commit `build.sh` pins
(`External/LExSDKv2/Src/LESDK/LE2/`). The binds are from the game's `BioGame\CookedPCConsole\Coalesced_INT.bin`.

| What | LE2 | LE1 | Where (LE2) |
|---|---|---|---|
| Game modes, `SFXGameModeManager.EGameModes` | 0 Default, 1 PowerWheel, 2 WeaponWheel, 3 Command, 4 Vehicle, 5 Conversation, 6 Cinematic, 7 GUI, 8 Movie, 9 Galaxy, 10 Orbital, 11 Photo, 12 CheatMenu | The same numbers for Conversation, Cinematic, Movie and Photo. Vehicle is 1, and 10 and 12 are Minigame and FlyCam | `SFXGame_classes.hpp:4054-4071` |
| The controller's game modes | `BioPlayerController.GameModeManager2` | `GameModeManager` | `SFXGame_classes.hpp:5535` |
| `GuiKey` events of the always-on Start and Escape binds, pressed and released (`BioPlayerInput.StaticConsoleBinds`, `StaticPCBinds`) | 21, 13, 42, 34: Escape sends `BUTTON_B` | 21, 24, 43, 46: Escape sends `KEY_ESCAPE` | `Engine_classes.hpp:1085, 1093, 1106, 1114`; binds `SFXGame_classes.hpp:13169-13170` |
| Photo mode's Esc and Start bind (`PC_Menu`, `Console_Menu`) | `SFXGameModePhoto.ShowMenu` | `ShowMenuGui` | `SFXGame_classes.hpp:27326` |
| Older files of ours in the ASI folder (`LegacyModules`) | `LE2MelePause.asi` (versions 17 and older) | `LE1MelePause.asi` (versions 7 to 17), `MelePause.asi` (version 6 and older) | |

Where LE2's binds differ from what the shared code expects:

- In Cinematic mode, Esc, Space and a gamepad's X run `SkipCinematic`: Esc skips a skippable in-engine cutscene in the
  vanilla game. Pause Anytime takes that Esc to pause, and its skip guard drops the skip while the key is down and for
  250 ms after its release.
- BioPlayerInput's always-on binds send Escape as `GuiKey BIOGUI_EVENT_BUTTON_B` (13, released 34), where LE1's send
  `KEY_ESCAPE`; Start sends `BUTTON_START` (21, released 42) in both. The skip guard drops those four while a press
  Pause Anytime took is down.

### Movie keys

From a read-only dump of the running MassEffect2.exe (its `.text` is encrypted on disk). RVAs are the exe's.

| What | Where | What it does |
|---|---|---|
| `SFXGameModeMovie`'s binds | Escape and SpaceBar → `PC_MovieSkip`, pad X → `Console_MovieSkip` | Both run `SkipMovie`, which calls the static native `SFXEngine.SkipMovie`. |
| `SFXEngine.SkipMovie` | `0xB3DCE0` | Calls slot 27 of the movie player (`GFullScreenMovie`, `0x1B652B0`), an empty function. In LE2, `SkipMovie` does nothing. |
| The engine's movie input | `0x2334d0`, slot 5 of the Bink movie player's `FViewportClient` vtable (`0x101D3F8`) | Every key event reaches it before `Console.InputKey`. On a press (`IE_Pressed`) of Escape, Enter, LeftMouseButton, XboxTypeS_Back, XboxTypeS_A or XboxTypeS_Start (A and Start unless the movie's flags say not), it asks the player to stop the movie (slot 41 of its main vtable) and returns TRUE: `Console.InputKey` never sees that press. On any other key it returns FALSE and the key goes on. |
| Which movies skip | `[FullScreenMovie] SkippableMovies` in Coalesced, 73 names | `GLO_01_Relay` is not among them. The movie input still takes its skip keys (the log shows Space-as-Enter taken), and the relay plays on: the list is checked after the input, where was not traced. |

`src/MovieInput.cpp` hooks the movie input after checking its first 28 bytes (`Game::MovieInputRva`,
`MovieInputStart`). On a press:

- Esc or Start while the pause keys are the movies' (`Bink::OwnsKeys`): the original is not called and the hook returns
  FALSE. The press goes on to `Console.InputKey`, where it pauses or resumes the movie. Before the hook, only Start's
  repeats and release got there, and the movie's thread read Esc by itself.
- Any key while Pause Anytime's movie pause is on: the same, so no key skips a paused movie.
- Space or X: the original is called with Enter in its place, so they skip what Esc skipped, and only that.
- Anything else: the original, unchanged.

Seen in the game:

- The world pause holds the picture, voice, lips and music together, and they resume in sync. The pauser is the
  player's `SFXPRI`.
- Keys reach `SFXGame.SFXConsole.InputKey`. With the console open, Esc comes to `SFXConsole.Open.InputKey` and closes
  it, as in LE1, and the per-frame console check (`Console.StateFrame`, `Typing` or `Open`) holds.
- `BioHUD.PostRender` runs every frame in conversations and in-engine cutscenes: the pause screen shows in both.

### Photo mode

`src/Photo.cpp` serves both games; `src/game/LE2.hpp` holds what differs. The natives come from the same dump, each
found through its class's vtable: the class name's string leads to its `StaticClass`, that to the constructor, and the
constructor writes the vtable. LE2's `BioPlayerInput` slots sit one below LE1's. The hooks check each function's first
bytes (`Game::Photo*Start`) before they patch it.

| What | LE2 | LE1 |
|---|---|---|
| `BioPlayerInput::InputKey` | `0xa62e00`, slot 89 of the vtable at `0x1261688`. It offers the key to the GUI, then calls `UPlayerInput::InputKey` (`0x542220`) and `UInput::InputKey` (`0x541e00`), which drops it under `WorldInfo.Pauser` | `0xc6de70` |
| `UPlayerInput::InputAxis` | `0x541790`, slot 90, not overridden. Its `Pauser` check is at `0x54189f` | `0x68bc40` |
| `UBioSFManager::Tick` | `0x1a07c0`. `MassEffectGuiManager`'s override (`0xb27820`) does its own part only while the world runs, then jumps there | `0x193bc0` |
| GUI sounds | Wwise events: they play under the world pause, no hook | `PlayGuiSound` and `UAudioDevice::CreateComponent`, the component marked `bNonRealtime` |
| The conversation panel | `MassEffectGuiManager.m_oConversationHandler.oPanel` | `m_oConversationPanel` |
| The camera switch gate | `SFXPlayerCamera.UpdateViewTarget` calls `PickCameraMode` only when `TimeSeconds - LastTransitionTime > TransitionTimeout` (0.25 s) | in `PickCameraMode`, `GameTimeSeconds`, 0.1 s |
| The subtitle at `SFXGameModePhoto.Activated` | kept | cleared |

The camera seen in the game: entering from a director's shot (a `CameraActor` view target), the camera mode left
behind is the gameplay camera, `SFXCameraMode_Explore`. The switch to the photo camera is an instant transition whose
start turns the controller to that camera's view, 24 degrees off the shot in one log; the entry puts it back on the
shot ([how-it-works.md](../how-it-works.md#photo-mode-entering), step 7).

## Pause screen

`src/game/LE2.hpp` holds the names and ids, `Le2Look` in `src/Overlay.cpp` the colours and sizes. All of them were read
from the game's files, and none of those files is shipped.

| What | LE2 | LE1 |
|---|---|---|
| Colours | title `#FFCC66`, labels `#FF9142` (`PC_MainWheel`), rules `#FF9C5B`, band `#5F2C0C` to `#843E14` (sampled from the game's wheel on screen, its fills in the movie are the same family), blur toward warm | title `#CCFFFF`, labels and rules `#3199C6`, band `#174868` to `#226E8F`, blur toward blue |
| Title | $BioMass 49.5 at 1080 lines, spacing 1.5: 1.5 times `PC_MainWheel`'s `guiTitleText` (22 at 720 lines, spacing 1) | $AeroLight 67.5, as `WindowPop` sets it |
| Fonts | `GUI_SF_Fonts.AeroLightFont_glyphs` (Myriad Pro+DFGGothicP-W5) and `.BioMassFont_glyphs` (Biomass+DFGSoGei-W7), byte-identical in all 8 languages. `AeroLightFont` and `BioMassFont` hold no outlines | `GUI_SF_Fonts.Fonts`, both in one movie |
| Words | resume 157151, photo mode 392221, title ours: 170645 is "No Data" (`WindowPop`'s paused box is dead in LE2) and 174771 "Pause" stays English in Russian, Polish and Japanese | 170645, 157151, 184397 |
| Gamepad buttons | `GUI_SF_Xbox_ControllerIcons.Xbox_ControllerIcons_IA` and `_I1`, 128x128, disc 93 texels across, centred at (61, 55.5): off-centre, so `ButtonArt` places it | `GUI_SF_SharedAssets.XBox_ControllerIcons_I4` and `_I9`, disc 95, centred |
| Sound | `BioSFManager.PlaySound` with `InGameGuiEnter` and `InGameGuiExit`, as `MassEffectGuiManager.ShowBrowserWheel` and `HideBrowserWheel`: Wwise events that also hold and give back the game's audio ([World pause](../how-it-works.md#world-pause)) | `PlayGuiSound` with the same names, through the GUI sound hooks |
| `GetSimpleString` | `SFXGame`, which `BioSPGame` extends | `BioSPGame` |

## Sheet for a first run

The procedure is in [testing.md](../testing.md), with `--game LE2`. `--install` needs the ASI loader in ME2
(`bink2w64_original.dll` in `Game\ME2\Binaries\Win64`), which ME3Tweaks Mod Manager installs with any mod. The log is
`Game\ME2\Binaries\Win64\Logs\PauseAnytime.log`. Run it all, with keyboard and mouse and with a gamepad.

- L0, before Pause Anytime is installed. Open the Steam overlay (Shift+Tab) for 15 s during a conversation line, then
  during an in-engine cutscene. In LE1 the overlay sets the world pause Pause Anytime uses (`WorldInfo.Pauser`). Note
  whether voice, lips and music stop together and resume in sync.
- S1, conversation. Esc pauses: picture, voice, music and lips stop, and the pause screen shows "Space Resume" and "F7
  Photo mode". Esc resumes at the same word, in sync. Then Start, with the A icon on the pause screen; A resumes.
- S2, in-engine cutscene. The same. Esc and Space are the cutscene's skip keys in LE2: the Esc that pauses and the Space
  that resumes must not skip it. With nothing paused, Space and X skip a skippable cutscene as in the vanilla game.
- S3, fullscreen movie. In the relay jump and the Citadel arrival or departure, Esc pauses the picture and the sound
  together and Esc resumes them in sync; the same with Start, and A resumes. Five pauses in one movie, and one held 15
  s: no offset. Space and X skip the Citadel movie and do nothing in the relay jump. Under the pause, X, Enter and A
  skip nothing. In a movie with no known sound ("no known sound" in its fullscreen line), Esc skips it as in the
  vanilla game. The world never pauses under a movie. In the Kasumi DLC's montage, note what Esc and Start do
  ([question 2](#open-questions)).
- S4, gameplay. Esc and Start open the pause menu and close it.
- S5, resume keys and the console. Pause with Esc and resume with Space, pause with Start and resume with A. With
  nothing paused, Space skips the line. Open the console during a pause: the pause screen hides, and Esc closes the
  console ("the console is open, vanilla" in the log).
- S6, photo mode, as in [testing.md](../testing.md)'s S6, with Y and F7, in a conversation and in an in-engine cutscene.
  The first press of a pause opens on the paused shot, facing the same way: the "photo mode on" line gives 0 uu and 0
  deg for the view. Its clicks and screenshot sound play.
- S7, the log. At start: "Bink: ... hooked", "movie sound: the exe's AK::SoundEngine::ExecuteActionOnEvent at ...",
  "movie input hooked"; "input hooked: ...", "GUI tick hooked: ..." and "GUI sounds: not hooked in the LE2 build, ...";
  "overlay: swap chain hooks Present true ..."; a line that
  ends in "LE2PauseAnytime N ready" (N is `ASI_VERSION`). At the first frame, "first BioHUD.PostRender" with the same
  controller for the HUD owner and GEngine. Each press: "Esc pressed: mode 5 (Conversation)" and the like, with LE2's
  mode names.
- S8, the look. The title on a brown band in gold, the labels in orange, each key left of its label. With a gamepad, the
  game's A and Y at the height of their labels, not drawn discs. "overlay ready: ..., LE2 look", "overlay: the game's
  gamepad icons: A fine, Y fine: used", "overlay: the game's fonts: ... used", "overlay: the game's texts: title \"\" kept
  ours, resume ... used, photo mode ... used", and "GUI sound InGameGuiEnter: played" after "world paused".
- S9, the Steam overlay, as in [testing.md](../testing.md)'s S9. The line goes on to its end. "game audio released: the
  Steam overlay is open, the world still held" with its "GUI sound InGameGuiExit: played", then "world pause ended".

## Open questions

1. *Answered 2026-10-30: yes.* The HUD over a fullscreen movie. Does `BioHUD.PostRender` stop there? LE1's movie pause
   tells a fullscreen movie by that silence. The relay and Citadel movies were fullscreen 0.2 s after their open.
2. Movies in Matinee. The Kasumi DLC (`DLC_HEN_MT`: `Montage_1.bik` and others) and `DLC_METR_Patch01` play movies from
   a Matinee track, `SFXInterpTrackMovieBink` (`SFXGame_classes.hpp:15489`). Kismet's `BioSeqAct_MovieBink` turns Movie
   mode on, so Esc stays the game's there; whether this track does is unknown. If it does not, the mode stays Cinematic,
   and only the engine's movie input, which takes Esc's press first, keeps Esc from pausing the world under a movie that
   plays on.
