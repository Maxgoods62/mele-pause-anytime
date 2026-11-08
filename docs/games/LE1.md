# Pause Anytime in LE1

This page is for modders. It covers `LE1PauseAnytime.asi`, the build for Mass Effect 1 Legendary Edition. The game's
names, ids and addresses are in [src/game/LE1.hpp](../../src/game/LE1.hpp); every other source file serves all three
games. The shared design is in [how-it-works.md](../how-it-works.md), and test results are in
[testing.md](../testing.md#le1).

## Keys

The world pause starts in game mode 5 (Conversation) or 6 (Cinematic), when nothing else holds the world paused
(`WorldInfo.Pauser` is None).

| Where | Esc or Start | Space or A | Y or F7 |
|---|---|---|---|
| Conversation or cutscene, not paused | Pauses the world | The game's | The game's |
| Under the world pause | Resumes | Resumes | Opens photo mode |
| Photo mode opened from the pause | Leaves it, still paused | The game's (Space takes the picture) | Leaves it, still paused |
| Fullscreen movie | Toggles the movie pause | Resumes a paused movie | The game's |
| Gameplay, menus, galaxy map, open console | The game's | The game's | The game's |

- Without mods, Esc and Start do nothing in an LE1 conversation. In a cutscene, Esc plays an error sound, since the
  game has its cutscene skip turned off, and Start does nothing.
- The [skip guard](../how-it-works.md#skip-guard) drops the `GuiKey` events of the always-on binds: Escape sends
  `KEY_ESCAPE`, 24 (released 46), and Start sends `BUTTON_START`, 21 (released 43).
- While Pause Anytime's photo session runs, photo mode's menu bind, `SFXGameModePhoto.ShowMenuGui`, is always
  dropped.
- The Steam overlay sets `WorldInfo.Pauser`. If it opens over the world pause, a key's resume logs "still paused by
  ...". Closing the overlay ends the pause. LE1's pause sounds hold no audio, so nothing is posted when it opens.

## Movies

Every LE1 movie carries its sound in its `.bik` file (`MoviePolicy::Bink`), and `BinkPause` stops picture and sound
together. LE1 reads no Kismet movie action (`MovieActionOf` returns false), and keeps no list of Bink-audio movies or
localized names.

Any fullscreen movie takes the pause keys: a movie opened from a file that has called `BinkWait` in the last 0.5 s,
while `BioHUD.PostRender` has been silent for 0.25 s. Only the newest such movie is paused. Loading-screen movies play
from memory (flags `0x04004400`, with `BINKFROMMEMORY`), so they never pause.

The galaxy map's travel movie is the main case. Seen in the game, it was
`..\..\BioGame\Movies\UCW_Loading_Flyby.bik`, flags `0x01004400`, from a file, 3840x2160, 150 frames, about 5 s.
Bink's own thread plays it, calling `BinkWait` some 250 times a second and never `BinkDoFrame`, and the HUD does not
draw. The movie starts from the galaxy map, so `CurrentMode` may stay 9 (Galaxy) for the whole movie. The movie code
never decides by the game mode.

### Movie keys

LE1's engine movie input is not hooked (`MovieInputRva` is 0). It gets a key's press before `Console.InputKey`, and
nothing keeps a key from it.

- Esc: the movie's thread reads it with `GetAsyncKeyState` in each `BinkWait` and toggles the pause. Every Esc event
  that still reaches `Console.InputKey` is taken there. In the game, only the release got there.
- A movie that the game skips on Esc is still skipped by that press. In the log, such a movie closes right after an
  "Esc pressed, read on thread ..." line, and the pause it asked for ends with the movie.
- Start: the movie's thread cannot read it, and the engine takes its press. Its first repeat or its release toggles
  the pause on the game thread (`Bink::Toggle`). A tap acts on its release, a held press after about 0.2 s. Such an
  event pauses no movie that opened less than 0.2 s before it, since its press may have skipped the movie before.
- A Start press with no event for 500 ms counts as over. After a game-thread stall of over 250 ms since its last event,
  the 500 ms run from the stall's end. A Start press that the game had before the movie stays the game's until its
  release.
- Space: the movie's thread reads it too, and it only resumes. A resumes from `Console.InputKey`: on its press, or on
  the repeat or release of a press that the engine took.

In LE1 the thread that plays the movie also presents the game's frames. So while the movie stays paused, the
`BinkWait` hook presents its last frame with the pause screen over it, up to 60 times a second.

## Photo mode

Y or F7 under the world pause opens the game's photo mode on the paused shot, for the controller that paused.

- The photo panel is a Scaleform panel of `MassEffectGuiManager` (`PhotoGuiSF.cpp`).
- The conversation panel is `MassEffectGuiManager.m_oConversationPanel`, a `BioSFPanel`. It is set aside with
  `bInputDisabled` 1, `bHandleInputThisFrame` 1 and `IsVisible` 0, and given back as found.
- `SFXGameModePhoto.Activated` clears the subtitle line. Pause Anytime puts it back at once in the hidden panel, with
  `BioSubtitles.DisplaySubtitle` and the time it has left, and again at the exit.
- Photo mode's GUI sounds are hooked. Under the world pause the audio device starts only sounds whose component is
  `bNonRealtime`. Components made inside the GUI manager's `PlayGuiSound` during the session are marked so.
- `PickCameraMode` switches only when `GameTimeSeconds - LastTransitionTime > 0.1 s`, and `GameTimeSeconds` stands
  still under the pause. So `LastTransitionTime` is set to `GameTimeSeconds - 1`.
- The tether is widened on foot (`PhotoModeTetherDistance`) and in a vehicle (`VehiclePhotoModeTetherDistance`).
- When a camera blend ends, its target becomes the view target. In LE1 a `CameraActor` there clears
  `CurrentCameraMode` at every update, so a blend still pending at the entry is held for the session. Before that
  hold, an entry in a cutscene's first second opened 51 units and 8 degrees off the shot, with "camera mode None".

The natives come from a memory dump of the running game. Each hook first compares the bytes listed in `LE1.hpp`.

| Native | RVA | Bytes checked |
|---|---|---|
| `BioPlayerInput::InputKey` (vtable slot 90) | `0xc6de70` | 13 |
| `UPlayerInput::InputAxis` | `0x68bc40` | 11 |
| `UBioSFManager::Tick` | `0x193bc0` | 9 |
| `UBioSFManager::PlayGuiSound` | `0x191ee0` | 15 |
| `UAudioDevice::CreateComponent` | `0x2ff470` | 16 |

`BioPlayerInput::InputAxis` ends in a jump to `UPlayerInput::InputAxis`.

## Pause screen

The look is `OverlayLook::LE1`, the `Le1Look` table in `src/Overlay.cpp`. Sizes are at 1080 lines: the game's
720-line stage times 1.5.

| Part | LE1 |
|---|---|
| Title | `#CCFFFF`, light, 67.5, no spacing |
| Labels | `#3199C6`, bold, 22.8, spacing 0.9 |
| Keys | Light, 18, spacing 0.75 |
| Band | `#174868` to `#226E8F`, rules `#3199C6` |
| Blur tint | 0.88, 0.97, 1.06 (blue) |
| Light font | `Tigensu_jpn` (`$AeroLight`) |
| Bold font | `Biomass_jpn` (`$BioMass`) |
| Words | Resume 157151, photo mode 184397 |
| String table | `ABioSPGame.GetSimpleString` |
| Gamepad A, Y | `GUI_SF_SharedAssets.XBox_ControllerIcons_I4`, `_I9` |
| Button art | 128 texels, disc at (64, 64), 95 across |
| Texture view | `{ 0x10247A8, 0x24, 0x1211D20, 0xC, 0x10 }` |

- The title copies the paused box of `WindowPop` (`$AeroLight` at 45). The labels copy what `PC_MainWheel` sets beside
  its B and Y buttons (`$BioMass` at 15.21, spacing 0.585). The keys copy the key bindings of `PCOptions`
  (`$AeroLight` at 12, spacing 0.5).
- Both fonts are in one GFx movie, `GUI_SF_Fonts.Fonts`, in `Startup_<lang>.pcc`, which stays loaded (RootSet). The
  movie has no export names, and the order differs in Japanese, so the fonts are found by name.
- Myriad Pro Begone (Nexus 1070) writes its own copy of that movie into every `Startup_<lang>.pcc`. Its header is a plain
  SWF's (`FWS`), not `GFX`, and the fonts keep their names. It redraws the ASCII letters and punctuation of
  `Tigensu_jpn` (not its digits or accented letters) and removes every kerning pair that touches them, so with it the
  title has no kerning. `Biomass_jpn` stays the same.
- The title "PAUSE" is Pause Anytime's own word. The game's paused box (170645) says "PAUSED".
- The gamepad art is DXT5, in `Startup_<lang>.pcc`, the same in every language, and it stays loaded.
- The sounds are the GuiSounds 2DA rows `InGameGuiEnter` and `InGameGuiExit`, both `snd_gui.c_snd_guiswish`. They go
  through the GUI manager's `PlayGuiSound` with the component marked `bNonRealtime`, so they need the GUI sound hooks.
  They hold none of the game's audio. The resume sound plays only when a key really unpaused the world.

## Engine facts

- The modes are `SFXGameModeManager.EGameModes`, read from `BioPlayerController.GameModeManager`: Conversation 5,
  Cinematic 6, Movie 8, Galaxy 9, Photo 11. `ModeNames` in `LE1.hpp` holds all 13, and the log prints each mode's name.
- The pinned LExASIs SDK gives `ProcessEvent` at RVA `0x12dd20` and `GEngine` at `0x17813e8`. Neither is checked
  against the exe's bytes.

Seen in the game:

- LE1 loads `XINPUT1_4.dll`. In a conversation, Start reaches `Console.InputKey` as `XboxTypeS_Start` with `bGamepad`
  TRUE, press and release. During the travel movie, the first Start event there is a repeat (event 2).
- The X button sends `SkipMovie` during a movie: the skip guard dropped one 160 ms after a Start release.
- In the background, LE1 holds the world still every frame, so a pause tested then proves nothing.

## Open questions

1. A skippable fullscreen movie: Esc's press skips it through the engine's movie input, and Start's press may too. Can
   one be paused at all? No test covers it.
2. Before the first HUD of a session, the HUD counts as silent. So a movie opened from a file at the main menu would
   take Esc, since LE1 has no sound test. Whether LE1 plays such a movie is not known.
3. X under the movie pause: outside the guard window its `SkipMovie` passes and ends the movie pause (`DropSkip` in
   `Pause.cpp`). No test says whether X resumed the movie.
4. Which keys LE1's engine movie input takes is not read. The code assumes it takes the press of Esc and Start.
5. Not tested yet: a Start press held into its repeats during a movie, Esc and Start mixed in a movie, a PlayStation
   pad.
