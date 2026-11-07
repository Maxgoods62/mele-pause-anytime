# Pause Anytime in LE3

This page is for modders. It covers `LE3PauseAnytime.asi`, the build for Mass Effect 3 Legendary Edition. The game's
names, ids and addresses are in [src/game/LE3.hpp](../../src/game/LE3.hpp); every other source file serves all three
games. The shared design is in [how-it-works.md](../how-it-works.md), and test results are in
[testing.md](../testing.md#le3).

## Keys

The world pause starts in game mode 7 (Conversation) or 8 (Cinematic), when nothing else holds the world paused
(`WorldInfo.Pauser` is None).

| Where | Esc or Start | Space or A | Y or F7 |
|---|---|---|---|
| Conversation or cutscene, not paused | Pauses the world | The game's | The game's |
| Under the world pause | Resumes | Resumes | Opens photo mode |
| Photo mode opened from the pause | Leaves it, still paused | The game's (Space takes the picture) | Leaves it, still paused |
| Kismet movie with a known sound | Toggles the movie pause | Resumes a paused movie | The game's |
| Matinee-track movie, loading screen | The game mode decides | The game's | The game's |
| Gameplay, menus, galaxy map, open console | The game's | The game's | The game's |

- `SFXGameModeCinematic` binds Escape and SpaceBar to `PC_CineSkip`, and XboxTypeS_X to `Console_CineSkip`. Pause
  Anytime takes the Esc that would skip a cutscene. Space and X still skip it when nothing is paused.
- The [skip guard](../how-it-works.md#skip-guard) drops the `GuiKey` events of the always-on binds: Escape sends
  `BUTTON_B`, 16 (released 37), and Start sends `BUTTON_START`, 24 (released 45). Since `GuiKey` is matched by number,
  a pad B press in the guard window is dropped too.
- While Pause Anytime's photo session runs, photo mode's menu bind, `SFXGameModePhoto.ShowMenu`, is always dropped.
- If the Steam overlay opens over the world pause, the next frame posts `InGameGuiExit` while the world is still held,
  so the voice line survives
  ([why](../how-it-works.md#the-audio-hold-and-the-steam-overlay)). Closing the overlay ends the pause.

## Movies

Of the 334 movies in `BioGame\Movies` and the DLC, only `StartupLoop_a`, the startup movie, has Bink audio. Every other
movie's sound is a Wwise event, which `BinkPause` does not stop. One player, `GFullScreenMovie`, plays every fullscreen
movie, for three kinds of caller:

| Caller | In the game files | Movie pause |
|---|---|---|
| Kismet `BioSeqAct_MovieBink` | 188 live actions, 71 movies | When its sound is known |
| Matinee track `SFXInterpTrackMovieBink` | 442 tracks, 240 movies | Never: nothing latches them |
| Loading screens (`SFXLoadMovieManager`) | 45 movies | Never: no known sound |

So Kismet plays only some LE3 movies. Most are Matinee tracks, inside cutscenes and conversations, or loading movies.
Nothing latches those, so the movie pause leaves Esc and Start to the game there. The world pause may still take Esc
([Esc and the world pause](#esc-and-the-world-pause)).

A fullscreen Kismet movie takes the pause keys when its sound is known (`MoviePolicy::BinkAndEvent`). The action
latched last must play this movie, name a Wwise event, and run nothing beside it that may make a sound (`MovieActionOf`
in `src/game/KismetMovie.hpp`). The movie player posts that event as the movie starts, and Pause Anytime pauses it with
`ExecuteActionOnEvent` right after `BinkPause`. The Bink-audio list is empty, because the game's own screens play over
`StartupLoop_a`.

- `SFXSeqAct_PlatformMovieBink` counts as a `BioSeqAct_MovieBink`. Its `Prime` input and `Primed` output preload the
  file and do not count as a start.
- LE3 has no `BioSeqAct_StartConversation`, so the conversation op for the beside test is `SFXSeqAct_StartAmbientConv`.
- Only `Genesis2Title` has localized files, from `DLC_CON_DH1` and `DLC_METR_Patch01`.

Of the 188 live movie actions, 122 post a Wwise event, and 114 of those pass `MovieActionOf`. They include the relay
jump, the first Citadel arrival, Omega's intros, the Leviathan arrivals and the memorial flashbacks of the ending. A
Matinee, music, a conversation, a sound loop or a nested sequence runs beside the 8 others: the Mars arrival, the
Citadel DLC's two Normandy movies, `Device_Reveal`, the Kro001 animatic, the intro of Hackett's speech,
`Lev002_Asteroid_Depart` and `Lev004_WW_ArrivalCrash4`.

### Movie keys

The engine's movie input is the Bink player's native `InputKey`, slot 5 of its `FViewportClient` vtable, at RVA
`0x24afc0`. It sees a press before `Console.InputKey`.

- Its keys are Escape, SpaceBar, XboxTypeS_Back, XboxTypeS_X and XboxTypeS_Start (X and Start unless the movie's flags
  turn them off). On a press it asks the player to stop the movie and reports the key as handled.
- The movie stops only if `[FullScreenMovie] SkippableMovies` lists it: `Device_Reveal`, `ProCit_100_Arrival`, and the
  Cat002 and Cat003 arrivals.
- Flag 0x400 makes it ignore every key. Matinee tracks set it, and so does Kismet while a cutscene runs, as for the
  relay jump. The keys then go on to `SFXGameModeMovie.SkipMovie`, a script that skips the cutscene and nothing else.
- Flag 0x80, on movies opened by file handle, turns the movie input off.

`src/MovieInput.cpp` hooks it after checking its first 28 bytes. The hook keeps these presses from it: Esc and Start
on a movie that owns the pause keys, any press under the movie pause, and a Space press that the movie's thread just
resumed on. They go on to `Console.InputKey`. Esc toggles the pause on the movie's thread, Start on the game thread, and
Space or A resume. Space and X pass unchanged, so with nothing paused they work as in the game without mods.

### Esc and the world pause

A movie with flag 0x400 or 0x80 lets Esc's press reach `Console.InputKey`. If no movie owns the pause keys and the mode
is 7 or 8, that press pauses the world. The Bink picture plays on, and the pause screen shows only while the HUD draws.
In a test, a movie opened from memory during the Citadel arrival conversation was refused, and Esc paused the world.

## Photo mode

Y or F7 under the world pause opens the game's photo mode on the paused shot, for the controller that paused.

- The photo panel is a GFx movie of `SFXGUIInteraction` (`PhotoGuiGFx.cpp`). `ShowPhotoMode(PC)` runs
  `EnableMode(Photo)` and `OpenMovie(MovieTag_PhotoMode)`, and `HidePhotoMode(PC)` undoes both.
- The conversation panel is the `BioSFHandler_Conversation` movie from `GetConversationHandler(PC)`. It is set aside
  with `SetVisible(FALSE)` and `SetInputEnabled(FALSE)`, which clear `m_bDisplayingWheel` and
  `m_bDisplayInvestigateSubMenu`. The exit writes those back.
- That movie advances under photo mode and can show itself again, so it is hidden again on each frame, and its input
  is turned off again when the game turned it on. The first time in a session, the log says so.
- The subtitle is left alone, photo mode's GUI sounds are not hooked, and the tether is widened on foot only.
- `Activated` turns the modes' input back on after 0.5 s (`EnableInputWrapper`): photo mode's timers are not held.

The GFx movies' `Tick` checks no `Pauser`. But most movies advance on the world's `TimeSeconds`, which stands still
under the pause, so photo mode's panel would never load. For each call during the session, `GfxClock` moves
`TimeSeconds` on by the real time. Then it puts it back, along with the movie stamps that the call wrote (+0x44 of each
entry, in arrays at (0x4c, 0x54) and (0xbc, 0xc4)). Without `GfxClock`, the labels stayed "TEST STRING".

The natives come from a memory dump of the running game. Each hook first compares the bytes listed in `LE3.hpp`.

| Native | RVA | Bytes checked |
|---|---|---|
| `BioPlayerInput::InputKey` (vtable slot 88) | `0xa5a040` | 24 |
| `UPlayerInput::InputAxis` (slot 89, not overridden) | `0x54ac90` | 16 |
| The GFx movies' `Tick` (slot 18 of vtable `0x12c7e48`) | `0x92d7a0` | 16 |

`UInput::InputKey` (`0x54b2f0`) drops keys while the `Pauser` (+0x670) is set, and `InputAxis` checks it at
`0x54ad9e`.

## Pause screen

The look is `OverlayLook::LE3`, the `Le3Look` table in `src/Overlay.cpp`, taken from the pause wheel `MainWheel`.
Sizes are at 1080 lines, and the title is 1.5 times the wheel's.

| Part | LE3 |
|---|---|
| Title | `#FFFFFF`, bold, 63, spacing 1.5 |
| Labels | `#ACF5FE`, bold, 22.8, spacing 0.9 |
| Keys | Light, 18, spacing 0.75 |
| Band | `#08759C` to `#084E7E`, rules `#0896F7` |
| Blur tint | 0.88, 0.97, 1.06 (blue) |
| Light font | `Myriad Pro+DFGGothicP-W5` |
| Bold font | `Biomass+DFGSoGei-W7` |
| Words | Resume 157151, photo mode 817979 |
| String table | `ASFXGame.GetSimpleString` |
| Gamepad A, Y | `GUI_SF_MainWheel.Xbox_ControllerIcons_ABtn`, `_YBtn` |
| Button art | 256 texels, disc at (128, 128), 108 across |
| Texture view | `{ 0x114CD18, 0x24, 0x12FE328, 0xC, 0x10 }` |

- Both fonts are in the font library `GUI_SF_Fonts.gfxfonts` of `SFXGUI_Fonts.pcc`, for every language. By the first
  HUD it is no longer in the object list, so at the main menu `Pause.cpp` loads it once through
  `SFXGUI_Fonts.ObjectReferencer_448`.
- The title is Pause Anytime's own word. "Pause" (174771) stays English in Russian, Polish and Japanese, and "Pause
  Game" (687508) is a sentence in French.
- The sounds `InGameGuiEnter` and `InGameGuiExit` play through `SFXGUIInteraction.PlayGuiSound`. The first starts
  with a Wwise Pause All and the second with a Resume All. So every end of the pause posts `InGameGuiExit` once. If it
  does not play, as after a load, it is tried again on the next frames, 3 times at most.

## Engine facts

- The modes are `SFXGameModeManager.EGameModes`, read from `BioPlayerController.GameModeManager2`: Conversation 7,
  Cinematic 8, Movie 10, Photo 14. `ModeNames` in `LE3.hpp` holds all 25, and the log prints each mode's name.
- The pinned LExASIs SDK gives `ProcessEvent` at RVA `0xf4f80` and `GEngine` at `0x18b31d0`. Neither is checked
  against the exe's bytes.
- `AK::SoundEngine::ExecuteActionOnEvent` and `GetIDFromString` come from the exe's export table.
- The movie log lines read `GFullScreenMovie` at RVA `0x1cb9cd0`: its flags at +0x58 and its skippable flag at
  +0x84. Only the logs use these reads, under an access-violation guard.
- Seen in the game: `BioHUD.PostRender` runs every frame in conversations and cutscenes, and stops over a fullscreen
  movie.

## Open questions

1. Matinee-track and file-handle movies inside a conversation or a cutscene: the mode during them, and what Esc does
   there without mods, are not checked. If the mode is 7 or 8, Esc pauses the world while the picture plays on.
2. Photo mode's GUI sounds under the pause (clicks, tabs, options) have not been checked.
3. X under the movie pause: the hook keeps it from the movie input, but outside the guard window its `SkipMovie` bind
   passes and ends the movie pause. The `SkipMovie` script then skips a cutscene that the game lets you skip.
