# MelePause in LE3

Status: from v11, LE3 is in `RELEASED_GAMES` (`scripts/games.sh`), so a plain `./build.sh` or `./package.sh` covers it.
`./build.sh --game LE3` builds `LE3MelePause.asi` from the same sources as LE1 and LE2, against LE3's SDK, with the
movie pause off. On 2026-10-30 the first build passed the pause part of the sheet below with keyboard and mouse and with
a gamepad, and from v12 photo mode under the pause passed in conversations ([testing.md](../testing.md#le3)).

## What the build has

| Part | LE3 build | LE1 build |
|---|---|---|
| Esc and Start pause a conversation or an in-engine cutscene; skip guard; Space and A resume | compiled in | on |
| Pause screen, and the Canvas banner where it cannot draw | compiled in | on |
| Movie pause (`MELEPAUSE_MOVIES`) | off: neither `Bink::Install` nor `MovieInput::Install` is called, and Esc and Start in a movie stay the game's | on |
| Photo mode under the pause (`MELEPAUSE_PHOTO`) | on from v12, its GUI side in `PhotoGuiGFx.cpp` (`MELEPAUSE_PHOTO_GFX`); its GUI sounds get no hook | on, `PhotoGuiSF.cpp` |
| File | `LE3MelePause.asi`, Windows version `12.0.6.0`, for `Game\ME3\Binaries\Win64\ASI` | `LE1MelePause.asi`, `12.0.4.0` |

Why the movie pause is off: 125 of the 126 movies in `BioGame\Movies` have no Bink audio track. As in LE2, their sound
is a Wwise event, which `BinkPause` does not stop: the picture would hold while the sound plays on. LE2's way, pausing
the event of the Kismet action that plays the movie, needs LE3's movie actions and the engine's movie input checked
first.

## Photo mode

LE3's photo panel is a GFx movie (`SFXSFHandler_PhotoMode`, `SFXGame_classes.hpp:15611`), opened by
`SFXGUIInteraction.ShowPhotoMode(PC)` (`EnableMode(Photo)` and `OpenMovie`) and closed by `HidePhotoMode(PC)`. The
conversation's panel is a movie too, `BioSFHandler_Conversation` from `GetConversationHandler(PC)`: its visibility and
input live in the native movie, so `PhotoGuiGFx.cpp` keeps them with `GetVisible`/`GetInputEnabled`, hides it with
`SetVisible(FALSE)` and `SetInputEnabled(FALSE)`, and gives them back with calls at the exit. The handler's
`SetVisible(FALSE)` clears `m_bDisplayingWheel` and `m_bDisplayInvestigateSubMenu`: they are written back after the
calls. With the reply wheel up, the game shows the movie again on its own while photo mode runs, often for many frames
in a row: `PhotoGui::Hold` hides it again each frame, and the exit line counts those frames.

The natives, found through the classes' vtables in a memory dump of the running game (MassEffect3.exe of 2026-10-30):

| Native | RVA | What it does under the pause |
|---|---|---|
| `BioPlayerInput::InputKey`, slot 88 (LE2: 89) | 0xa5a040 | Hands the key to `SFXGUIInteraction` first (0xbc9be0), then to `UPlayerInput::InputKey` (0x54b710) and `UInput::InputKey` (0x54b2f0), which drops presses while `WorldInfo.Pauser` (+0x670) is set. Hooked as in LE2: the `Pauser` is cleared around the call. |
| `UPlayerInput::InputAxis`, slot 89, not overridden | 0x54ac90 | Skips the axis binds while the `Pauser` is set (0x54ad9e). Same first bytes as LE2's. Hooked as in LE2. |
| The GFx movies' `Tick`, slot 18 of vtable 0x12c7e48, called by the viewport client's tick (0xc25695) | 0x92d7a0 | Checks no `Pauser`. It advances each movie by the time since its last advance on the movie's clock: real time, or for most movies the world's `TimeSeconds` (0x8c2570, +0x74c), which stands still under the pause. Each movie got `Advance(0)`: photo mode's movie never played the frame that tells the game it loaded (`MovieLoaded`, then `InitializePhotoMode`), so its labels stayed "TEST STRING", and the conversation movie's hide was never drawn. |

While photo mode is on, `GfxClock` (`Photo.cpp`) moves `TimeSeconds` by the real time since the last call around each
call of the GFx movies' `Tick`, then puts back the movies' stamps it wrote with that time (`Game::GfxMovieLists`, the
stamp at +0x44 of each entry) and `TimeSeconds`: the next call measures from the world's clock again. Nothing else sees
the clock move. `SFXGUIInteraction`'s own tick (slot 87, 0xbcbf70) skips only its queued message boxes under the pause
(`IsPaused`, 0x593f40).

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
- S1, conversation. Esc pauses: picture, voice, music and lips stop, and the pause screen shows "Space Resume" and "F7
  Photo mode". Esc resumes at the same word, in sync. Then Start, with the A icon on the pause screen; A resumes.
- S2, in-engine cutscene. The same. Note what Esc, Space and X do in a skippable cutscene with nothing paused (the log
  names each `SkipCinematic` it passes): the Esc that pauses and the Space that resumes must not skip it.
- S3, fullscreen movie. Esc and Start do what they do in the vanilla game, and the world does not pause under the movie.
- S4, gameplay. Esc and Start open the pause menu and close it.
- S5, resume keys and the console. Pause with Esc and resume with Space, pause with Start and resume with A. With
  nothing paused, Space skips the line. Open the console during a pause: the pause screen hides, and Esc closes the
  console ("the console is open, vanilla" in the log).
- S6, photo mode under the pause, in a conversation with the reply wheel up and without, then in a cutscene. Y or F7
  opens it on the paused shot: its panel shows its tabs and options (no "TEST STRING"), the wheel is gone, the camera
  answers the mouse, the keys and both sticks. Y, F7, Esc or Start leaves it: the paused shot, the wheel and the pause
  screen come back.
- S7, the log. At start: "movie pause: not in the LE3 build" and no Bink line; "input hooked: ..." and "GUI tick
  hooked: ..."; "overlay: swap chain hooks Present true ..."; a line that ends in "LE3MelePause N ready" (N is
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
5. Photo mode in a cutscene. The first photo build opened it on a cutscene's shot (camera and keys answered, the panel
   did not load yet); v12's panel fix was tested in conversations only.
6. Photo mode's GUI sounds under the pause (click, tabs, options): LE3's movies play them through `SFXGUIInteraction`,
   with no hook. Not checked on purpose yet.
