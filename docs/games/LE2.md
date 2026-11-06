# Pause Anytime in LE2

This page is for modders. It covers `LE2PauseAnytime.asi`, the build for Mass Effect 2 Legendary Edition. The game's
names, ids and addresses are in [src/game/LE2.hpp](../../src/game/LE2.hpp); every other source file serves all three
games. The shared design is in [how-it-works.md](../how-it-works.md), and test results are in
[testing.md](../testing.md#le2).

## Keys

The world pause starts in game mode 5 (Conversation) or 6 (Cinematic), when nothing else holds the world paused
(`WorldInfo.Pauser` is None).

| Where | Esc or Start | Space or A | Y or F7 |
|---|---|---|---|
| Conversation or cutscene, not paused | Pauses the world | The game's | The game's |
| Under the world pause | Resumes | Resumes | Opens photo mode |
| Photo mode opened from the pause | Leaves it, still paused | The game's (Space takes the picture) | Leaves it, still paused |
| Movie Pause Anytime can pause | Toggles the movie pause | Resumes a paused movie | The game's |
| Any other movie | The game's | Space skips a skippable movie | The game's |
| Gameplay, menus, galaxy map, open console | The game's | The game's | The game's |

- In Cinematic mode, Esc, Space and the pad's X run `SkipCinematic`, so Esc skips a skippable cutscene in the game
  without mods. Pause Anytime takes that Esc to pause. Space and X still skip the cutscene when nothing is paused.
- The [skip guard](../how-it-works.md#skip-guard) drops the `GuiKey` events of the always-on binds: Escape sends
  `BUTTON_B`, 13 (released 34), and Start sends `BUTTON_START`, 21 (released 42). Since `GuiKey` is matched by number,
  a pad B press in the guard window is dropped too.
- While Pause Anytime's photo session runs, photo mode's menu bind, `SFXGameModePhoto.ShowMenu`, is always dropped.
- The Steam overlay sets `WorldInfo.Pauser`. If it opens over the world pause, the next frame posts `InGameGuiExit`
  while the world is still held, so the voice line survives
  ([why](../how-it-works.md#the-audio-hold-and-the-steam-overlay)). Closing the overlay ends the pause.

## Movies

Most LE2 movies have no Bink audio: 164 of the 166 base-game movies and all 45 DLC movies. Their sound is a Wwise event
that a Kismet `BioSeqAct_MovieBink` posts beside the picture, and `BinkPause` does not stop it. Of the two movies with
Bink audio, `load_f01_a` and `visionavi`, only `visionavi` is on the Bink-audio list. The code gives no reason.

A fullscreen movie takes the pause keys only when Pause Anytime knows its sound (`MoviePolicy::BinkAndEvent`):

1. The Kismet movie action latched last plays this movie, posts a Wwise event, and runs nothing beside it that may
   make a sound (`MovieActionOf` in `src/game/KismetMovie.hpp`). That event is paused with `ExecuteActionOnEvent`
   right after `BinkPause`.
2. Or the movie's base name is on the Bink-audio list. The latch test runs first, so a listed movie whose action runs
   something beside it has no known sound.

Any other fullscreen movie keeps the game's Esc and Start, and Esc skips it when the game lets you. Loading-screen
movies play from memory, so they never pause.

- A file matches the movie name of the action, or that name plus `_` and a language suffix: `int`, `fra`, `fre`,
  `deu`, `dee`, `esn`, `ita`, `ite`, `pol`, `poe`, `rus`, `jpn`.
- The conversation op for the beside test is `BioSeqAct_StartConversation`.
- Wwise events whose names start with `Mute_`, `UnMute`, `Stop_` or `VO_Kill_` do not count as a sound. Illium's
  arrival posts `UnMute_All_World_Sounds` on `Out` and `Mute_All_World_Sounds` on `HasStarted`.

Of LE2's 117 movie actions with a Wwise event, 71 pass `MovieActionOf`: the relay jump and the hubs' arrivals and
departures. The others fail because voice lines, conversations, music cues, Matinees or `Play_` events run beside them.
They are in the Prologue (`BioD_ProNor` `ProNorEscape`, `Opening_End_Seq`, `ProNor_Pod2`), in the suicide mission
(`EndGm1`), and in Tali's and Grunt's arrivals. `visionavi`'s Matinee posts `Play_cineanim_N7Spdr1_flashback_exit` with
it.

### Movie keys

The engine's movie input is the Bink player's native `InputKey`, slot 5 of its `FViewportClient` vtable, at RVA
`0x2334d0`. It sees a press before `Console.InputKey`.

- Its keys are Escape, Enter, LeftMouseButton, XboxTypeS_Back, XboxTypeS_A and XboxTypeS_Start (A and Start unless the
  movie's flags turn them off). On a press it asks the player to stop the movie and reports the key as handled.
- The movie stops only if `[FullScreenMovie] SkippableMovies` lists it. That list has 73 movies, and `GLO_01_Relay` is
  not one of them, so the relay jump takes the key and plays on.
- `SFXGameModeMovie` binds Escape and SpaceBar to `PC_MovieSkip`, and the pad's X to `Console_MovieSkip`. Both run
  `SkipMovie`, which calls the native `SFXEngine.SkipMovie` (`0xB3DCE0`), and that calls an empty slot. So without the
  mod, Space and X skip nothing in a movie.

`src/MovieInput.cpp` hooks it after checking its first 28 bytes. On a press:

| Press | The hook |
|---|---|
| Esc or Start, the movie owns the keys | Keeps it from the movie input; `Console.InputKey` gets it |
| Any key under the movie pause | Keeps it: no key skips a paused movie |
| Space the movie's thread just resumed on | Keeps it |
| Space or X, a movie open, nothing paused | Passes it as Enter: it skips what Esc skips |
| Esc or Start on any other movie | Passes it unchanged |

- Esc toggles the pause on the movie's thread. Start toggles it on its press, on the game thread. Space and A
  resume.
- The movie's thread reads Space a frame before the game thread (4 ms in `CitHub_Arrival`). The third row is for
  that case.
- The Enter row tests only that a movie is open, so it also applies to a loading screen.
- Escape is always one of the movie input's keys. On a movie that Pause Anytime cannot pause, Esc's press does not
  reach `Console.InputKey`, and in tests the world never paused under a movie.

## Photo mode

Y or F7 under the world pause opens the game's photo mode on the paused shot, for the controller that paused.

- The photo panel is a Scaleform panel of `MassEffectGuiManager` (`PhotoGuiSF.cpp`).
- The conversation panel is `MassEffectGuiManager.m_oConversationHandler.oPanel`. It is set aside with
  `bInputDisabled` 1, `bHandleInputThisFrame` 1 and `IsVisible` 0, and given back as found.
- `SFXGameModePhoto.Activated` keeps the subtitle line.
- Photo mode's GUI sounds are not hooked. They are Wwise events, which play under the world pause as they are.
- `PickCameraMode` runs only once `TimeSeconds - LastTransitionTime` is over `TransitionTimeout` (0.25 s), and
  `TimeSeconds` stands still under the pause. So `LastTransitionTime` is set to `TimeSeconds - TransitionTimeout - 1`.
- The tether is widened on foot and in a vehicle (`VehiclePhotoModeTetherDistance`).

The natives come from a memory dump of the running game. Each hook first compares the bytes listed in `LE2.hpp`.

| Native | RVA | Bytes checked |
|---|---|---|
| `BioPlayerInput::InputKey` (vtable slot 89) | `0xa62e00` | 24 |
| `UPlayerInput::InputAxis` (slot 90, not overridden) | `0x541790` | 16 |
| `UBioSFManager::Tick` | `0x1a07c0` | 16 |

`UInput::InputKey` (`0x541e00`) drops keys while the `Pauser` (+0x684) is set, and `InputAxis` checks it at
`0x54189f`. The GUI manager's own Tick (`0xb27820`) does its work only while the world runs, then jumps to
`UBioSFManager::Tick`.

## Pause screen

The look is `OverlayLook::LE2`, the `Le2Look` table in `src/Overlay.cpp`, taken from the pause wheel `PC_MainWheel`.
Sizes are at 1080 lines, and the title is 1.5 times the wheel's, with its spacing.

| Part | LE2 |
|---|---|
| Title | `#FFCC66`, bold, 49.5, spacing 1.5 |
| Labels | `#FF9142`, bold, 22.8, spacing 0.9 |
| Keys | Light, 18, spacing 0.75 |
| Band | `#5F2C0C` to `#843E14`, rules `#FF9C5B` |
| Blur tint | 1.06, 0.97, 0.88 (warm) |
| Light font | `Myriad Pro+DFGGothicP-W5` |
| Bold font | `Biomass+DFGSoGei-W7` |
| Words | Resume 157151, photo mode 392221 |
| String table | `ASFXGame.GetSimpleString` |
| Gamepad A, Y | `GUI_SF_Xbox_ControllerIcons.Xbox_ControllerIcons_IA`, `_I1` |
| Button art | 128 texels, disc at (61, 55.5), 93 across |
| Texture view | `{ 0x1086278, 0x24, 0x1229C50, 0xC, 0x10 }` |

- The light font is in `GUI_SF_Fonts.AeroLightFont_glyphs` and the bold one in `GUI_SF_Fonts.BioMassFont_glyphs`, in
  `Startup_<lang>.pcc`. Both are the same in all eight languages. The movies `AeroLightFont` and `BioMassFont` hold no
  outlines.
- The title is Pause Anytime's own word. String 170645 is not in LE2's table, and "Pause" (174771) stays English in
  Russian, Polish and Japanese.
- The sounds `InGameGuiEnter` and `InGameGuiExit` are the ones `ShowBrowserWheel` and `HideBrowserWheel` play,
  through `BioSFManager.PlaySound`. The first starts with a Wwise Pause All and the second with a Resume All. So every
  end of the pause posts `InGameGuiExit` once.

## Engine facts

- The modes are `SFXGameModeManager.EGameModes`, read from `BioPlayerController.GameModeManager2`: Conversation 5,
  Cinematic 6, Movie 8, Photo 11. `ModeNames` in `LE2.hpp` holds all 13, and the log prints each mode's name.
- The pinned LExASIs SDK gives `ProcessEvent` at RVA `0xd8d50` and `GEngine` at `0x175ef28`. Neither is checked
  against the exe's bytes.
- `AK::SoundEngine::ExecuteActionOnEvent` and `GetIDFromString` come from the exe's export table, by mangled name.

Seen in the game:

- The pauser is the player's `SFXPRI`. Keys reach `SFXGame.SFXConsole.InputKey`, and with the console open Esc
  reaches `SFXConsole.Open.InputKey`.
- `BioHUD.PostRender` runs every frame in conversations and cutscenes, and stops over a fullscreen movie.

## Open questions

1. Matinee-track movies: the Kasumi DLC (`Montage_1.bik` and others) and `DLC_METR_Patch01` play movies from
   `SFXInterpTrackMovieBink`, which nothing latches. The mode while they play is not known. If it stays Cinematic and
   Esc's press gets through, Esc pauses the world while the picture plays on.
2. `visionavi` is on the Bink-audio list, but its Matinee posts an event beside it. It may never pause when its Kismet
   plays it.
3. X under the movie pause: the hook keeps it from the movie input, but outside the guard window its `SkipMovie` bind
   passes and ends the movie pause. The test says only that X and Enter skip nothing there.
4. Whether the movie's thread presents the game's frames in LE2 is not recorded. If not, the pause screen shows only
   as the movie pauses.
