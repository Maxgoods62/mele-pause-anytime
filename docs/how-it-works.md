# How MelePause works

MelePause is an ASI plugin. The ASI loader, LEBinkProxy, loads it after the game's code is decrypted and gives it a
shared hook manager, which MeleBridge and other ASIs use too. The sources:

| File | What it holds |
|---|---|
| `src/Entry.cpp` | The loader entry: the log, the SDK, then the hooks in `Pause.cpp`, `Bink.cpp` and `Overlay.cpp`. |
| `src/Pause.cpp` | The keys, the world pause, photo mode, the skip guard and the fallback banner. Game thread only. |
| `src/Bink.cpp` | The movie pause, on the thread that plays the movie. |
| `src/Overlay.cpp`, `src/Blur.cpp` | The pause screen, on the render thread. |

## Hooks

| Hook | What it does |
|---|---|
| `ProcessEvent`: `Console.InputKey` (`Pause.cpp`) | Every key that gets past the engine reaches it, also while the world is paused. On an Esc or Start press it reads the local player's controller (GEngine's first `LocalPlayer`), its `GameModeManager.CurrentMode` and `WorldInfo.Pauser`, then pauses or resumes the world, or lets the key through. A press it acted on is consumed (`ReturnValue` TRUE, the original not called), with that key's repeats and release. While a fullscreen movie plays, or MelePause's movie pause is on, every Esc event is consumed without acting, except in the open console: the movie's thread toggles the movie pause. Start's press never arrives then, so the first repeat or release of a Start press it did not see toggles the movie pause (`Bink::Toggle`), and the rest of that press is consumed. A Start press it saw and left to the game stays the game's until its release, also if a movie starts under it. A press of Space or A resumes MelePause's world pause (not in photo mode). During the movie pause, any event of either resumes the movie. What it acted on is consumed until the release, as is a Space press the movie's thread resumed on. |
| `ProcessEvent`: `Console.InputKey`, `XboxTypeS_Y` and `F7` | Under MelePause's world pause of a conversation or a cutscene, a press enters photo mode. In MelePause's photo mode, a press of Y, F7, Esc or Start leaves it. A press it acted on is consumed with its repeats and release. Anywhere else the key goes to the game. |
| `ProcessEvent`: `SkipMovie`, `SkipCinematic`, `GuiKey` on an `SFXGameMode` | Dropped while a press of Esc, Start, Space or A that MelePause took is down (2 s at most), and for 250 ms after its release, whether `Console.InputKey` or the movie's thread took it. `GuiKey` only for the Escape and Start events that the always-on Escape and Start binds send to Scaleform. This guards against a consumed key that still reaches PlayerInput's mode bindings. |
| `ProcessEvent`: after `BioHUD.PostRender` | Notes the time (the movie's thread tells a fullscreen movie by the HUD's silence), checks the world pause every frame, lifts a movie pause once the HUD has drawn for a second, and publishes what the pause screen shows. When the pause screen cannot draw, it draws the fallback banner on the HUD's Canvas (`Canvas.TextSize` and `Canvas.DrawText`, GEngine's `MediumFont`). |
| `ProcessEvent`: every call on the game thread | Notes the time, for the movie reports only: does the game thread still run under a fullscreen movie? |
| `ProcessEvent`: after `PlayerController.PlayerTick` on the player's controller | While photo mode is on: `SFXPlayerCamera.UpdateCamera`, so the camera follows the controller that `PlayerPhotoMode.PlayerMove` just moved. The engine does not update the camera under the world pause. |
| `ProcessEvent`: `ShowMenuGui` on `SFXGameModePhoto` | Dropped while MelePause's photo mode is on: photo mode's own Esc and Start binds would hide it and open the pause menu over the conversation. |
| Native: `BioPlayerInput::InputKey` (RVA 0xc6de70), `UPlayerInput::InputAxis` (RVA 0x68bc40), `UBioSFManager::Tick` (RVA 0x193bc0) | While photo mode is on, a call on the controller's `PlayerInput` or on the GUI manager runs with `WorldInfo.Pauser` cleared, and the `Pauser` is put back after. The engine drops input and stops the GUI under the world pause. |
| Native: `UBioSFManager::PlayGuiSound` (RVA 0x191ee0), `UAudioDevice::CreateComponent` (RVA 0x2ff470) | While photo mode is on, the sound the GUI manager's `PlayGuiSound` makes is marked `bNonRealtime`: under the world pause the audio device starts only those. |
| `dxgi`: `IDXGISwapChain::Present`, `IDXGISwapChain1::Present1`, `IDXGISwapChain::ResizeBuffers` (`Overlay.cpp`) | Draws the pause screen into the back buffer before the frame is shown, once per frame, whichever of the two presents it. Releases its views of the back buffer before a resize. The methods' addresses come from a throwaway device and swap chain made at start. |
| `bink2w64_original.dll`: `BinkOpen`, `BinkOpenWithOptions`, `BinkClose`, `BinkWait`, `BinkDoFrame`, `BinkDoFrameAsync` (`Bink.cpp`) | Tracks the open movies (from a file or from memory) and logs who plays them. `BinkWait` also reads Esc, toggles the movie pause on its press and applies a pause; reads Space, which only resumes; and, while the movie stays paused, presents the pause screen. `BinkDoFrame` and `BinkDoFrameAsync` count calls and apply a resume, never a pause. |

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
   photo tether (`PhotoModeTetherDistance`, 1000) is widened when the shot is far from the pawn.
