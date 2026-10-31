# How MelePause works

MelePause is an ASI plugin, one file per game: `LE1MelePause.asi` for LE1, `LE2MelePause.asi` for LE2 and
`LE3MelePause.asi` for LE3. The ASI loader, LEBinkProxy, loads it after the game's code is decrypted and gives it a
shared hook manager, which MeleBridge and other ASIs use too. The sources:

| File | What it holds |
|---|---|
| `src/Entry.cpp` | The loader entry: the log, the SDK, then the hooks in `Pause.cpp`, `Bink.cpp`, `MovieInput.cpp` and `Overlay.cpp`. The loader loads every file of the ASI folder before it attaches any, so a copy of version 6 or older (`Game::LegacyModule`, `MelePause.asi` for LE1) is already loaded then: if it is, the entry installs nothing and leaves the log to it (the loader would refuse its hooks anyway: same names), says so to a debugger (DebugView), and returns false, which the loader logs in `Logs\bink2w64_proxy.log`. |
| `src/SharedVersion.h`, `src/MelePause.rc` | The name and version the loader logs, and the Windows version info (`N.0.<game id>.0`) and Exo 2 fonts compiled into `LE<n>MelePause.asi`. |
| `src/Pause.cpp` | The keys, the world pause, the skip guard and the fallback banner. Game thread only. |
| `src/Photo.hpp`, `src/Photo.cpp` | Photo mode under the pause: the session, the camera, and the natives that let its input, panel and (in LE1) sounds past the pause. The natives' RVAs, the conversation panel and the camera switch gate come from `game/LE<n>.hpp`. |
| `src/PhotoGui.hpp`, `src/PhotoGuiSF.cpp`, `src/PhotoGuiGFx.cpp` | Photo mode's GUI side, as `Photo.cpp` asks for it: the game's entry and exit to photo mode's panel, and the conversation's panel set aside while it shows. `PhotoGuiSF.cpp` for LE1's and LE2's ScaleForm GUI manager (`BioSFPanel` flags), `PhotoGuiGFx.cpp` for LE3's GFx movies (calls on the native movie), picked by `MELEPAUSE_PHOTO_GFX`. |
| `src/PhotoOff.cpp` | The same calls for a game whose `game/LE<n>.hpp` turns photo mode off (`MELEPAUSE_PHOTO 0`): Y and F7 stay the game's. |
| `src/Engine.cpp` | Helpers for calling into the game and reading its objects (`CallGame`, `PathOf`, `Alive`, ...). |
| `src/game/` | What differs per game: `Game.hpp` picks `LE1.hpp`, `LE2.hpp` or `LE3.hpp` for the build's SDK target (modes, GuiKey events, the photo mode and movie pause switches, ...). `LE2.hpp` leaves photo mode's GUI sounds unhooked and gives the movie pause only the movies whose sound it knows ([games/LE2.md](games/LE2.md)); `LE3.hpp` does the same for movies, gives photo mode its GFx side and the layout `GfxClock` reads ([games/LE3.md](games/LE3.md)). `KismetMovie.hpp`, which `LE2.hpp` and `LE3.hpp` include, reads a Kismet movie action: its movie, its Wwise event, and whether anything beside it may make a sound. |
| `src/Bink.cpp` | The movie pause, on the thread that plays the movie. |
| `src/MovieSound.cpp` | LE2 and LE3: the Wwise event a Kismet movie action posts beside the picture, latched on the game thread and paused beside `BinkPause` with the exe's `AK::SoundEngine::ExecuteActionOnEvent`. Inert in LE1. |
| `src/MovieInput.cpp` | LE2 and LE3: the hook on the engine's movie input, which would skip a movie on Esc's and Start's press. Nothing in LE1. |
| `src/Overlay.cpp`, `src/Blur.cpp` | The pause screen, on the render thread. |
| `src/Api.hpp` | What `Pause.cpp`, `Bink.cpp`, `MovieSound.cpp`, `MovieInput.cpp` and `Overlay.cpp` call of each other. No game class: `Bink.cpp`, `MovieSound.cpp` and `Overlay.cpp` include only it. |

