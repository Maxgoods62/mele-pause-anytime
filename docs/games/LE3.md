# MelePause in LE3

Status: from v11, LE3 is in `RELEASED_GAMES` (`scripts/games.sh`), so a plain `./build.sh` or `./package.sh` covers it.
`./build.sh --game LE3` builds `LE3MelePause.asi` from the same sources as LE1 and LE2, against LE3's SDK. From v13 it has
the movie pause, limited as in LE2 to movies whose sound MelePause knows. On 2026-10-30 the first build passed the pause
part of the sheet below with keyboard and mouse and with a gamepad, from v12 photo mode under the pause passed in
conversations and in an in-engine cutscene, and a test build of the movie pause passed S3 in the relay jump
([testing.md](../testing.md#le3)). The game's look ([Pause screen](#pause-screen)) came with v18.

## What the build has

| Part | LE3 build | LE1 build |
|---|---|---|
| Esc and Start pause a conversation or an in-engine cutscene; skip guard; Space and A resume | compiled in | on |
| Pause screen, and the Canvas banner where it cannot draw | on, from v18 in the game's menu colours, with its words for the labels, its menu sound, its gamepad buttons and its fonts | on, from v14 in the game's menu colours, with the game's words and its menu sound, from v15 with its gamepad buttons, from v16 in its fonts |
| Movie pause | on from v13 for a movie a level starts whose sound MelePause knows ([Movies](#movies)); Esc and Start in any other movie stay the game's | on |
| Photo mode under the pause | on from v12, its GUI side in `PhotoGuiGFx.cpp` (`MELEPAUSE_PHOTO_GFX`); its GUI sounds get no hook | on, `PhotoGuiSF.cpp` |
| File | `LE3MelePause.asi`, Windows version `18.0.6.0`, for `Game\ME3\Binaries\Win64\ASI` | `LE1MelePause.asi`, `18.0.4.0` |

## Movies

Of the 334 movies in `BioGame\Movies` and the DLC, only `StartupLoop_a` (the startup movie) carries a Bink audio track.
Every other movie's sound is a Wwise event, which `BinkPause` does not stop. One movie player (`GFullScreenMovie`) plays
every fullscreen movie, and starts that event itself once the movie starts, for three callers:

| Caller | Live in the game files | Esc and Start |
|---|---|---|
| Kismet `BioSeqAct_MovieBink` (and `SFXSeqAct_PlatformMovieBink`) | 188 actions, 71 movies; 122 actions name an event | Paused when the sound is known, as in LE2 (below) |
| Matinee track `SFXInterpTrackMovieBink`, inside an in-engine cutscene or a conversation | 442 tracks, 240 movies: the end of the Prologue, the Mars arrival, the summit videos, most of the ending | The game's: nothing latches them |
| Loading screens (`SFXLoadMovieManager`) | 45 movies: the Normandy and shuttle rides, the Citadel's transit | The game's: opened by file handle or from memory, nothing latches them |

A movie takes Esc and Start when MelePause knows its sound (`MoviePolicy::BinkAndEvent`): the Wwise event of the Kismet
action that plays it, paused with `ExecuteActionOnEvent` right after `BinkPause`, when the action runs nothing else
beside the movie that may make a sound (`MovieActionOf`, `src/game/KismetMovie.hpp`, shared with LE2). LE3 has no
`BioSeqAct_StartConversation`: its conversation op is `SFXSeqAct_StartAmbientConv` (`ConversationActionClass`). LE3's
movie action can preload its file (input `Prime`, output `Primed`): neither counts as starting the movie. 114 of the 122
actions with an event pass, 38 movies: the relay jump `GLO_01_Relay` (`Play_cineanim_massrelay`), the first Citadel
arrival, Omega's `OmgIntro_001`-`010`, the Leviathan arrivals, End01's Hammer movies and the 93 memorial flashbacks of the
ending. A Matinee, music, a conversation, a sound loop or a nested sequence runs beside the 8 others (the Mars arrival,
the Citadel DLC's two Normandy movies, `Device_Reveal`, the Kro001 animatic, Hackett's speech intro,
`Lev002_Asteroid_Depart`, `Lev004_WW_ArrivalCrash4`). Only Genesis2Title has localized files (`MovieLanguages`).

### Movie keys

From a read-only dump of the running MassEffect3.exe. RVAs are the exe's.

| What | Where | What it does |
|---|---|---|
| `SFXGameModeMovie`'s binds | Escape and SpaceBar → `PC_MovieSkip`, pad X → `Console_MovieSkip`; Start, A, B and Enter unbound | Both run `SkipMovie`, a script: during a loading movie nothing; else, with the cutscene mode (8) on and its skip allowed, the cutscene skip. LE3 has no native `SkipMovie`. |
| The engine's movie input | `0x24afc0`, slot 5 of the Bink movie player's `FViewportClient` vtable (`0x10DED78`); LE2's first 28 bytes | On a press of Escape, SpaceBar, XboxTypeS_Back, XboxTypeS_X or XboxTypeS_Start (X and Start unless the movie's flags say not), it asks the player to stop the movie and returns TRUE. Enter, A and the left mouse button are not its keys. A movie with flag 0x400 (the relay jump, played under the cutscene mode; the Matinee tracks) or 0x80 ignores every key, which goes on to `SkipMovie`. |
| Which movies skip | `[FullScreenMovie] SkippableMovies` (`BioEngine`): `Device_Reveal`, `ProCit_100_Arrival`, the Cat002 and Cat003 arrivals | Checked when the movie starts (the player's +0x84). `GLO_01_Relay` is not among them. |

`src/MovieInput.cpp` hooks the movie input as in LE2: Esc and Start while the pause keys are the movies', and any key
under MelePause's movie pause, go on to `Console.InputKey` instead. Space and X reach it as they are
(`MovieSkipKeyAs` is null): they are its keys already. The movie lines in the log add the player's flags and skippable
flag (`Game::MoviePlayer`).

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

## Pause screen

`src/game/LE3.hpp` holds the names and ids, `Le3Look` in `src/Overlay.cpp` the colours and sizes. All of them were read
from the game's files, and none of those files is shipped.

| What | LE3 | LE2 |
|---|---|---|
| Colours | title `#FFFFFF` (`MainWheel`'s `guiTitleText`), labels `#ACF5FE` (its buttons), rules `#0896F7` (the edge of `PC_SharedAssets_I46`), band `#08759C` to `#084E7E` (its bars `MainWheel_I1E` and `_I9`, lighter at the top), blur toward blue | title `#FFCC66`, labels `#FF9142`, rules `#FF9C5B`, band `#5F2C0C` to `#843E14`, blur toward warm |
| Title | $BioMass 63 at 1080 lines, spacing 1.5: 1.5 times `guiTitleText`'s 42, spacing 1 | $BioMass 49.5, spacing 1.5 |
| Labels and keys | font and sizes as in LE2, in the colour of the wheel's buttons. The wheel's own prompts differ: $AeroLight 30, `#FFBA65` (`PC_SharedAssets`' `ControlTextLeftAlign`) | $BioMass 22.8, the keys in the light font at 18 |
| Fonts | `GUI_SF_Fonts.gfxfonts` of `SFXGUI_Fonts.pcc`, both fonts in one SWF version 15 movie, LE2's two: cut to the pause screen's letters of all 8 languages they come out byte-identical to LE2's. The movie is out of the object list by the first HUD, unlike the GUI movies, so `Pause.cpp` has the game's loader bring it back there, at the main menu, with `DynamicLoadObject` on the file's `ObjectReferencer_448` (`PauseFonts.LoadPath`), 31 ms on the test machine; the next garbage collection drops it. `SFXGUI_FontsJPN.pcc`, which the Japanese game uses (`SFXGUIInteraction.FontMap`), gives the same letters, so one file serves every language. `$BioMass` and `$AeroLight` are "BioMass Shared" and "AeroLight Shared" here | `GUI_SF_Fonts.AeroLightFont_glyphs` and `.BioMassFont_glyphs` |
| Words | resume 157151, photo mode 817979, title ours: 170645 is not in the table, 174771 stays "Pause" in Russian, Polish and Japanese, and 687508 ("Pause Game") is a sentence in French | resume 157151, photo mode 392221, title ours |
| Gamepad buttons | `GUI_SF_MainWheel.Xbox_ControllerIcons_ABtn` and `_YBtn`, as the wheel shows them, in `Startup.pcc`, 256x256 DXT5, a ring 108 texels across, centred | `GUI_SF_Xbox_ControllerIcons.Xbox_ControllerIcons_IA` and `_I1` |
| Sound | `SFXGUIInteraction.PlayGuiSound` with `InGameGuiEnter` and `InGameGuiExit`, as `ShowBrowserWheel` and `HideBrowserWheel` | `BioSFManager.PlaySound` with the same names |
| `GetSimpleString` | `SFXGame` | `SFXGame` |
| `UTexture.Resource` | at +0x60 | at +0xA0 |

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
- S3, fullscreen movie. The relay jump (galaxy map, another cluster): Esc and Start pause picture and whoosh together
  under the pause screen, and Esc, Start, Space or A resumes them in sync; 5 cycles and a 15 s hold. Space and X with
  nothing paused skip nothing (the relay is not skippable). A loading screen and a movie inside a cutscene: Esc and Start
  do what they do in the vanilla game, and the world does not pause under the movie.
- S4, gameplay. Esc and Start open the pause menu and close it.
- S5, resume keys and the console. Pause with Esc and resume with Space, pause with Start and resume with A. With
  nothing paused, Space skips the line. Open the console during a pause: the pause screen hides, and Esc closes the
  console ("the console is open, vanilla" in the log).
- S6, photo mode under the pause, in a conversation with the reply wheel up and without, then in a cutscene. Y or F7
  opens it on the paused shot: its panel shows its tabs and options (no "TEST STRING"), the wheel is gone, the camera
  answers the mouse, the keys and both sticks. Y, F7, Esc or Start leaves it: the paused shot, the wheel and the pause
  screen come back.
- S7, the log. At start: "movie sound: the exe's AK::SoundEngine::ExecuteActionOnEvent at ...", "Bink:
  bink2w64_original.dll hooked (...), BinkPause true", "movie input hooked: ..."; "input hooked: ..." and "GUI tick
  hooked: ..."; "overlay: swap chain hooks Present true ..."; a line that ends in "LE3MelePause N ready" (N is
  `ASI_VERSION`). At the first frame, "first BioHUD.PostRender" with the same controller for the HUD owner and GEngine.
  Each press: "Esc pressed: mode 7 (Conversation)" and the like, with LE3's mode names.
- S8, the look. The title on a blue band in white, the labels in pale cyan, each key left of its label. With a gamepad,
  the game's A and Y at the height of their labels, not drawn discs. "overlay ready: ..., LE3 look", "overlay: the game's
  gamepad icons: A fine, Y fine: used", "the game's fonts: loaded their package in ... ms: SFXGUI_Fonts.ObjectReferencer_448",
  "overlay: the game's fonts: ... used", "overlay: the game's texts: title \"\" kept ours, resume ... used, photo mode ...
  used", and "GUI sound InGameGuiEnter: played" after "world paused".

## Open questions

1. *Answered 2026-10-30: yes.* Sync. Does the world pause stop LE3's conversations (`BioConversationController`) and
   keep voice, lips and music in sync?
2. *Answered 2026-10-30: yes.* The HUD. LE3's HUD draws through GFx. Does `BioHUD.PostRender` still pass through
   `ProcessEvent` every frame in conversations and cutscenes? The per-frame checks and the pause screen rest on it.
3. *Answered 2026-10-30.* Esc in a skippable cutscene. Escape, SpaceBar and X are LE3's skip keys there (Facts). Esc
   pauses, as in LE2, and the press that paused or resumed did not skip.
4. *Answered 2026-10-30: yes (v12, user report).* The console. Does the `Typing`/`Open` check through
   `Console.StateFrame` hold for LE3's `SFXConsole` (`SFXGame_classes.hpp:20022`)?
5. *Answered 2026-10-30: yes.* Photo mode in a cutscene. The first photo build opened it on a cutscene's shot (camera
   and keys answered, the panel did not load yet). v12 loads the panel there too: 4 sessions in an in-engine cutscene on
   Omega (mode 7, a `CameraActor`'s shot), the panel's GUI ticks as many as the frames.
6. Photo mode's GUI sounds under the pause (click, tabs, options): LE3's movies play them through `SFXGUIInteraction`,
   with no hook. Not checked on purpose yet.
7. Movies inside a cutscene or a conversation (the Matinee tracks, most of LE3's movies): which game modes and flags they
   play under, and what Esc does there in the vanilla game. Pausing them would need the world pause (the Matinee and its
   voice) with `BinkPause` (the picture).
8. The movie pause untested so far: a hold of 15 s or more, Start and A on a gamepad.