4. `ShowPhotoMode`, the game's own entry, places the controller on the shot and clears the subtitle. The subtitle line
   is put back at once in the hidden conversation panel. The GUI ticks let past the pause (step 5) run the panel's
   handler, whose `Update` would hide the line, and nothing would show it again before the world resumes. Then the
   ignore counters are zeroed and `bAlwaysTick` is set on the controller, so `PlayerTick` runs under the pause.
5. Input. The engine drops it while `WorldInfo.Pauser` is set: `UInput::InputKey` keeps only the releases of keys held
   from before, and `UPlayerInput::InputAxis` skips the axis binds, so the sticks, the keys and the mouse never reach
   photo mode. The GUI manager's tick (`UBioSFManager::Tick`) stops too: no panel's movie advances (photo mode's tabs
   and options do not change on screen), the focus stays, and the button prompts are not refreshed. Vanilla photo mode
   never meets this, since it pauses with `bPlayersOnly`. Three native hooks clear the `Pauser` around each call while
   photo mode is on and put it back as the call returns: `BioPlayerInput::InputKey` and `UPlayerInput::InputAxis` on the
   controller's `PlayerInput`, and `UBioSFManager::Tick` on the GUI manager. The game thread runs them between the
   world's ticks, so nothing else sees the world unpaused. The key that opened photo mode sets
   `PlayerInput.bUsingGamepad` (Y the pad, F7 the keyboard), as it would have if it had reached the game: photo mode's
   button prompts follow it, and the pad's sticks need it. The world pause's input "absorb" step does not get in the
   way: with `bAlwaysTick` the controller is ticked instead.
6. Sounds. Under the world pause the audio device (its `Update` gets `bGameTicking` off) starts and advances no sound
   except an `AudioComponent` marked `bNonRealtime`, so photo mode's click (`TakePhoto`), tab and option sounds would
   wait for the pause to end and then all play at once. Two hooks, `UBioSFManager::PlayGuiSound` on the GUI manager and
   `UAudioDevice::CreateComponent` inside it, mark the component it makes `bNonRealtime`, so it plays at once.
7. One `UpdateCamera`, with `LastTransitionTime` pushed back first: `GameTimeSeconds` stands still under the pause, and
   a camera switch within 0.1 s of the last is refused. The photo cameras get the shot's field of view. They start at
   54.4 degrees, and a conversation shot is often near 24.

## Photo mode, leaving

`ExitPhoto` calls `HidePhotoMode`, the game's exit, which turns the mode off and pops the controller's state. Then it
gives back:

- the static console binds that photo mode's `Deactivated` resets to the defaults, such as the conversation's stick swap
  for a southpaw profile. A key bound twice, told apart by Ctrl, Shift or Alt, is left alone, because the game's setter
  edits only the first. The PC binds are not touched: neither a conversation nor a cutscene changes them, and their
  defaults hold such pairs (Enter and Alt+Enter);
- every field changed on entry;
- the subtitle line, when the entry could not put it back (`BioSubtitles.DisplaySubtitle`);
- the controller's place, then one `UpdateCamera`. A camera the conversation had frozen is frozen again after that
  update, on the saved shot.

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

Exo 2 is compiled into `MelePause.asi` as resources, Latin and Cyrillic only, about 68 KB a weight. The shaders compile
at the game's first frame with Windows' own `d3dcompiler_47.dll`. Dear ImGui's import of it is delay-loaded and bound to
the copy in System32 first, so a copy placed next to the game is never used, and a Windows without it gets the banner.

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