## Hooks

| Hook | What it does |
|---|---|
| `ProcessEvent`: `Console.InputKey` (`Pause.cpp`) | Every key that gets past the engine reaches it, also while the world is paused. On an Esc or Start press it reads the local player's controller (GEngine's first `LocalPlayer`), its `GameModeManager.CurrentMode` and `WorldInfo.Pauser`, then pauses or resumes the world, or lets the key through. A press it acted on is consumed (`ReturnValue` TRUE, the original not called), with that key's repeats and release. While a fullscreen movie plays, or MelePause's movie pause is on, every Esc event is consumed without acting, except in the open console: the movie's thread toggles the movie pause. Start's press never arrives then, so the first repeat or release of a Start press it did not see toggles the movie pause (`Bink::Toggle`), and the rest of that press is consumed. A Start press it saw and left to the game stays the game's until its release, also if a movie starts under it. A press of Space or A resumes MelePause's world pause (not in photo mode). During the movie pause, any event of either resumes the movie. What it acted on is consumed until the release, as is a Space press the movie's thread resumed on. |
| `ProcessEvent`: `Console.InputKey`, `XboxTypeS_Y` and `F7` | Under MelePause's world pause of a conversation or a cutscene, a press enters photo mode. In MelePause's photo mode, a press of Y, F7, Esc or Start leaves it. A press it acted on is consumed with its repeats and release. Anywhere else the key goes to the game. |
| `ProcessEvent`: `SkipMovie`, `SkipCinematic`, `GuiKey` on an `SFXGameMode` | Dropped while a press of Esc, Start, Space or A that MelePause took is down (2 s at most), and for 250 ms after its release, whether `Console.InputKey` or the movie's thread took it. `GuiKey` only for the Escape and Start events that the always-on Escape and Start binds send to Scaleform. This guards against a consumed key that still reaches PlayerInput's mode bindings. |
| `ProcessEvent`: after `BioHUD.PostRender` | Notes the time (the movie's thread tells a fullscreen movie by the HUD's silence), checks the world pause every frame, lifts a movie pause once the HUD has drawn for a second, and publishes what the pause screen shows. When the pause screen cannot draw, it draws the fallback banner on the HUD's Canvas (`Canvas.TextSize` and `Canvas.DrawText`, GEngine's `MediumFont`). |
| `ProcessEvent`: every call on the game thread | Notes the time, for the movie reports only: does the game thread still run under a fullscreen movie? |
| `ProcessEvent`: `Activated` and `Deactivated` on a `BioSeqAct_MovieBink` (LE2, LE3) | Latches the movie it plays, the id of the Wwise event it posts, and whether its Kismet runs more beside the movie that may make a sound; clears the latch at `Deactivated`. |
| The engine's movie input, a native `InputKey` (LE2, LE3, `MovieInput.cpp`) | Every key event reaches it before `Console.InputKey`. While the pause keys are the movies', a press of Esc or Start skips it and goes on to `Console.InputKey`; under the movie pause, every press does. Space and X reach it as Enter in LE2, so they skip a movie the game lets skip, and as they are in LE3, where they are its keys. The rest goes to it unchanged ([games/LE2.md](games/LE2.md#movie-keys), [games/LE3.md](games/LE3.md#movie-keys)). |
| `ProcessEvent`: after `PlayerController.PlayerTick` on the player's controller | While photo mode is on: `SFXPlayerCamera.UpdateCamera`, so the camera follows the controller that `PlayerPhotoMode.PlayerMove` just moved. The engine does not update the camera under the world pause. |
| `ProcessEvent`: `ShowMenuGui` on `SFXGameModePhoto` | Dropped while MelePause's photo mode is on: photo mode's own Esc and Start binds would hide it and open the pause menu over the conversation. |
| Native: `BioPlayerInput::InputKey`, `UPlayerInput::InputAxis`, `UBioSFManager::Tick` (`Photo.cpp`; RVAs LE1 0xc6de70, 0x68bc40, 0x193bc0; LE2 0xa62e00, 0x541790, 0x1a07c0) | While photo mode is on, a call on the controller's `PlayerInput` or on the GUI manager runs with `WorldInfo.Pauser` cleared, and the `Pauser` is put back after. The engine drops input and stops the GUI under the world pause. |
| Native (LE3): `BioPlayerInput::InputKey`, `UPlayerInput::InputAxis` (RVAs 0xa5a040, 0x54ac90) and the GFx movies' `Tick` (0x92d7a0) (`Photo.cpp`) | The input hooks as in LE1 and LE2. The GFx movies' `Tick` checks no `Pauser` but advances most movies on `WorldInfo.TimeSeconds`, which stands still under the pause: while photo mode is on, `GfxClock` moves `TimeSeconds` by the real time around each call, then puts it and the movies' stamps back ([games/LE3.md](games/LE3.md#photo-mode)). |
| Native: `UBioSFManager::PlayGuiSound` (RVA 0x191ee0), `UAudioDevice::CreateComponent` (RVA 0x2ff470) (LE1, `Photo.cpp`) | While photo mode is on, and while MelePause plays the pause's own sound (`PhotoMode::PlayGuiSound`), the sound the GUI manager's `PlayGuiSound` makes is marked `bNonRealtime`: under the world pause the audio device starts only those. LE2's GUI sounds are Wwise events, which play under the world pause: no hook. |
| `dxgi`: `IDXGISwapChain::Present`, `IDXGISwapChain1::Present1`, `IDXGISwapChain::ResizeBuffers` (`Overlay.cpp`) | Draws the pause screen into the back buffer before the frame is shown, once per frame, whichever of the two presents it. Releases its views of the back buffer before a resize. The methods' addresses come from a throwaway device and swap chain made at start. |
| `bink2w64_original.dll`: `BinkOpen`, `BinkOpenWithOptions`, `BinkClose`, `BinkWait`, `BinkDoFrame`, `BinkDoFrameAsync` (`Bink.cpp`) | Tracks the open movies (from a file or from memory) and logs who plays them. `BinkWait` also reads Esc, toggles the movie pause on its press and applies a pause; reads Space, which only resumes; and, while the movie stays paused, presents the pause screen. `BinkDoFrame` and `BinkDoFrameAsync` count calls and apply a resume, never a pause. |

The loader loads the ASI folder's files in name order and attaches them in that order, and a hook installed later runs
before the ones installed earlier. The `ProcessEvent` and `Present` hooks of an ASI whose name sorts after
`LE1MelePause.asi` (`LE1StreamingLevelsHUD`, `MeleBridge.asi`) therefore run before MelePause's: they see the keys
MelePause then takes, and what they draw lies under the pause screen's blur.

Each native hook first checks the function's first bytes against this build of the game. On another build it logs a
warning and patches nothing: photo mode then opens, but what that hook covers does not answer (the camera, the panel or
its sounds). The RVAs come from the game's code dumped from memory, since the executable is encrypted on disk.

## World pause

Esc calls the controller's `Pause` exec, which the `pause` console command also runs (`PlayerController.Pause` ->
`ServerPause` -> `GameInfo.SetPause`), then checks that `WorldInfo.Pauser` is set. Esc again calls it once more, and
`ServerPause` toggles it off. MelePause keeps the paused WorldInfo by its object index.

This pause stops the picture, the voice-over, the music and the lip sync together, and they resume in sync. The pause
menu and photo mode use `WorldInfo.bPlayersOnly` instead, under which the conversation's clock, `TimeSeconds` and the
music keep running.

MelePause never leaves the game paused. Each frame and each Esc, its pause ends when `Pauser` became None without it (a
load, the game itself, the Steam overlay closing: `ClearPause` drops every `CanUnpause` that agrees, MelePause's too),
when that WorldInfo is gone (a map change), or when the mode is no longer Conversation or Cinematic (then it resumes).
It only pauses a world nobody else paused.

## Movie pause

Bink movies keep playing under the world pause, so MelePause pauses the movie itself.

| Question | Answer |
|---|---|
| How is Esc read? | By the thread that plays the movie, in each `BinkWait` (some 250 a second while a movie plays, paused or not): `GetAsyncKeyState(VK_ESCAPE)`, acted on when it goes down, and only while one of the game's windows is in the foreground. Repeats do not count, and a press within 150 ms of the last is a bounce. After half a second without a poll, a key already down is no press, except while the movie pause is on: a press seen late then resumes. During a fullscreen movie the engine's movie input takes Esc's press before `Console.InputKey`, and only the release gets through. |
| When does a movie take the key? | While a fullscreen movie plays: a movie opened from a file (loading screens play from memory) that called `BinkWait` in the last half second, while `BioHUD.PostRender` has not run for a quarter second. The HUD draws every frame in play and not at all over a fullscreen movie, so a movie on a texture in a level never takes Esc. The movie's thread reads nothing of the game except those two times: no game object, no game mode. |
| Which movie? | The newest fullscreen movie. Other open movies are left alone. |
| What does a press do? | It toggles: it pauses that movie, or resumes what MelePause paused. `Console.InputKey` takes no part in it: an Esc press, repeat or release that reaches it then is only consumed, and it arms the skip guard. |
| How is it paused? | The thread that plays the movie calls `BinkPause(movie, 1)` at its `BinkWait`, and only there. `BinkPause` only makes `BinkWait` answer "wait" and stops the sound, so a player that does not call `BinkWait` could not be paused anyway. A resume (`BinkPause(movie, 0)`) is applied at the movie's next call of any kind. |
| And Start? | The movie's thread cannot read it (XInput goes through the game, and Steam Input may stand in for it), and the engine's movie input takes its press as it takes Esc's. What is left reaches `Console.InputKey` on the game thread, which keeps running under the movie: the repeats (after 0.2 s held, then every 0.1 s) and the release. The first of them toggles the same pause through `Bink::Toggle`: a tap acts on its release, a held press on its first repeat. A press with no event for half a second is over (its release lost to a focus change), unless the game thread stalled since its last event (no `ProcessEvent` for a quarter second). The pad is read once a frame, so a stall holds its repeats back too, and one press must not toggle twice. |
| And in LE2 and LE3? | Most movies have no Bink audio: their sound is a Wwise event that a Kismet `BioSeqAct_MovieBink` posts beside the picture. A movie takes the pause keys only when its sound is known (the latched event, or Bink audio), and that event is paused and resumed right after `BinkPause`, on the movie's thread. The engine's movie input would skip a skippable movie on Esc's and Start's press first: `MovieInput.cpp` keeps those presses from it, and gives it Space and X as Enter in LE2. Any other movie keeps vanilla Esc and Start; in LE3 that is most movies, played inside a cutscene or a conversation by a Matinee track that nothing latches. |
| When does it end? | On Esc or Start, when the movie closes, when Space's `SkipMovie` reaches the game, or when the HUD has drawn again for a second (the game moved on). A pause whose movie has made no Bink call for 2 s is dropped, by the next Esc at the latest, which then goes back to the game: nothing reads Esc for that movie any more. A silent game thread does not end it, since Esc is read on the movie's thread. |

## Photo mode, entering

The game's real entry to photo mode, `MassEffectGuiManager.ShowPhotoMode`, has no gate. Called alone in a conversation
it breaks: photo mode's priority is below Conversation's, the conversation panel eats the sticks, and photo mode only
sets `bPlayersOnly`, under which the conversation's clock keeps running. `EnterPhoto` (Y or F7) records each change
before it makes it, so a stop midway gives back what it got to.

1. The controller's timers are held (`bPaused`), as the world pause holds them. The controller is about to tick, and the
   conversation's `SkipNode` timer would move the conversation on.
2. The conversation panel: its input is turned off (else it eats the sticks, and A or Enter would pick a reply) and it
   is hidden (`IsVisible`).
3. `SFXGameModePhoto.Priority` is raised to 3 (`ModePriority_Menus`), above Conversation (2), so `ShowPhotoMode` makes
   it the current mode. In a cutscene, the director's `CameraActor` view target is swapped for the controller, and the
   photo tether (`PhotoModeTetherDistance`, 1000) is widened when the shot is far from the pawn. A blend to a new view
   target (`PendingViewTarget`, such as a cutscene's first second, blending to the director's camera) is held for the
   session: `Camera.UpdateCamera` would mix the photo view with that target's and, at the blend's end, make it the view
   target (in LE1 a `CameraActor` there also clears `CurrentCameraMode` at each update).
4. `ShowPhotoMode`, the game's own entry, places the controller on the shot; in LE1 it also clears the subtitle. The
   subtitle line is put back at once in the hidden conversation panel. The GUI ticks let past the pause (step 5) run
   the panel's handler, whose `Update` would hide the line, and nothing would show it again before the world resumes.
   Then the ignore counters are zeroed and `bAlwaysTick` is set on the controller, so `PlayerTick` runs under the
   pause.
5. Input. The engine drops it while `WorldInfo.Pauser` is set: `UInput::InputKey` keeps only the releases of keys held
   from before, and `UPlayerInput::InputAxis` skips the axis binds, so the sticks, the keys and the mouse never reach
   photo mode. The GUI manager's tick (`UBioSFManager::Tick`) stops too: no panel's movie advances (photo mode's tabs
   and options do not change on screen), the focus stays, and the button prompts are not refreshed. Vanilla photo mode
   never meets this, since it pauses with `bPlayersOnly`. Three native hooks clear the `Pauser` around each call while
   photo mode is on and put it back as the call returns: `BioPlayerInput::InputKey` and `UPlayerInput::InputAxis` on the
   controller's `PlayerInput`, and `UBioSFManager::Tick` on the GUI manager. The game thread runs them between the
   world's ticks, so nothing else sees the world unpaused. LE3's GUI is GFx movies, whose tick checks no `Pauser` but
   advances them on the stopped game clock: there `GfxClock` lets that tick see the real time go by instead
   ([games/LE3.md](games/LE3.md#photo-mode)). The key that opened photo mode sets
   `PlayerInput.bUsingGamepad` (Y the pad, F7 the keyboard), as it would have if it had reached the game: photo mode's
   button prompts follow it, and the pad's sticks need it. The world pause's input "absorb" step does not get in the
   way: with `bAlwaysTick` the controller is ticked instead.
6. Sounds. Under the world pause the audio device (its `Update` gets `bGameTicking` off) starts and advances no sound
   except an `AudioComponent` marked `bNonRealtime`, so photo mode's click (`TakePhoto`), tab and option sounds would
   wait for the pause to end and then all play at once. Two hooks, `UBioSFManager::PlayGuiSound` on the GUI manager and
   `UAudioDevice::CreateComponent` inside it, mark the component it makes `bNonRealtime`, so it plays at once. That is
   LE1; LE2's GUI sounds are Wwise events, which play under the world pause, and get no hook.
7. The camera. `LastTransitionTime` is pushed back first: the game refuses a camera switch too soon after the last,
   and its clock stands still under the pause (LE1: `GameTimeSeconds` and 0.1 s, in `PickCameraMode`; LE2:
   `TimeSeconds` and `TransitionTimeout`, 0.25 s, in `SFXPlayerCamera.UpdateViewTarget`). One `UpdateCamera` then
   switches to the photo camera. The switch is an instant transition, an `SFXCameraMode_Interpolate` of 0.00001 s,
   which an update of 0 s leaves at its start, the old camera's view: one more update of 0.001 s ends it. Its start
   (`InitializeTransition`) also turns the controller to the old camera mode's view. In a director's shot that mode is
   the gameplay camera left behind, which looks elsewhere, so the controller is put back on the shot, place and
   rotation, before a last `UpdateCamera`. The photo cameras get the shot's field of view. They start at 54.4 degrees,
   and a conversation shot is often near 24.

## Photo mode, leaving

`ExitPhoto` calls `HidePhotoMode`, the game's exit, which turns the mode off and pops the controller's state. Then it
gives back:

- the static console binds that photo mode's `Deactivated` resets to the defaults, such as the conversation's stick swap
  for a southpaw profile. A key bound twice, told apart by Ctrl, Shift or Alt, is left alone, because the game's setter
  edits only the first. The PC binds are not touched: neither a conversation nor a cutscene changes them, and their
  defaults hold such pairs (Enter and Alt+Enter);
- every field changed on entry;
- the subtitle line, when the entry could not put it back (`BioSubtitles.DisplaySubtitle`);
- the controller's place, then one `UpdateCamera`, and one of 0.001 s that ends the instant transition back to the
  conversation's or the director's camera (entering, step 7). A camera the conversation had frozen is frozen again
  after that, on the saved shot. A held blend is put back with its time to go, and the saved shot is on screen until
  the world resumes.

Each frame and each key also check the session: if the world pause ends without MelePause, the world goes, or the mode
leaves Photo, the photo session ends.

## Pause screen

`Overlay.cpp` draws into the game's back buffer from the hooked `IDXGISwapChain::Present`, on the render thread. The
blur passes come first (`Blur.cpp`, which sets back every piece of D3D11 state it changes), then the text and icons with
Dear ImGui. Nothing there touches a game object: the game thread publishes what to show each frame after
`BioHUD.PostRender`. Sizes follow the back buffer's height, laid out for 1080 lines.

It shows under the world pause while the HUD draws (a flag left over a load or a fault shows nothing), and under the
movie pause while the HUD does not. It is gone at once, with no fade, when photo mode starts (before photo mode shows
anything) and while the console is open. It comes back three frames after photo mode ends.

A paused Bink movie presents nothing: its thread only polls `BinkWait`. So while a movie from a file is open and the HUD
is silent, each frame is copied before anything is drawn on it. While the movie pause holds and the game has presented
nothing for 0.1 s, the movie's `BinkWait` hook puts that frame back and presents it with the pause screen over it, 60
times a second at most. It does this only on the thread that presents the game's frames, which in LE1 is the movie's
thread. A window resized while the movie is paused keeps its frame until the movie resumes.

The look is the game's (`Game::Look` in `game/LE<n>.hpp`). LE2 and LE3 have MelePause's own: "PAUSE" in a bracketed
frame, each key over its label. LE1 has the colours of its menus: the title on a band (a blue gradient at 55 % opacity
with a #3199C6 rule along each edge, both fading out over 120 px at each end) in #CCFFFF, each key left of its label in
#3199C6, the gamepad's buttons as discs in the colours of the game's own button icons, the keys as translucent grey
keycaps, and the blur tinted toward blue. Nothing of the game's files is drawn or shipped: the colours were read from
them.

In LE1 the words come from the game's string table (`MELEPAUSE_GAME_TEXTS`). On the first frame whose world has a
`BioSPGame`, `Pause.cpp` calls the static native `BioSPGame.GetSimpleString` for the ids in `Game::PauseTexts` (170645,
157151, 184397), tried again on the next two frames while all three come back empty, under its own access violation
guard. It hands them to the overlay as UTF-8 (`Overlay::UseGameTexts`), which takes each one whose every letter is in the
embedded font (`ImFont::IsGlyphInFont`) and keeps its own for the others. The pause, and a key's resume, then play
the GUI manager's `PlayGuiSound` with the rows in `Game::PauseSound` and `Game::ResumeSound` (`InGameGuiEnter` and
`InGameGuiExit`, both `snd_gui.c_snd_guiswish`). The GUI sound hooks mark the sound `bNonRealtime`, so the pause's is
heard at once under the world pause; the resume's plays once the world is unpaused. The movie pause plays none: its keys are read on the movie's thread, which cannot call
the game.

Exo 2 is compiled into `LE<n>MelePause.asi` as resources (`src/MelePause.rc`), Latin and Cyrillic only, about 68 KB a
weight. The shaders compile at the game's first frame with Windows' own `d3dcompiler_47.dll`. Dear ImGui's import of it
is delay-loaded and bound to the copy in System32 first, so a copy placed next to the game is never used, and a Windows
without it gets the banner.

If the swap chain cannot be hooked, the overlay cannot start, it faults, or it gets no view on the back buffer 10 frames
in a row, the banner is drawn instead (the HUD's Canvas, GEngine's `MediumFont`, scaled). There is no banner during a
movie pause, since the HUD does not draw over a fullscreen movie. A back buffer format the blur does not handle gets a
plain darkening. Nothing of the back buffer is held between pauses.

## Crash guards

The key handling, the per-frame checks, the pause screen and the banner each run under an access-violation guard, and
each fault is logged.

| Where the fault happens | What MelePause does |
|---|---|
| The key handling | Passes the key to the game. |
| The per-frame checks | Resumes and drops its pause. Three faults turn the checks off for the session, and the pause screen with them. |
| The pause screen | Turns it off for the session; the banner takes over. |
| The banner | Turns the banner off. |

A missing Bink module or export turns only the movie pause off.

## Engine facts it relies on

- The game loads `XINPUT1_4.dll`. In a conversation, Start reaches `Console.InputKey` as `XboxTypeS_Start` with
  `bGamepad` TRUE, press and release. During the travel movie vanilla Start does nothing, and the first Start event that
  reaches `Console.InputKey` is a repeat (event 2): the engine takes the press.
- The galaxy map's travel movie (`..\..\BioGame\Movies\UCW_Loading_Flyby.bik`, flags `0x01004400`: from a file,
  3840x2160, 150 frames, about 5 s) plays on Bink's own thread, which calls `BinkWait` in a loop (some 250 a second,
  `BinkDoFrame` never). `BioHUD.PostRender` does not run while it plays. The game thread still runs `ProcessEvent` and
  `Console.InputKey`, which gets Esc's release alone. Loading-screen movies play from memory (flags `0x04004400`).
- The engine's movie input gets Esc's press before anything else. In UE3's code (`FFullScreenMovieBink::InputKey`) it
  skips the movie with that press when the movie is skippable. The travel movie is not skippable. A skippable movie is
  skipped as the poll reads the press, and the pause asked for then ends with its close. In the log, a movie closes
  right after an "Esc pressed, read on thread ..." line.
- The HUD's silence tells a fullscreen movie from one in a level. If the HUD kept drawing over some fullscreen movie,
  Esc would stay vanilla during it, and the movie's close line says "never a fullscreen movie (the HUD draws)". A movie
  in a level opened from a file while the HUD is silent for another reason would take Esc; none is known.
- A Scaleform movie with input focus may take Esc before the console. Then nothing reaches MelePause, and the log has no
  "Esc pressed" line. The movie pause does not depend on this.
- Bink 2022.05, the version LE1 ships, can hang a player that calls `BinkPause` and then stops calling `BinkWait` (RAD
  fixed it in 2025.05). MelePause only applies a pause to a movie that calls `BinkWait`, and its reports during a pause
  count those calls. Without them nothing can be applied to that movie and Esc cannot be read for it: after 2 s Esc goes
  back to the game ("has made no Bink call" in the log), and the movie stays paused until it calls Bink again (the
  resume is applied then) or closes.
- Under the world pause, `PlayerTick` runs only on a controller with `bAlwaysTick`, and the engine does not update the
  camera.

## Open questions

- Does a TRUE return from `Console.InputKey` keep Esc from PlayerInput's bindings? Not verified, hence the skip guard.
  The log says, for each `SkipMovie` and `SkipCinematic`, whether it was dropped.
