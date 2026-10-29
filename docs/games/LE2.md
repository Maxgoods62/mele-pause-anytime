# MelePause in LE2

Status: runs, not released. `./build.sh --game LE2` builds `LE2MelePause.asi` from the same sources as LE1, against
LE2's SDK, with photo mode and the movie pause off. On 2026-10-29 the v7 build passed the sheet below with keyboard and
mouse and with a gamepad ([testing.md](../testing.md#tested-so-far)). LE2 is not a released game (`RELEASED_GAMES` in
`scripts/games.sh`): a plain `./build.sh` or `./package.sh` leaves it out, and `package.sh` has no `README.txt` text for
it.

## What the build has

| Part | LE2 build | LE1 build |
|---|---|---|
| Esc and Start pause a conversation or an in-engine cutscene; skip guard; Space and A resume | compiled in | on |
| Pause screen, and the Canvas banner where it cannot draw | compiled in | on |
| Movie pause (`MELEPAUSE_MOVIES`) | off: `Bink::Install` is not called, and Esc and Start in a movie stay the game's | on |
| Photo mode under the pause (`MELEPAUSE_PHOTO`) | off: `PhotoOff.cpp`. Y and F7 stay the game's, and the pause screen offers no photo key | on |
| File | `LE2MelePause.asi`, Windows version `7.0.5.0`, for `Game\ME2\Binaries\Win64\ASI` | `LE1MelePause.asi`, `7.0.4.0` |

Why the two switches are off:

- Movie pause: 164 of the 166 movies in `BioGame\Movies` have no Bink audio track, and none of the DLC's 45 has one.
  Their sound is a Wwise event, which `BinkPause` does not stop: the picture would hold while the sound plays on. Only
  `load_f01_a.bik` and `visionavi.bik` carry their own sound.
- Photo mode: `PhotoLE1.cpp` hooks five MassEffect1.exe natives by address and checks their first bytes against that
  executable. MassEffect2.exe's are not known. The file also uses names LE2's SDK does not have:
  `WorldInfo.GameTimeSeconds` (LE2's `WorldInfo` has `TimeSeconds` and `RealTimeSeconds`,
  `Engine_classes.hpp:5125-5126`), `AudioComponent.bNonRealtime`, and `MassEffectGuiManager.m_oConversationPanel`.

## Facts

`src/game/LE2.hpp` holds these. The file and line are in the LE2 SDK of the LExASIs commit `build.sh` pins
(`External/LExSDKv2/Src/LESDK/LE2/`). The binds are from the game's `BioGame\CookedPCConsole\Coalesced_INT.bin`.

| What | LE2 | LE1 | Where (LE2) |
|---|---|---|---|
| Game modes, `SFXGameModeManager.EGameModes` | 0 Default, 1 PowerWheel, 2 WeaponWheel, 3 Command, 4 Vehicle, 5 Conversation, 6 Cinematic, 7 GUI, 8 Movie, 9 Galaxy, 10 Orbital, 11 Photo, 12 CheatMenu | The same numbers for Conversation, Cinematic, Movie and Photo. Vehicle is 1, and 10 and 12 are Minigame and FlyCam | `SFXGame_classes.hpp:4054-4071` |
| The controller's game modes | `BioPlayerController.GameModeManager2` | `GameModeManager` | `SFXGame_classes.hpp:5535` |
| `GuiKey` events of the always-on Start and Escape binds, pressed and released (`BioPlayerInput.StaticConsoleBinds`, `StaticPCBinds`) | 21, 13, 42, 34: Escape sends `BUTTON_B` | 21, 24, 43, 46: Escape sends `KEY_ESCAPE` | `Engine_classes.hpp:1085, 1093, 1106, 1114`; binds `SFXGame_classes.hpp:13169-13170` |
| Photo mode's Esc and Start bind (`PC_Menu`, `Console_Menu`) | `SFXGameModePhoto.ShowMenu` | `ShowMenuGui` | `SFXGame_classes.hpp:27326` |
| An older file of ours in the ASI folder | none (`LegacyModule` is null) | `MelePause.asi` (version 6 and older) | |

Where LE2's binds differ from what the shared code expects:

- In Cinematic mode, Esc, Space and a gamepad's X run `SkipCinematic`: Esc skips a skippable in-engine cutscene in the
  vanilla game. MelePause takes that Esc to pause, and its skip guard drops the skip while the key is down and for 250
  ms after its release.
- BioPlayerInput's always-on binds send Escape as `GuiKey BIOGUI_EVENT_BUTTON_B` (13, released 34), where LE1's send
  `KEY_ESCAPE`; Start sends `BUTTON_START` (21, released 42) in both. The skip guard drops those four while a press
  MelePause took is down.

Seen in the game:

- The world pause holds the picture, voice, lips and music together, and they resume in sync. The pauser is the
  player's `SFXPRI`.
- Keys reach `SFXGame.SFXConsole.InputKey`. With the console open, Esc comes to `SFXConsole.Open.InputKey` and closes
  it, as in LE1, and the per-frame console check (`Console.StateFrame`, `Typing` or `Open`) holds.
- `BioHUD.PostRender` runs every frame in conversations and in-engine cutscenes: the pause screen shows in both.

## Sheet for a first run

The procedure is in [testing.md](../testing.md), with `--game LE2`. `--install` needs the ASI loader in ME2
(`bink2w64_original.dll` in `Game\ME2\Binaries\Win64`), which ME3Tweaks Mod Manager installs with any mod. The log is
`Game\ME2\Binaries\Win64\Logs\MelePause.log`. Run it all, with keyboard and mouse and with a gamepad.

- L0, before MelePause is installed. Open the Steam overlay (Shift+Tab) for 15 s during a conversation line, then during
  an in-engine cutscene. In LE1 the overlay sets the world pause MelePause uses (`WorldInfo.Pauser`). Note whether
  voice, lips and music stop together and resume in sync.
- S1, conversation. Esc pauses: picture, voice, music and lips stop, and the pause screen shows "Space Resume" with no
  photo key. Esc resumes at the same word, in sync. Then Start, with the A icon on the pause screen; A resumes.
- S2, in-engine cutscene. The same. Esc and Space are the cutscene's skip keys in LE2: the Esc that pauses and the Space
  that resumes must not skip it. With nothing paused, Space and X skip a skippable cutscene as in the vanilla game.
- S3, fullscreen movie. Esc and Start do what they do in the vanilla game (Esc skips a movie the game lets you skip),
  and the world does not pause under the movie. In the Kasumi DLC's montage, note what Esc and Start do ([question
  2](#open-questions)).
- S4, gameplay. Esc and Start open the pause menu and close it.
- S5, resume keys and the console. Pause with Esc and resume with Space, pause with Start and resume with A. With
  nothing paused, Space skips the line. Open the console during a pause: the pause screen hides, and Esc closes the
  console ("the console is open, vanilla" in the log).
- S6, Y and F7 under the pause. They do what they do in the vanilla game, and the log has no line for them.
- S7, the log. At start: "movie pause: not in the LE2 build" and no Bink line; "photo mode under the pause: not in the
  LE2 build" and no input, GUI tick or GUI sound hook line; "overlay: swap chain hooks Present true ..."; a line that
  ends in "LE2MelePause 7 ready". At the first frame, "first BioHUD.PostRender" with the same controller for the HUD
  owner and GEngine. Each press: "Esc pressed: mode 5 (Conversation)" and the like, with LE2's mode names.

## Open questions

1. The HUD over a fullscreen movie. Does `BioHUD.PostRender` stop there? The movie pause's test for a fullscreen movie
   rests on it, so this matters once LE2 has the movie pause.
2. Movies in Matinee. The Kasumi DLC (`DLC_HEN_MT`: `Montage_1.bik` and others) and `DLC_METR_Patch01` play movies from
   a Matinee track, `SFXInterpTrackMovieBink` (`SFXGame_classes.hpp:15489`). Kismet's `BioSeqAct_MovieBink` turns Movie
   mode on, so Esc stays the game's there; whether this track does is unknown. If it does not, the mode stays Cinematic,
   and only the engine's movie input, which takes Esc's press first, keeps Esc from pausing the world under a movie that
   plays on.
