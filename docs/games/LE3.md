# MelePause in LE3

Status: from v11, LE3 is in `RELEASED_GAMES` (`scripts/games.sh`), so a plain `./build.sh` or `./package.sh` covers it.
`./build.sh --game LE3` builds `LE3MelePause.asi` from the same sources as LE1 and LE2, against LE3's SDK, with photo
mode and the movie pause off. On 2026-10-30 the first build passed the pause part of the sheet below with keyboard and
mouse and with a gamepad ([testing.md](../testing.md#le3)).

## What the build has

| Part | LE3 build | LE1 build |
|---|---|---|
| Esc and Start pause a conversation or an in-engine cutscene; skip guard; Space and A resume | compiled in | on |
| Pause screen, and the Canvas banner where it cannot draw | compiled in | on |
| Movie pause (`MELEPAUSE_MOVIES`) | off: neither `Bink::Install` nor `MovieInput::Install` is called, and Esc and Start in a movie stay the game's | on |
| Photo mode under the pause (`MELEPAUSE_PHOTO`) | off: `PhotoOff.cpp`. Y and F7 stay the game's, and the pause screen offers no photo key | on |
| File | `LE3MelePause.asi`, Windows version `11.0.6.0`, for `Game\ME3\Binaries\Win64\ASI` | `LE1MelePause.asi`, `11.0.4.0` |

Why the two switches are off:

- Movie pause: 125 of the 126 movies in `BioGame\Movies` have no Bink audio track. As in LE2, their sound is a Wwise
  event, which `BinkPause` does not stop: the picture would hold while the sound plays on. LE2's way, pausing the event
  of the Kismet action that plays the movie, needs LE3's movie actions and the engine's movie input checked first.
- Photo mode: `Photo.cpp` hooks MassEffect<n>.exe natives by address and checks their first bytes; MassEffect3.exe's are
  not known. LE3's photo panel is a GFx movie (`SFXSFHandler_PhotoMode`, `SFXGame_classes.hpp:15611`), not LE1's and
  LE2's `BioSFPanel`.

## Facts

`src/game/LE3.hpp` holds these. The file and line are in the LE3 SDK of the LExASIs commit `build.sh` pins
(`External/LExSDKv2/Src/LESDK/LE3/`). The binds are the defaults of `Default__BioPlayerInput` in the game's
`SFXGame.pcc`.

| What | LE3 | LE1 | Where (LE3) |
|---|---|---|---|
| Game modes, `SFXGameModeManager.EGameModes` | 0 Default, 1 Vehicle, 2 Atlas, 3 PowerWheel, 4 WeaponWheel, 5 Command, 6 InjuredShepard, 7 Conversation, 8 Cinematic, 9 GUI, 10 Movie, 11 Galaxy, 12 Orbital, 13 MultiLand, 14 Photo, 15 CheatMenu, and nine more to 24 Lobby | 5 Conversation, 6 Cinematic, 8 Movie, 11 Photo | `SFXGame_classes.hpp:3210-3239` |
| The controller's game modes | `BioPlayerController.GameModeManager2` | `GameModeManager` | `SFXGame_classes.hpp:7610` |
| `GuiKey` events of the always-on Start and Escape binds, pressed and released (`BioPlayerInput.StaticConsoleBinds`, `StaticPCBinds`) | 24, 16, 45, 37: Escape sends `BUTTON_B`, as in LE2 | 21, 24, 43, 46: Escape sends `KEY_ESCAPE` | `SFXGame_classes.hpp:1126` (`BioGuiEvents` is in SFXGame here); binds `:17765-17766` |
| Photo mode's Esc and Start bind | `SFXGameModePhoto.ShowMenu` | `ShowMenuGui` | `SFXGame_classes.hpp:31027` |
| An older file of ours in the ASI folder | none (`LegacyModule` is null) | `MelePause.asi` (version 6 and older) | |
| Cutscene skip binds (`SFXGameModeCinematic`) | Escape and SpaceBar: `PC_CineSkip`; XboxTypeS_X: `Console_CineSkip` | Esc | `Default__SFXGameModeCinematic` in `SFXGame.pcc` |

The rest of the shared code's names (`WorldInfo.Pauser`, `BioHUD`, `SFXConsole`, `SFXPlayerCamera`, the Console's
`InputKey`) are in LE3's SDK under the same names: the build compiled with no change to a shared file.

## Sheet for a first run

The procedure is in [testing.md](../testing.md), with `--game LE3`. `--install` needs the ASI loader in ME3
(`bink2w64_original.dll` in `Game\ME3\Binaries\Win64`), which ME3Tweaks Mod Manager installs with any mod. The log is
`Game\ME3\Binaries\Win64\Logs\MelePause.log`. Run it all, with keyboard and mouse and with a gamepad.

- L0, before MelePause is installed. Open the Steam overlay (Shift+Tab) for 15 s during a conversation line, then during
  an in-engine cutscene. In LE1 and LE2 the overlay sets the world pause MelePause uses (`WorldInfo.Pauser`). Note
  whether voice, lips and music stop together and resume in sync.
- S1, conversation. Esc pauses: picture, voice, music and lips stop, and the pause screen shows "Space Resume" with no
  photo key. Esc resumes at the same word, in sync. Then Start, with the A icon on the pause screen; A resumes.
- S2, in-engine cutscene. The same. Note what Esc, Space and X do in a skippable cutscene with nothing paused (the log
  names each `SkipCinematic` it passes): the Esc that pauses and the Space that resumes must not skip it.
- S3, fullscreen movie. Esc and Start do what they do in the vanilla game, and the world does not pause under the movie.
- S4, gameplay. Esc and Start open the pause menu and close it.
- S5, resume keys and the console. Pause with Esc and resume with Space, pause with Start and resume with A. With
  nothing paused, Space skips the line. Open the console during a pause: the pause screen hides, and Esc closes the
  console ("the console is open, vanilla" in the log).
- S6, Y and F7 under the pause. They do what they do in the vanilla game, and the log has no line for them.
- S7, the log. At start: "movie pause: not in the LE3 build" and no Bink line; "photo mode under the pause: not in the
  LE3 build"; "overlay: swap chain hooks Present true ..."; a line that ends in "LE3MelePause N ready" (N is
  `ASI_VERSION`). At the first frame, "first BioHUD.PostRender" with the same controller for the HUD owner and GEngine.
  Each press: "Esc pressed: mode 7 (Conversation)" and the like, with LE3's mode names.

## Open questions

1. *Answered 2026-10-30: yes.* Sync. Does the world pause stop LE3's conversations (`BioConversationController`) and
   keep voice, lips and music in sync?
2. *Answered 2026-10-30: yes.* The HUD. LE3's HUD draws through GFx. Does `BioHUD.PostRender` still pass through
   `ProcessEvent` every frame in conversations and cutscenes? The per-frame checks and the pause screen rest on it.
3. *Answered 2026-10-30.* Esc in a skippable cutscene. Escape, SpaceBar and X are LE3's skip keys there (Facts). Esc
   pauses, as in LE2, and the press that paused or resumed did not skip.
4. The console. Does the `Typing`/`Open` check through `Console.StateFrame` hold for LE3's `SFXConsole`
   (`SFXGame_classes.hpp:20022`)?
