# Pause Anytime in LE2

Status: in `RELEASED_GAMES` (`scripts/games.sh`), so built by default. Test results: [testing.md](../testing.md#le2).

## What differs from LE1

`./build.sh --game LE2` builds `LE2PauseAnytime.asi` from the same sources as LE1, against LE2's SDK. Everything the
LE1 build has is on. What differs:

| Part | LE2 | LE1 |
|---|---|---|
| Movie pause | Only a movie whose sound Pause Anytime knows ([Movies](#movies)) | `MoviePolicy::Bink`: each movie's sound is in its `.bik` |
| The engine's movie input | Hooked; Space and X reach it as Enter ([Movie keys](#movie-keys)) | Not hooked |
| Photo mode's GUI sounds (`PAUSEANYTIME_PHOTO_GUI_SOUNDS`) | 0: no hook ([Photo mode](#photo-mode)) | 1: hooked |
| Pause screen | LE2's menu look ([Pause screen](#pause-screen)) | LE1's menu look |
| File | `LE2PauseAnytime.asi`, Windows version `N.0.5.0`, in `Game\ME2\Binaries\Win64\ASI` | `LE1PauseAnytime.asi`, `N.0.4.0` |

N is `ASI_VERSION` in `src/SharedVersion.h`.

## Movies

- 164 of the 166 movies in `BioGame\Movies` have no Bink audio track, and none of the DLC's 45 has one. Their sound is
  a Wwise event, which `BinkPause` does not stop: the picture would hold while the sound plays on. Only
  `load_f01_a.bik` and `visionavi.bik` carry their own sound.
- So a movie takes Esc and Start only when Pause Anytime knows its sound (`MoviePolicy::BinkAndEvent` in
  `src/game/LE2.hpp`). That sound is one of two things:
  - The Wwise event of the Kismet `BioSeqAct_MovieBink` that plays the movie. The exe's
    `AK::SoundEngine::ExecuteActionOnEvent` pauses that event right after `BinkPause`.
  - Bink audio, for `visionavi`, the one movie listed in `BinkAudioMovies`.
- Any other movie keeps the game's Esc and Start: Esc skips a movie that the game lets you skip.
- Pause Anytime knows a movie's sound when the Kismet action that plays it posts an event and starts nothing else
  beside the movie that could make a sound (`MovieActionOf` in `src/game/KismetMovie.hpp`, shared with LE3). It checks:
  - the action's `Out` and `HasStarted` outputs;
  - the ops started at the same moment;
  - the Matinee or conversation line whose event starts the action.
- Of the 117 movie actions with a Wwise event, 71 pass. Among them are the relay jump and the movies of the Citadel,
  Omega, Tuchanka and Illium hubs.
- The others fail because voice lines, conversations, music cues, Matinees or `Play_` events run beside them. They
  include movies of the Prologue, the suicide mission (`EndGm1`), and Tali's and Grunt's arrivals.
- The engine's movie input would skip a skippable movie on a press of Esc or Start, before Pause Anytime sees the
  press. See [Movie keys](#movie-keys).

### Movie keys

These come from a read-only dump of the running MassEffect2.exe, whose `.text` is encrypted on disk. RVAs are the
exe's.

- `SFXGameModeMovie` binds Escape and SpaceBar to `PC_MovieSkip`, and the pad's X to `Console_MovieSkip`. Both run
  `SkipMovie`, which calls the static native `SFXEngine.SkipMovie`.
- `SFXEngine.SkipMovie` (`0xB3DCE0`) calls slot 27 of the movie player (`GFullScreenMovie`, `0x1B652B0`), an empty
  function. In LE2, `SkipMovie` does nothing.
- The engine's movie input is at `0x2334d0`, slot 5 of the Bink movie player's `FViewportClient` vtable
  (`0x101D3F8`). Every key event reaches it before `Console.InputKey`.
  - On a press (`IE_Pressed`) of Escape, Enter, LeftMouseButton, XboxTypeS_Back, XboxTypeS_A or XboxTypeS_Start, it
    asks the player to stop the movie (slot 41 of its main vtable) and returns TRUE. `Console.InputKey` never sees that
    press. A and Start count only when the movie's flags allow them.
  - On any other key it returns FALSE, and the key goes on.
- `[FullScreenMovie] SkippableMovies` in Coalesced lists the 73 movies that skip. `GLO_01_Relay` is not among them.
  The movie input still takes the skip keys during the relay jump: the log shows that it takes Space, which the hook
  passes on as Enter. The relay jump plays on, so the list is checked somewhere after the movie input. Where that check
  happens has not been traced.

`src/MovieInput.cpp` hooks the movie input after checking its first 28 bytes (`Game::MovieInputRva`,
`MovieInputStart`). On a press:

- Esc or Start while a movie owns the pause keys (`Bink::OwnsKeys`): the hook does not call the original and returns
  FALSE. The press goes on to `Console.InputKey`. The movie's thread toggles the pause on Esc, and `Pause.cpp` on
  Start. Before the hook, only Start's repeats and release reached `Console.InputKey`.
- Any key while Pause Anytime's movie pause is on: the same, so no key skips a paused movie.
- Space just after the movie's thread resumed on it: the same, so that resume never skips the movie. The movie's
  thread reads the key at once and the game thread a frame later (4 ms apart in `CitHub_Arrival`).
- Space or X while a movie plays: the hook calls the original with Enter in their place. They skip what Esc skipped,
  and only that.
- Anything else: the original, unchanged.

## Photo mode

`src/Photo.cpp` serves every game, and `src/game/LE2.hpp` holds what differs. LE2's photo panel is a ScaleForm panel
of the GUI manager, as in LE1 (`PhotoGuiSF.cpp`).

The natives come from a read-only dump of the running MassEffect2.exe. Each was found through its class's vtable. The
string with the class name leads to the class's `StaticClass`. The `StaticClass` leads to the constructor, and the
constructor writes the vtable. LE2's `BioPlayerInput` slots sit one below LE1's. The hooks check each function's first
bytes (`Game::Photo*Start`) before they patch it.

| What | LE2 | LE1 |
|---|---|---|
| `BioPlayerInput::InputKey` | `0xa62e00`, slot 89 of the vtable at `0x1261688` | `0xc6de70`, slot 90 |
| `UPlayerInput::InputAxis` | `0x541790`, slot 90, not overridden | `0x68bc40` |
| `UBioSFManager::Tick` | `0x1a07c0` | `0x193bc0` |
| GUI sounds | No hook | `PlayGuiSound` and `UAudioDevice::CreateComponent`, the component marked `bNonRealtime` |
| The conversation panel | `MassEffectGuiManager.m_oConversationHandler.oPanel` | `m_oConversationPanel` |
| The camera switch gate | `TimeSeconds`, 0.25 s, in `SFXPlayerCamera.UpdateViewTarget` | `GameTimeSeconds`, 0.1 s, in `PickCameraMode` |
| The subtitle at `SFXGameModePhoto.Activated` | Kept | Cleared |

- `BioPlayerInput::InputKey` offers the key to the GUI, then calls `UPlayerInput::InputKey` (`0x542220`) and
  `UInput::InputKey` (`0x541e00`). The last one drops the key while `WorldInfo.Pauser` is set.
- `UPlayerInput::InputAxis` checks the `Pauser` at `0x54189f`.
- `MassEffectGuiManager`'s override of the tick (`0xb27820`) does its own part only while the world runs, then jumps
  to `UBioSFManager::Tick`.
- The GUI sounds are Wwise events, which play under the world pause as they are.
- `SFXPlayerCamera.UpdateViewTarget` calls `PickCameraMode` only when
  `TimeSeconds - LastTransitionTime > TransitionTimeout`.

Seen in the game: when photo mode opens from a director's shot (a `CameraActor` view target), the camera mode left
behind is the gameplay camera, `SFXCameraMode_Explore`. The switch to the photo camera is an instant transition. Its
start turns the controller to the old camera's view, 24 degrees off the shot in one log. The entry then puts the
controller back on the shot ([how-it-works.md](../how-it-works.md#photo-mode-entering), step 7).

## Pause screen

`src/game/LE2.hpp` holds the names and ids, and `Le2Look` in `src/Overlay.cpp` holds the colours and sizes. All of them
were read from the game's files, and none of those files is shipped.

| What | LE2 | LE1 |
|---|---|---|
| Title colour | `#FFCC66`, `PC_MainWheel`'s title | `#CCFFFF` |
| Label colour | `#FF9142`, `PC_MainWheel`'s B and Y fields | `#3199C6` |
| Rules | `#FF9C5B` | `#3199C6` |
| Band | `#5F2C0C` to `#843E14` | `#174868` to `#226E8F` |
| Blur | Toward warm | Toward blue |
| Title | $BioMass 49.5 at 1080 lines, spacing 1.5 | $AeroLight 67.5, as `WindowPop` sets it |
| Fonts | `GUI_SF_Fonts.AeroLightFont_glyphs` and `.BioMassFont_glyphs` | `GUI_SF_Fonts.Fonts`, both in one movie |
| Words | Resume 157151, photo mode 392221; the title is Pause Anytime's own | Resume 157151, photo mode 184397; the title is Pause Anytime's own |
| Gamepad buttons | `GUI_SF_Xbox_ControllerIcons.Xbox_ControllerIcons_IA` and `_I1` | `GUI_SF_SharedAssets.XBox_ControllerIcons_I4` and `_I9` |
| Sound | `BioSFManager.PlaySound` with `InGameGuiEnter` and `InGameGuiExit` | `PlayGuiSound` with the same names, through the GUI sound hooks |
| `GetSimpleString` | `SFXGame`, which `BioSPGame` extends | `BioSPGame` |

- The band's colours were sampled from the game's wheel on screen. The fill colours of the wheel in the movie file are
  similar.
- The title is 1.5 times the size of `PC_MainWheel`'s `guiTitleText`, with its spacing. `guiTitleText` is 33 at 1080
  lines: 22 with a spacing of 1 on the movie's 720-line stage.
- The fonts are Myriad Pro+DFGGothicP-W5 (the light one) and Biomass+DFGSoGei-W7 (the bold one), byte-identical in all
  8 languages. The movies `AeroLightFont` and `BioMassFont` beside them hold no outlines.
- 170645, LE1's title, is not in LE2's table (the lookup gives "No Data"), and `WindowPop`'s paused box is unused in
  LE2. "Pause" (174771) stays English in Russian, Polish and Japanese. So the title stays Pause Anytime's own.
- The gamepad textures are 128x128. The disc is 93 texels across, centred at (61, 55.5). It is off-centre, so
  `ButtonArt` places it. LE1's disc is 95 across, and centred.
- The sounds are the ones `MassEffectGuiManager.ShowBrowserWheel` and `HideBrowserWheel` play. They are Wwise events
  that also hold and give back the game's audio ([World pause](../how-it-works.md#world-pause)).

## Facts

`src/game/LE2.hpp` holds these. The file and line are in the LE2 SDK of the LExASIs commit that `build.sh` pins
(`External/LExSDKv2/Src/LESDK/LE2/`). The binds are from the game's `BioGame\CookedPCConsole\Coalesced_INT.bin`.

The game modes are `SFXGameModeManager.EGameModes` (`SFXGame_classes.hpp:4054-4071`):

| Number | Mode |
|---|---|
| 0 | Default |
| 1 | PowerWheel |
| 2 | WeaponWheel |
| 3 | Command |
| 4 | Vehicle |
| 5 | Conversation |
| 6 | Cinematic |
| 7 | GUI |
| 8 | Movie |
| 9 | Galaxy |
| 10 | Orbital |
| 11 | Photo |
| 12 | CheatMenu |

Conversation, Cinematic, Movie and Photo have LE1's numbers. In LE1, Vehicle is 1, and 10 and 12 are Minigame and
FlyCam.

| What | LE2 | LE1 | Where (LE2) |
|---|---|---|---|
| The controller's game modes | `BioPlayerController.GameModeManager2` | `GameModeManager` | `SFXGame_classes.hpp:5535` |
| `GuiKey` events of the always-on Start and Escape binds | Start 21 (released 42), Escape 13 (released 34) | Start 21 (released 43), Escape 24 (released 46) | `Engine_classes.hpp:1085, 1093, 1106, 1114`; binds `SFXGame_classes.hpp:13169-13170` |
| What Escape sends there | `BUTTON_B` | `KEY_ESCAPE` | `Engine_classes.hpp:1085, 1106` |
| Photo mode's Esc and Start bind (`PC_Menu`, `Console_Menu`) | `SFXGameModePhoto.ShowMenu` | `ShowMenuGui` | `SFXGame_classes.hpp:27326` |

The always-on binds are `BioPlayerInput.StaticPCBinds` and `StaticConsoleBinds`. Where LE2's binds differ from what the
shared code expects:

- In Cinematic mode, Esc, Space and a gamepad's X run `SkipCinematic`. In the game without mods, Esc skips a skippable
  in-engine cutscene. Pause Anytime takes that Esc to pause, and its skip guard drops the skip while the key is down and
  for 250 ms after its release.
- The always-on binds send Escape as `GuiKey BIOGUI_EVENT_BUTTON_B`, where LE1's send `KEY_ESCAPE`. Start sends
  `BUTTON_START` in both games. The skip guard drops those four events while a press that Pause Anytime took is down.

Seen in the game:

- The world pause holds the picture, voice, lips and music together, and they resume in sync. The pauser is the
  player's `SFXPRI`.
- Keys reach `SFXGame.SFXConsole.InputKey`. With the console open, Esc comes to `SFXConsole.Open.InputKey` and closes
  the console, as in LE1. The per-frame console check (`Console.StateFrame`, `Typing` or `Open`) holds.
- `BioHUD.PostRender` runs every frame in conversations and in-engine cutscenes, so the pause screen shows in both.
- `BioHUD.PostRender` stops over a fullscreen movie, so the LE1 test for a fullscreen movie, a silent HUD, works in
  LE2 too. The relay and Citadel movies were fullscreen 0.2 s after their open (2026-10-30).
- The Steam overlay sets `WorldInfo.Pauser`, as in LE1. Without Pause Anytime it stops the picture, voice, lips and
  music, and all of them resume together about 2 s after the overlay closes (L0, [testing.md](../testing.md#le2)).

## Open questions

- Matinee-track movies. The Kasumi DLC (`DLC_HEN_MT`: `Montage_1.bik` and others) and `DLC_METR_Patch01` play movies
  from a Matinee track, `SFXInterpTrackMovieBink` (`SFXGame_classes.hpp:15489`). Kismet's `BioSeqAct_MovieBink` turns
  Movie mode on, so Esc stays the game's there. Whether this track does the same is unknown. If it does not, the mode
  stays Cinematic. Then only the engine's movie input, which gets Esc's press first, stops Esc from pausing the world
  while the movie plays on.
