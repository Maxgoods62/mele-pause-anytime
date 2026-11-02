# Pause Anytime in LE3

Status: in `RELEASED_GAMES` (`scripts/games.sh`), so built by default. Test results: [testing.md](../testing.md#le3).

## What differs from LE1

`./build.sh --game LE3` builds `LE3PauseAnytime.asi` from the same sources as LE1 and LE2, against LE3's SDK.
Everything the LE1 build has is on. What differs:

| Part | LE3 | LE1 |
|---|---|---|
| Movie pause | Only a Kismet movie whose sound Pause Anytime knows ([Movies](#movies)) | `MoviePolicy::Bink`: each movie's sound is in its `.bik` |
| The engine's movie input | Hooked; Space and X reach it as they are ([Movie keys](#movie-keys)) | Not hooked |
| Photo mode's GUI side (`PAUSEANYTIME_PHOTO_GFX`) | 1: GFx movies, `PhotoGuiGFx.cpp` ([Photo mode](#photo-mode)) | 0: ScaleForm panels, `PhotoGuiSF.cpp` |
| Photo mode's GUI sounds (`PAUSEANYTIME_PHOTO_GUI_SOUNDS`) | 0: no hook | 1: hooked |
| Pause screen | LE3's menu look ([Pause screen](#pause-screen)) | LE1's menu look |
| File | `LE3PauseAnytime.asi`, Windows version `N.0.6.0`, in `Game\ME3\Binaries\Win64\ASI` | `LE1PauseAnytime.asi`, `N.0.4.0` |

N is `ASI_VERSION` in `src/SharedVersion.h`.

## Movies

Of the 334 movies in `BioGame\Movies` and the DLC, only `StartupLoop_a` (the startup movie) carries a Bink audio track.
Every other movie's sound is a Wwise event, which `BinkPause` does not stop. One movie player (`GFullScreenMovie`) plays
every fullscreen movie. Once a movie starts, the player starts the movie's Wwise event itself. Three kinds of caller
ask it to play a movie:

| Caller | Live in the game files | Esc and Start |
|---|---|---|
| Kismet `BioSeqAct_MovieBink` (and `SFXSeqAct_PlatformMovieBink`) | 188 actions, 71 movies; 122 actions name an event | Paused when the sound is known, as in LE2 (below) |
| Matinee track `SFXInterpTrackMovieBink`, in an in-engine cutscene or a conversation | 442 tracks, 240 movies | Nothing latches them; what Esc does is not tested yet ([Open questions](#open-questions)) |
| Loading screens (`SFXLoadMovieManager`) | 45 movies | The game's: opened by file handle or from memory, nothing latches them |

- The Matinee tracks play the end of the Prologue, the Mars arrival, the summit videos and most of the ending.
- The loading screens play the Normandy and shuttle rides and the Citadel's transit.

A Kismet movie takes Esc and Start when Pause Anytime knows its sound (`MoviePolicy::BinkAndEvent`). That is the case
when the action that plays the movie names a Wwise event and runs nothing else beside the movie that could make a
sound (`MovieActionOf`, `src/game/KismetMovie.hpp`, shared with LE2). Pause Anytime pauses that event with
`ExecuteActionOnEvent` right after `BinkPause`.

- LE3 has no `BioSeqAct_StartConversation`. Its conversation op is `SFXSeqAct_StartAmbientConv`
  (`ConversationActionClass`).
- LE3's movie action can preload its file (input `Prime`, output `Primed`). Neither counts as starting the movie.
- 114 of the 122 actions with an event pass. They play 38 movies. Among them are:
  - the relay jump `GLO_01_Relay` (`Play_cineanim_massrelay`);
  - the first Citadel arrival;
  - `OmgIntro_001` to `010` on Omega;
  - the Leviathan arrivals;
  - the Hammer movies of End01;
  - the memorial flashbacks of the ending.
- The other 8 fail because a Matinee, music, a conversation, a sound loop or a nested sequence runs beside them:
  - the Mars arrival;
  - the two Normandy movies of the Citadel DLC;
  - `Device_Reveal`;
  - the Kro001 animatic;
  - the intro of Hackett's speech;
  - `Lev002_Asteroid_Depart`;
  - `Lev004_WW_ArrivalCrash4`.
- Only Genesis2Title has localized files (`MovieLanguages`).

### Movie keys

These come from a read-only dump of the running MassEffect3.exe. RVAs are the exe's.

- `SFXGameModeMovie` binds Escape and SpaceBar to `PC_MovieSkip`, and the pad's X to `Console_MovieSkip`. Start, A, B
  and Enter are unbound. Both binds run `SkipMovie`, a script. During a loading movie it does nothing. Otherwise, with
  the cutscene mode (8) on and its skip allowed, it skips the cutscene. LE3 has no native `SkipMovie`.
- The engine's movie input is at `0x24afc0`, slot 5 of the Bink movie player's `FViewportClient` vtable
  (`0x10DED78`), with LE2's first 28 bytes.
  - On a press of Escape, SpaceBar, XboxTypeS_Back, XboxTypeS_X or XboxTypeS_Start, it asks the player to stop the
    movie and returns TRUE. X and Start count only when the movie's flags allow them.
  - Enter, A and the left mouse button are not its keys.
- For a movie with flag 0x400 or 0x80, the movie input ignores every key, and the key goes on to `SkipMovie`.
  - The relay jump (played under the cutscene mode) and the Matinee tracks' movies have flag 0x400.
  - The movies opened by file handle have flag 0x80: the movie input is off for them.
- `[FullScreenMovie] SkippableMovies` (`BioEngine`) lists the movies that skip: `Device_Reveal`, `ProCit_100_Arrival`,
  and the Cat002 and Cat003 arrivals. The player checks it when the movie starts and keeps the result in its field at
  +0x84. `GLO_01_Relay` is not among them.

`src/MovieInput.cpp` hooks the movie input as in LE2 ([games/LE2.md](LE2.md#movie-keys)). These presses do not reach
the movie input and go on to `Console.InputKey` instead:

- Esc and Start while a movie owns the pause keys (`Bink::OwnsKeys`);
- any key while Pause Anytime's movie pause is on;
- a Space press that the movie's thread has just resumed on.

Space and X reach the movie input as they are (`MovieSkipKeyAs` is null), since they are its keys already. The movie
lines in the log add the player's flags and skippable flag (`Game::MoviePlayer`).

## Photo mode

LE3's photo panel is a GFx movie (`SFXSFHandler_PhotoMode`, `SFXGame_classes.hpp:15611`).
`SFXGUIInteraction.ShowPhotoMode(PC)` opens it (`EnableMode(Photo)` and `OpenMovie`), and `HidePhotoMode(PC)` closes
it.

The conversation's panel is a movie too, `BioSFHandler_Conversation`, from `GetConversationHandler(PC)`. Its visibility
and input state live in the native movie.

- `PhotoGuiGFx.cpp` saves both with `GetVisible` and `GetInputEnabled`, hides the movie with `SetVisible(FALSE)` and
  `SetInputEnabled(FALSE)`, and restores both at the exit.
- The handler's `SetVisible(FALSE)` clears `m_bDisplayingWheel` and `m_bDisplayInvestigateSubMenu`, so they are
  written back after the calls.
- With the reply wheel up, the game shows the movie again on its own while photo mode runs, often for many frames in a
  row. `PhotoGui::Hold` hides it again each frame, and the exit line counts those frames.

The natives were found through the classes' vtables in a memory dump of the running game (MassEffect3.exe of
2026-10-30):

| Native | RVA |
|---|---|
| `BioPlayerInput::InputKey`, slot 88 (LE2: 89) | `0xa5a040` |
| `UPlayerInput::InputAxis`, slot 89, not overridden | `0x54ac90` |
| The GFx movies' `Tick`, slot 18 of vtable `0x12c7e48` | `0x92d7a0` |

What each one does under the pause:

- `BioPlayerInput::InputKey` hands the key to `SFXGUIInteraction` first (`0xbc9be0`), then to `UPlayerInput::InputKey`
  (`0x54b710`) and `UInput::InputKey` (`0x54b2f0`). The last one drops presses while `WorldInfo.Pauser` (+0x670) is
  set. The hook works as in LE2: it clears the `Pauser` around the call.
- `UPlayerInput::InputAxis` skips the axis binds while the `Pauser` is set (`0x54ad9e`). It has LE2's first bytes, and
  the hook works as in LE2.
- The viewport client's tick (`0xc25695`) calls the GFx movies' `Tick`, which checks no `Pauser`. It advances each
  movie by the time since its last advance on the movie's clock. That clock is real time, or for most movies the
  world's `TimeSeconds` (`0x8c2570`, +0x74c), which stands still under the pause.
- Without `GfxClock`, each movie got `Advance(0)` under the pause. Photo mode's movie never played the frame that tells
  the game it loaded (`MovieLoaded`, then `InitializePhotoMode`), so its labels stayed "TEST STRING". The conversation
  movie's hide was never drawn either.

While photo mode is on, `GfxClock` (`Photo.cpp`) wraps each call of the GFx movies' `Tick`:

- Before the call, it moves `TimeSeconds` forward by the real time since the last call.
- After the call, it puts back `TimeSeconds`. Each movie stamp that the call wrote with the moved time goes back to the
  world's time too (`Game::GfxMovieLists`, the stamp at +0x44 of each entry). The next call measures from the world's
  clock again.

Nothing else sees the clock move. `SFXGUIInteraction`'s own tick (slot 87, `0xbcbf70`) skips only its queued message
boxes under the pause (`IsPaused`, `0x593f40`).

## Pause screen

`src/game/LE3.hpp` holds the names and ids, and `Le3Look` in `src/Overlay.cpp` holds the colours and sizes. All of them
were read from the game's files, and none of those files is shipped.

| What | LE3 | LE2 |
|---|---|---|
| Title colour | `#FFFFFF`, `MainWheel`'s `guiTitleText` | `#FFCC66` |
| Label colour | `#ACF5FE`, the wheel's buttons | `#FF9142` |
| Rules | `#0896F7`, the edge of `PC_SharedAssets_I46` | `#FF9C5B` |
| Band | `#08759C` to `#084E7E`, lighter at the top (the bars `MainWheel_I1E` and `_I9`) | `#5F2C0C` to `#843E14` |
| Blur | Toward blue | Toward warm |
| Title | $BioMass 63 at 1080 lines, spacing 1.5 | $BioMass 49.5, spacing 1.5 |
| Labels and keys | As in LE2, in the colour of the wheel's buttons | $BioMass 22.8; the keys in the light font at 18 |
| Fonts | `GUI_SF_Fonts.gfxfonts` of `SFXGUI_Fonts.pcc` | `GUI_SF_Fonts.AeroLightFont_glyphs` and `.BioMassFont_glyphs` |
| Words | Resume 157151, photo mode 817979; the title is Pause Anytime's own | Resume 157151, photo mode 392221; the title is Pause Anytime's own |
| Gamepad buttons | `GUI_SF_MainWheel.Xbox_ControllerIcons_ABtn` and `_YBtn` | `GUI_SF_Xbox_ControllerIcons.Xbox_ControllerIcons_IA` and `_I1` |
| Sound | `SFXGUIInteraction.PlayGuiSound` with `InGameGuiEnter` and `InGameGuiExit` | `BioSFManager.PlaySound` with the same names |
| `GetSimpleString` | `SFXGame` | `SFXGame` |
| `UTexture.Resource` | At +0x60 | At +0xA0 |

- As in LE2, the title is 1.5 times the size of the wheel's `guiTitleText`, which is 42 with a spacing of 1.
- The wheel's own prompts differ from the pause screen's labels: $AeroLight 30, `#FFBA65` (`ControlTextLeftAlign` in
  `PC_SharedAssets`).
- The font library holds both fonts in one SWF version 15 movie, the same two fonts as LE2. Cut down to the letters
  the pause screen uses in all 8 languages, they are byte-identical to LE2's. `$BioMass` and `$AeroLight` are "BioMass
  Shared" and "AeroLight Shared" here.
- The font movie is out of the object list by the first HUD, unlike the GUI movies. So `Pause.cpp` has the game's
  loader bring it back at the main menu, with `DynamicLoadObject` on the file's `ObjectReferencer_448`
  (`PauseFonts.LoadPath`). That took 31 ms on the test machine. The next garbage collection drops it again.
- `SFXGUI_FontsJPN.pcc`, which the Japanese game uses (`SFXGUIInteraction.FontMap`), gives the same letters. So one
  file serves every language.
- 170645 is not in the table. "Pause" (174771) stays English in Russian, Polish and Japanese, and "Pause Game" (687508)
  is a sentence in French. So the title stays Pause Anytime's own.
- The gamepad buttons are the ones the wheel shows, in `Startup.pcc`: 256x256 DXT5, with a ring 108 texels across,
  centred.
- The sounds are the ones `ShowBrowserWheel` and `HideBrowserWheel` play. They are Wwise events that also hold and give
  back the game's audio ([World pause](../how-it-works.md#world-pause)).

## Facts

`src/game/LE3.hpp` holds these. The file and line are in the LE3 SDK of the LExASIs commit that `build.sh` pins
(`External/LExSDKv2/Src/LESDK/LE3/`). The binds are the defaults of `Default__BioPlayerInput` in the game's
`SFXGame.pcc`.

The game modes are `SFXGameModeManager.EGameModes` (`SFXGame_classes.hpp:3210-3239`):

| Number | Mode |
|---|---|
| 0 | Default |
| 1 | Vehicle |
| 2 | Atlas |
| 3 | PowerWheel |
| 4 | WeaponWheel |
| 5 | Command |
| 6 | InjuredShepard |
| 7 | Conversation |
| 8 | Cinematic |
| 9 | GUI |
| 10 | Movie |
| 11 | Galaxy |
| 12 | Orbital |
| 13 | MultiLand |
| 14 | Photo |
| 15 | CheatMenu |
| 16 to 24 | Nine more, up to 24 Lobby |

LE1 has 5 Conversation, 6 Cinematic, 8 Movie and 11 Photo.

| What | LE3 | LE1 | Where (LE3) |
|---|---|---|---|
| The controller's game modes | `BioPlayerController.GameModeManager2` | `GameModeManager` | `SFXGame_classes.hpp:7610` |
| `GuiKey` events of the always-on Start and Escape binds | Start 24 (released 45), Escape 16 (released 37) | Start 21 (released 43), Escape 24 (released 46) | `SFXGame_classes.hpp:1126`; binds `:17765-17766` |
| What Escape sends there | `BUTTON_B`, as in LE2 | `KEY_ESCAPE` | `SFXGame_classes.hpp:1126` |
| Photo mode's Esc and Start bind | `SFXGameModePhoto.ShowMenu` | `ShowMenuGui` | `SFXGame_classes.hpp:31027` |
| Cutscene skip binds (`SFXGameModeCinematic`) | Escape and SpaceBar: `PC_CineSkip`; XboxTypeS_X: `Console_CineSkip` | Esc | `Default__SFXGameModeCinematic` in `SFXGame.pcc` |

In LE3, `BioGuiEvents` is in SFXGame. The always-on binds are `BioPlayerInput.StaticPCBinds` and
`StaticConsoleBinds`. The rest of the shared code's names (`WorldInfo.Pauser`, `BioHUD`, `SFXConsole`,
`SFXPlayerCamera`, the Console's `InputKey`) are in LE3's SDK under the same names.

Seen in the game (2026-10-30 unless stated):

- The world pause stops LE3's conversations (`BioConversationController`) and keeps voice, lips and music in sync.
- LE3's HUD draws through GFx, and `BioHUD.PostRender` still passes through `ProcessEvent` every frame in
  conversations and cutscenes. The per-frame checks and the pause screen rest on it. Over a fullscreen movie it stops:
  the relay jump was fullscreen 0.2 s after its open.
- In a skippable cutscene, Escape, SpaceBar and X are the skip keys. Esc pauses there, as in LE2, and the press that
  paused or resumed did not skip.
- The console check through `Console.StateFrame` (`Typing` or `Open`) holds for LE3's `SFXConsole`
  (`SFXGame_classes.hpp:20022`): v12, played by hand.
- Photo mode opens on a cutscene's shot, and since v12 its panel loads there too. In four photo sessions on Omega, the
  panel had one GUI tick per frame. They ran in an in-engine cutscene on a `CameraActor`'s shot, in game mode 7
  (Conversation).
- With the Steam overlay open over a pause, Esc's resume logged "still paused by ...": another pause held the world
  (S9, 2026-11-02).

## Open questions

1. Photo mode's GUI sounds under the pause (clicks, tabs, options). LE3's movies play them through
   `SFXGUIInteraction`, with no hook. No test has looked at them yet.
2. Matinee-track movies, which are most of LE3's movies and play inside a cutscene or a conversation. Their flag is
   0x400 ([Movie keys](#movie-keys)). Not checked in the game yet: which game mode is current while they play, what
   Esc does there in the game without mods, and so whether Esc pauses the world under them. Pausing them would need
   the world pause (for the Matinee and its voice) together with `BinkPause` (for the picture).
