# How Pause Anytime works

Pause Anytime is an ASI plugin, one file per game: `LE1PauseAnytime.asi` for LE1, `LE2PauseAnytime.asi` for LE2 and
`LE3PauseAnytime.asi` for LE3. The ASI loader is [LEBinkProxy](https://github.com/ME3Tweaks/LEBinkProxy), a
`bink2w64.dll` proxy that loads every `.asi` file in `Game\ME<n>\Binaries\Win64\ASI`. It loads Pause Anytime after the
game's code is decrypted, and gives it a shared hook manager. Other ASIs use the same manager, such as
[MeleBridge](https://github.com/Maxgoods62/mele-maxgoods-toolkit/blob/main/tools/mele-bridge/README.md). Pause Anytime
needs no other mod.

Pause Anytime hooks the game's UnrealScript calls (`ProcessEvent`), a few of its native functions, the Bink 2 movie
library and the DXGI swap chain. In a conversation or a cutscene it sets the game's own world pause, which stops the
picture, the voice-over, the music and the lip sync together. A Bink movie keeps playing under that pause. So Pause
Anytime pauses the movie through Bink, and reads Esc on the movie's own thread. The pause screen is drawn into the
game's back buffer just before each frame is shown.

Pause Anytime is built with Visual Studio 2026 against the shared Visual C++ runtime (`MSVCP140.dll`,
`VCRUNTIME140.dll`). An older `msvcp140.dll` on the player's machine can make it crash. Its log is
`Logs\PauseAnytime.log` next to the game's executable, rewritten at each start. At the end of the start the log has a
line `LE<n>PauseAnytime N ready`, where N is `ASI_VERSION`.

## Source files

- `src/Entry.cpp`: the loader entry. It sets up the log and the SDK, then installs the hooks in `Pause.cpp`, `Bink.cpp`,
  `MovieInput.cpp` and `Overlay.cpp`.
- `src/SharedVersion.h`, `src/PauseAnytime.rc`, `src/Resources.h`: the name and version the loader logs, and the
  Windows version info (`N.0.<game id>.0`, with game ids 4, 5 and 6). Also the Exo 2 fonts compiled into
  `LE<n>PauseAnytime.asi`, whose resource ids are in `Resources.h`.
- `src/CMakeLists.txt`: one build target per game from the same sources, the Dear ImGui library, and the delay-loaded
  `d3dcompiler_47.dll` ([Exo 2 and the shaders](#exo-2-and-the-shaders)).
- `src/fonts/`: Exo 2 Regular and SemiBold, the fallback font, and its licence (`OFL.txt`).
- `src/Pause.hpp`, `src/Pause.cpp`: game thread only. The keys, the world pause, the skip guard and the fallback banner.
  Also the Kismet movie latch (LE2, LE3), what the pause screen shows each frame, and the game's words, gamepad icons
  and fonts for the pause screen.
- `src/Photo.hpp`, `src/Photo.cpp`: photo mode under the pause. The session, the camera, and the natives that let its
  input, its panel and (in LE1) its sounds past the pause. The natives' RVAs, the conversation panel and the camera
  switch gate come from `game/LE<n>.hpp`.
- `src/PhotoGui.hpp`, `src/PhotoGuiSF.cpp`, `src/PhotoGuiGFx.cpp`: photo mode's GUI side, as `Photo.cpp` asks for it.
  That is the game's entry and exit to photo mode's panel, and the conversation's panel set aside while photo mode
  shows. `PAUSEANYTIME_PHOTO_GFX` picks one of the two files:
  - `PhotoGuiSF.cpp` for the ScaleForm GUI manager of LE1 and LE2 (`BioSFPanel` flags);
  - `PhotoGuiGFx.cpp` for the GFx movies of LE3 (calls on the native movie).
- `src/Engine.hpp`, `src/Engine.cpp`: helpers for calling into the game and reading its objects (`CallGame`, `PathOf`,
  `Alive`, ...).
- `src/game/`: what differs per game. `Game.hpp` picks `LE1.hpp`, `LE2.hpp` or `LE3.hpp` for the build's SDK target.
  These hold the modes, the `GuiKey` events, the photo mode switches and more.
  - `LE2.hpp` and `LE3.hpp` leave photo mode's GUI sounds unhooked. They give the movie pause only the movies whose
    sound Pause Anytime knows ([games/LE2.md](games/LE2.md), [games/LE3.md](games/LE3.md)).
  - `LE3.hpp` also gives photo mode its GFx side, and the layout that `GfxClock` reads.
  - `KismetMovie.hpp`, which `LE2.hpp` and `LE3.hpp` include, reads a Kismet movie action: its movie, its Wwise event,
    and whether anything beside it may make a sound.
- `src/Bink.cpp`: the movie pause, on the thread that plays the movie.
- `src/MovieSound.cpp`: LE2 and LE3 only. The Wwise event that a Kismet movie action posts beside the picture. The game
  thread latches it, and the movie's thread pauses it beside `BinkPause` with the exe's
  `AK::SoundEngine::ExecuteActionOnEvent`. It does nothing in LE1.
- `src/MovieInput.cpp`: LE2 and LE3 only. The hook on the engine's movie input, which would skip a movie on the press
  of Esc or Start. It does nothing in LE1.
- `src/Overlay.cpp`, `src/Blur.hpp`, `src/Blur.cpp`: the pause screen, on the render thread.
- `src/GameFont.hpp`, `src/GameFont.cpp`: one of the game's fonts, from a GFx movie, as a TrueType file in memory. Plain
  C++, with no game or ImGui types.
- `src/Api.hpp`: what `Pause.cpp`, `Bink.cpp`, `MovieSound.cpp`, `MovieInput.cpp` and `Overlay.cpp` call of each other.
  It names no game class. `Bink.cpp`, `MovieSound.cpp` and `Overlay.cpp` include it and no game class header, so they
  build the same for every game.

## Hooks

Every hook is installed through the loader's shared hook manager (`InstallHook`).

### UnrealScript calls

All of these go through the `ProcessEvent` hook in `Pause.cpp`.

- `Console.InputKey` with Esc, Start, Space or A: the pause and the resume ([Keys](#keys)).
- `Console.InputKey` with `XboxTypeS_Y` or `F7`: photo mode.
  - Under Pause Anytime's world pause of a conversation or a cutscene, a press enters photo mode. Only the controller
    that paused can do this.
  - In Pause Anytime's photo mode, a press of Y, F7, Esc or Start leaves it.
  - A press the hook acted on is consumed, with its repeats and its release.
  - During a fullscreen movie, and anywhere else, the key goes to the game.
- `SkipMovie`, `SkipCinematic` and `GuiKey` on an `SFXGameMode`: the skip guard.
  - These calls are dropped while a press of Esc, Start, Space or A that Pause Anytime took is down (2 s at most), and
    for 250 ms after its release. The press counts whether `Console.InputKey` or the movie's thread took it.
  - `GuiKey` is dropped only for the Escape and Start events that the always-on Escape and Start binds send to
    Scaleform (`Game::GuiStart`, `GuiEscape` and their releases).
  - The guard is there for a consumed key that still reaches PlayerInput's mode bindings
    ([Open questions](#open-questions)).
  - In LE2 and LE3, Esc skips a skippable cutscene in the game without mods (`SkipCinematic`). The guard also covers
    the `SkipCinematic` of the Esc that paused or resumed. With nothing paused, Space or a gamepad's X still skips a
    skippable cutscene.
  - A `SkipMovie` that did not come from a guarded key ends the movie pause
    ([When the movie pause ends](#when-the-movie-pause-ends)).
- After `BioHUD.PostRender`, once per frame:
  - notes the time: the movie's thread uses the HUD's silence to recognise a fullscreen movie;
  - checks the world pause and the photo session ([World pause](#world-pause));
  - lifts a movie pause once the HUD has drawn for a second;
  - publishes what the pause screen shows;
  - reads the game's words, gamepad icons and fonts in the first frames ([Pause screen](#pause-screen));
  - draws the fallback banner on the HUD's Canvas when the pause screen cannot draw ([Fallbacks](#fallbacks)).
- Every call on the game thread: notes the time. The movie reports use it, to say whether the game thread still runs
  under a fullscreen movie. Start uses it too: a Start press does not count as over while the game thread has stalled
  (no `ProcessEvent` for 0.25 s, [Start](#start)).
- `Activated` and `Deactivated` on a `BioSeqAct_MovieBink` (LE2, LE3), before the original runs:
  - `Activated` latches the movie that the action plays, the id of the Wwise event it posts, and whether its Kismet
    runs anything else beside the movie that may make a sound. The movie that `Activated` starts finds the latch in
    place.
  - `Deactivated` clears the latch, if the latch is still that movie's.
- After `PlayerController.PlayerTick` on the player's controller, while photo mode is on:
  `SFXPlayerCamera.UpdateCamera`. The camera then follows the controller that `PlayerPhotoMode.PlayerMove` has just
  moved. The engine does not update the camera under the world pause.
- Photo mode's menu bind on `SFXGameModePhoto`: `ShowMenuGui` in LE1, `ShowMenu` in LE2 and LE3. The call is dropped
  while Pause Anytime's photo mode is on. Otherwise photo mode's own Esc and Start binds would hide photo mode, and
  open the pause menu over the conversation.

### Keys

`Console.InputKey` sees every key that gets past the engine, even while the world is paused. Pause Anytime watches six
keys there: Esc (`Escape`), Start (`XboxTypeS_Start`), Y (`XboxTypeS_Y`), F7, Space (`SpaceBar`) and A
(`XboxTypeS_A`).

On a press of Esc or Start, the hook reads three things:

- the local player's controller: GEngine's first `LocalPlayer`, or `Console.ConsoleTargetPlayer` as a fallback;
- its game mode manager's `CurrentMode`: `GameModeManager` in LE1, `GameModeManager2` in LE2 and LE3;
- `WorldInfo.Pauser`.

The press then does the first of these that applies:

1. During a fullscreen movie, the press belongs to the movie pause ([Movie pause](#movie-pause)). The movie's thread
   toggles it on Esc, and `Bink::Toggle` toggles it on Start.
2. In Pause Anytime's photo mode, the press leaves photo mode, and the world stays paused.
3. Under Pause Anytime's world pause, the press resumes the world.
4. In a conversation or a cutscene (mode Conversation or Cinematic) whose world nobody has paused, the press pauses
   the world.

Otherwise the key goes to the game. The other rules:

- In the open console (its `Typing` or `Open` state), Esc and Start go to the game: Esc closes the console.
- A press the hook acted on is consumed: `ReturnValue` is TRUE, and the original is not called. That key's repeats and
  its release are consumed too.
- While a fullscreen movie plays, or the movie pause is on, every Esc event is consumed without action, except in the
  open console. The movie's thread handles Esc then.
- In LE1 the engine's movie input takes Start's press during a movie. So the first repeat or release of a Start press
  that the hook did not see toggles the movie pause (`Bink::Toggle`), and the rest of that press is consumed. In LE2
  and LE3, `MovieInput.cpp` lets the press through, and the press toggles the movie pause at once.
- A Start press that the hook saw and passed to the game stays with the game until its release, even if a movie starts
  during the press.
- Space and A only resume, and never pause. A press resumes Pause Anytime's world pause, but not in photo mode, where
  Space takes the picture, and not in the open console. During the movie pause, any event of Space or A resumes the
  movie.
- With nothing paused, Space and A go to the game. Space then skips a conversation line, or a movie or a cutscene that
  the game lets you skip.
- A press of Space or A that the hook acted on is consumed until its release. So is a Space press that the movie's
  thread resumed on.
- Start is `XboxTypeS_Start`, which an Xbox pad sends. A PlayStation pad should send it through Steam Input or
  DS4Windows, but only an Xbox pad has been tested.

### Native functions

The engine's movie input is a native `InputKey` (LE2, LE3, `MovieInput.cpp`). Every key event reaches it before
`Console.InputKey`. On a press:

- of Esc or Start while the pause keys are the movies' (`Bink::OwnsKeys`): the hook returns FALSE without calling the
  original. The press then skips nothing, and goes on to `Console.InputKey`.
- of any key under the movie pause: the same, so no key skips a paused movie.
- of Space, when the movie's thread has just resumed on that press: the same. The movie's thread reads the key at once,
  and the game thread a frame later.
- of Space or X while a movie is open: in LE2 the movie input gets Enter in their place, so they skip a movie the game
  lets you skip, as Esc did. In LE3 they reach it unchanged, because they are its skip keys already.
- of any other key: unchanged. Every event that is not a press goes on unchanged too.

Details per game: [games/LE2.md](games/LE2.md#movie-keys), [games/LE3.md](games/LE3.md#movie-keys).

Photo mode hooks these natives (`Photo.cpp`), at these RVAs:

| Native | LE1 | LE2 | LE3 |
|---|---|---|---|
| `BioPlayerInput::InputKey` | 0xc6de70 | 0xa62e00 | 0xa5a040 |
| `UPlayerInput::InputAxis` | 0x68bc40 | 0x541790 | 0x54ac90 |
| `UBioSFManager::Tick` | 0x193bc0 | 0x1a07c0 | none |
| The GFx movies' `Tick` | none | none | 0x92d7a0 |
| `UBioSFManager::PlayGuiSound` | 0x191ee0 | none | none |
| `UAudioDevice::CreateComponent` | 0x2ff470 | none | none |

- The input hooks and `UBioSFManager::Tick`: the engine drops input and stops the GUI under the world pause. So while
  photo mode is on, a call on the controller's `PlayerInput` runs with `WorldInfo.Pauser` cleared. In LE1 and LE2, so
  does a call of `UBioSFManager::Tick` on the GUI manager. The `Pauser` is put back after the call.
- The GFx movies' `Tick` (LE3) checks no `Pauser`. But it advances most movies on `WorldInfo.TimeSeconds`, which stands
  still under the pause. While photo mode is on, `GfxClock` moves `TimeSeconds` by the real time around each call.
  Then it puts back `TimeSeconds` and the movies' stamps ([games/LE3.md](games/LE3.md#photo-mode)).
- `UBioSFManager::PlayGuiSound` and `UAudioDevice::CreateComponent` (LE1): under the world pause the audio device
  starts only components marked `bNonRealtime`. While photo mode is on, and while Pause Anytime plays the pause's own
  sound (`PhotoMode::PlayGuiSound`), these hooks mark the component that the GUI manager's `PlayGuiSound` makes as
  `bNonRealtime`.
- LE2 and LE3 hook no GUI sound. LE2's GUI sounds are Wwise events, which play under the world pause. LE3's go through
  `SFXGUIInteraction`, and whether photo mode's sounds play under the pause there is not checked yet
  ([games/LE3.md](games/LE3.md#open-questions)).

### DXGI and Bink

- `IDXGISwapChain::Present`, `IDXGISwapChain1::Present1` and `IDXGISwapChain::ResizeBuffers` (`Overlay.cpp`).
  - The two present hooks draw the pause screen into the back buffer before the frame is shown. They draw once per
    frame, whichever of the two presents it. A test present (`DXGI_PRESENT_TEST`) draws nothing.
  - The resize hook first releases the overlay's views of the back buffer.
  - The methods' addresses come from a throwaway device and swap chain, made at start on a hidden window.
- `BinkOpen`, `BinkOpenWithOptions`, `BinkClose`, `BinkWait`, `BinkDoFrame` and `BinkDoFrameAsync` (`Bink.cpp`). The
  hooks go to `bink2w64_original.dll`, or to `bink2w64.dll` when no `bink2w64_original.dll` is loaded. `BinkPause` is
  not hooked: Pause Anytime only calls it.
  - The open and close hooks track the open movies (from a file or from memory), and log who plays them. When a movie
    closes while its Wwise event is paused, that event is resumed.
  - `BinkWait` reads Esc, toggles the movie pause on its press and applies a pause. It also reads Space, which only
    resumes. While the movie stays paused, it presents the pause screen.
  - `BinkDoFrame` and `BinkDoFrameAsync` count calls and apply a resume, never a pause.

### Hook order

The loader loads the ASI folder's files in name order, and attaches them in that order. A hook installed later runs
before the ones installed earlier. So an ASI whose file name sorts after `LE<n>PauseAnytime.asi` runs its
`ProcessEvent` and `Present` hooks before Pause Anytime's. That ASI sees the keys that Pause Anytime then takes, and
what it draws lies under the pause screen's blur. An ASI whose name sorts before runs its hooks after Pause Anytime's.
In LE1, for example, `LE1StreamingLevelsHUD-v5.asi` and `MeleBridge.asi` sort after `LE1PauseAnytime.asi`. The Steam
overlay and ReShade also draw over the frame. Whichever hook runs later draws on top.

### Another build of the game

Each native hook first checks the function's first bytes against this build of the game. On another build it logs a
warning and patches nothing:

- Photo mode's hooks: Y and F7 still open photo mode, but what the missing hook covers does not respond (the camera,
  the panel or its sounds). Without the input hook or the GUI tick hook, the pause screen no longer offers photo mode's
  key.
- The movie input hook (LE2, LE3): Esc and Start skip a movie the game lets you skip, as in the game without mods. In
  LE2, Space and X then skip nothing.

The pause screen reaches the game's gamepad buttons through method tables at fixed RVAs (`Game::TextureView`). On
another build these do not match: the log has a warning, and the pause screen draws its own discs. The RVAs come from
the game's code dumped from memory, since the executable is encrypted on disk.

## World pause

Esc or Start calls the controller's `Pause` exec, which the `pause` console command also runs (`PlayerController.Pause`
-> `ServerPause` -> `GameInfo.SetPause`). Then Pause Anytime checks that `WorldInfo.Pauser` is set. If it is still
None, the pause is refused, and the log has a warning. To resume, the next press calls `Pause` again through the
controller that paused, and `ServerPause` toggles the pause off. Pause Anytime keeps the paused WorldInfo and that
controller by their object indexes. It checks both again before each use, because a map change or a load replaces
them.

This pause stops the picture, the voice-over, the music and the lip sync together, and they resume in sync. The pause
menu and photo mode use `WorldInfo.bPlayersOnly` instead. Under that one, the conversation's clock, `TimeSeconds` and
the music keep running.

Pause Anytime tries never to leave the game stuck in its pause. `Validate` checks the pause on each frame, and on each
press of a key that Pause Anytime watches. The pause ends when:

- `Pauser` became None without Pause Anytime: a load, the game itself, or the Steam overlay closing. (`ClearPause`
  drops every `CanUnpause` that agrees, Pause Anytime's too.)
- the paused WorldInfo is gone: a map change or a load;
- the mode is no longer Conversation or Cinematic. Pause Anytime then resumes the world. Photo mode is allowed while
  Pause Anytime's own photo session runs.

Pause Anytime only pauses a world that nobody else has paused. A few rare cases leave the world paused, and the log
says so each time:

- the controller that paused is gone when the resume comes;
- another pause still holds the world after the resume, for example the Steam overlay;
- a fault happens during the resume.

If the game stays paused, the console's `pause` command resumes it.

In LE2 and LE3 the pause's sounds also control the game's audio. `InGameGuiEnter` starts with a Wwise Pause All, and
`InGameGuiExit` with a Resume All (`Game::PauseSoundHoldsAudio`). A voice line that the Pause All still holds when the
world runs again is lost, and a later Resume All does not bring it back. A test in LE2, with the Steam overlay ending
the pause, showed this. When `InGameGuiExit` was posted 0.7 s after the world resumed, the line was lost. When it was
posted during the pause, the line was kept. So Pause Anytime posts `InGameGuiExit` at these times:

- on every resume that Pause Anytime makes (a key, a change of mode, a fault), in the frame the world resumes;
- once from `Validate` while the Steam overlay is open (`bIsExternalUIOpen`) and the world is still held, because
  closing the overlay ends the pause without a key that Pause Anytime sees;
- late, at every other end of the pause, which gives back only the rest of the audio.

`InGameGuiExit` is not posted at the start of every pause, although that would also keep the line. The pause would
then not be silent, because ambient sound goes on under it (measured in LE2: a peak of 0.018 against 0.000).

## Movie pause

Bink movies keep playing under the world pause, so Pause Anytime pauses the movie itself.

### How Esc is read

The thread that plays the movie reads Esc in each `BinkWait`, with `GetAsyncKeyState(VK_ESCAPE)`. A movie player (the
code that plays the movie) calls `BinkWait` some 250 times a second while a movie plays, paused or not.

- A press counts when the key goes down, and only while one of the game's windows is in the foreground. A press while
  another program's window is in front is ignored.
- Repeats do not count. A press within 150 ms of the last one is a bounce.
- If there was no poll for half a second, a key that is already down does not count as a press. The exception is
  while the movie pause is on: then a press seen late still resumes the movie.
- The same poll reads Space, which only resumes Pause Anytime's movie pause. With no movie pause, Space is the game's.
- Any Esc while one of the game's windows is in front counts, even one meant to close the console or the Steam
  overlay. With a menu open over a fullscreen movie, Esc and Start still go to the movie.
- In LE1 the engine's movie input takes Esc's press before `Console.InputKey`, and only the release gets through. In
  LE2 and LE3, `MovieInput.cpp` keeps the press from the movie input. The press then reaches `Console.InputKey` too,
  which only consumes it.

### When a movie takes the key

A movie takes the pause keys when all three of these are true:

- it was opened from a file (LE1's and LE2's loading screens play from memory; LE3 opens its own from a file, and their
  sound is not known, so they stay the game's);
- it called `BinkWait` in the last half second;
- `BioHUD.PostRender` has not run for 0.25 s.

In LE2 and LE3 its sound must also be known ([LE2 and LE3](#le2-and-le3)). The HUD draws every frame in play, and not
at all over a fullscreen movie. So a movie on a texture in a level never takes Esc. A key pressed in the first quarter
second of a movie is not taken, because the HUD has not been silent long enough yet.

The movie's thread reads no game object and no game mode. Of the game thread's state it reads only these:

- two timestamps: the HUD's last draw, and the game thread's last `ProcessEvent` (for the reports);
- the requests that the game thread posts (`Bink::Toggle`, `Bink::RequestResume`). Start toggles the pause. A resume
  comes from Space or A, a `SkipMovie`, the HUD drawing again for a second, or a fault;
- in LE2 and LE3, the Kismet movie latch that the game thread writes (`MovieSound::Current`);
- in LE3, two fields of the engine's movie player, for the logs only (`Game::MoviePlayer`).

### Which movie

The newest fullscreen movie. Other open movies are left alone.

### What a press does

The press toggles. It pauses that movie, or it resumes every movie that Pause Anytime paused. `Console.InputKey` takes
no part in Esc then. An Esc press, repeat or release that reaches it is only consumed, and arms the skip guard.

### How the movie is paused

The thread that plays the movie calls `BinkPause(movie, 1)` at its `BinkWait`, and only there. `BinkPause` only makes
`BinkWait` respond "wait", and stops the sound. So a movie player that does not call `BinkWait` could not be paused
anyway. A resume (`BinkPause(movie, 0)`) is applied at the movie's next call of any kind.

### Start

The movie's thread cannot read Start: XInput goes through the game, and Steam Input may stand in for it.

- In LE1 the engine's movie input takes Start's press, as it takes Esc's. What is left reaches `Console.InputKey` on
  the game thread, which keeps running under the movie: the repeats (after 0.2 s held, then every 0.1 s) and the
  release. The first of them toggles the same pause through `Bink::Toggle`. So a tap acts on its release, and a held
  press on its first repeat.
- In LE2 and LE3, on a movie that Pause Anytime can pause, `MovieInput.cpp` lets Start's press through to
  `Console.InputKey`. There the press toggles the pause at once.

If a Start press has had no event for half a second, it counts as over. This covers a release lost, for example, to a
focus change. The exception is a game-thread stall since the last event (no `ProcessEvent` for 0.25 s). The pad is
read once per frame, so a stall also delays the repeats, and one press must not toggle the pause twice.

### LE2 and LE3

- Most movies have no Bink audio. Their sound is a Wwise event that a Kismet `BioSeqAct_MovieBink` posts beside the
  picture.
- A movie takes the pause keys only when its sound is known: the latched event, or Bink audio. That event is paused and
  resumed right after `BinkPause`, on the movie's thread.
- The engine's movie input would skip a skippable movie on the press of Esc or Start first. `MovieInput.cpp` keeps
  those presses from it, and in LE2 gives it Space and X as Enter.
- A Kismet movie whose sound is not known keeps the game's own Esc and Start: Esc skips it when the game lets you.
- Nothing latches a movie on a Matinee track (`SFXInterpTrackMovieBink`, inside a cutscene or a conversation), so the
  movie pause leaves it alone. In LE3 that is most movies. What Esc and Start do during such a movie is not tested yet
  ([games/LE2.md](games/LE2.md#open-questions), [games/LE3.md](games/LE3.md#open-questions)).
- Without the exe's `ExecuteActionOnEvent` export, no movie has a known sound, so the movie pause takes none.

The movies whose sound Pause Anytime knows are listed and counted in [games/LE2.md](games/LE2.md#movies) and
[games/LE3.md](games/LE3.md#movies).

### When the movie pause ends

The movie pause ends:

- on Esc or Start;
- on Space or A (Space is read on the movie's thread too);
- when the movie closes;
- when the game gets a `SkipMovie` that did not come from a key that Pause Anytime guards;
- when the HUD has drawn again for a second (the game moved on);
- after a fault in the per-frame checks.

A pause whose movie has made no Bink call for 2 s is dropped, by the next Esc at the latest. That Esc then goes back to
the game, since nothing reads Esc for that movie any more. The movie stays as `BinkPause` left it, and the resume is
applied if it calls Bink again. A silent game thread does not end the movie pause, since Esc is read on the movie's
thread.

## Photo mode, entering

The game's real entry to photo mode, `ShowPhotoMode`, has no gate. It is on `MassEffectGuiManager` in LE1 and LE2, and
on `SFXGUIInteraction` in LE3. Called alone in a conversation, it breaks:

- photo mode's priority is below Conversation's;
- the conversation panel eats the sticks;
- photo mode only sets `bPlayersOnly`, under which the conversation's clock keeps running.

`EnterPhoto` (Y or F7) records each change before it makes it. If entering stops halfway, the changes made so far are
undone. The pause screen goes away first, so photo mode's first screenshot cannot catch it. If the entry is refused,
the pause screen comes back in the same frame.

1. The controller's timers are held (`bPaused`), as the world pause holds them. The controller is about to tick, and
   the conversation's `SkipNode` timer would move the conversation on.
2. The conversation panel is set aside. Its input is turned off: otherwise it eats the sticks, and A or Enter would
   pick a reply. It is also hidden. In LE1 and LE2 this goes through the panel's flags (`bInputDisabled`, `IsVisible`).
   In LE3 the panel is a GFx movie, set aside with its `SetVisible` and `SetInputEnabled`.
3. `SFXGameModePhoto.Priority` is raised to 3 (`ModePriority_Menus`), above Conversation (2). So `ShowPhotoMode` makes
   photo mode the current mode. Then:
   - In a cutscene, the director's `CameraActor` view target is swapped for the controller.
   - The photo tether (`PhotoModeTetherDistance`, 1000) is widened when the shot is far from the pawn.
   - A pending blend to a new view target (`PendingViewTarget`) is held for the whole session. This happens, for
     example, in a cutscene's first second, while the camera blends to the director's camera. Without the hold,
     `Camera.UpdateCamera` would mix the photo view with that target's view, and at the end of the blend make that
     target the view target. (In LE1, a `CameraActor` there also clears `CurrentCameraMode` on each update.)
   - A camera that the conversation froze (`bDisabled`, from `BioConversation.DisableCamera`) is unfrozen for the
     session. A frozen camera would never run the photo camera.
4. `ShowPhotoMode`, the game's own entry, places the controller on the shot. In LE1 it also clears the subtitle line.
   Pause Anytime puts the line back at once, in the hidden conversation panel. It cannot wait. Step 5 lets the GUI
   ticks run under the pause, and they run the panel's handler. The handler's `Update` hides the line when the text
   changes, and nothing would show the line again before the world resumes. Then the ignore counters are zeroed, and
   `bAlwaysTick` is set on the controller, so `PlayerTick` runs under the pause.
5. Input. The engine drops it while `WorldInfo.Pauser` is set. `UInput::InputKey` keeps only the releases of keys held
   from before, and `UPlayerInput::InputAxis` skips the axis binds. The sticks, the keys and the mouse never reach
   photo mode. The GUI manager's tick (`UBioSFManager::Tick`) stops too. No panel's movie advances, so photo mode's tabs
   and options do not change on screen. The focus stays, and the button prompts are not refreshed. Photo mode in the
   game without mods never meets this, since it pauses with `bPlayersOnly`.
   - Three native hooks clear the `Pauser` around each call while photo mode is on, and put it back as the call
     returns: `BioPlayerInput::InputKey` and `UPlayerInput::InputAxis` on the controller's `PlayerInput`, and
     `UBioSFManager::Tick` on the GUI manager. The game thread runs them between the world's ticks, so nothing else
     sees the world unpaused.
   - LE3's GUI is GFx movies. Their tick checks no `Pauser`, but advances them on the stopped game clock. There
     `GfxClock` lets that tick see the real time go by instead ([games/LE3.md](games/LE3.md#photo-mode)).
   - The key that opened photo mode sets `PlayerInput.bUsingGamepad`: Y sets the pad, F7 the keyboard. The key would
     have set it if it had reached the game. Photo mode's button prompts follow it, and the pad's sticks need it.
   - The world pause's input "absorb" step does not get in the way: with `bAlwaysTick`, the controller is ticked
     instead.
6. Sounds. In LE1, under the world pause, the audio device's `Update` gets `bGameTicking` off. The device then starts
   and advances no sound except an `AudioComponent` marked `bNonRealtime`. So photo mode's click (`TakePhoto`), tab
   and option sounds would wait for the pause to end, and then all play at once. Two hooks,
   `UBioSFManager::PlayGuiSound` on the GUI manager and `UAudioDevice::CreateComponent` inside it, mark the component
   that `PlayGuiSound` makes `bNonRealtime`, and the sound plays at once. LE2 and LE3 have no such hook. LE2's GUI
   sounds are Wwise events, which play under the world pause. LE3's go through `SFXGUIInteraction`, and whether they
   play under the pause is not checked yet ([games/LE3.md](games/LE3.md#open-questions)).
7. The camera. `LastTransitionTime` is pushed back first. The game refuses a camera switch too soon after the last
   one, and its clock stands still under the pause:
   - LE1: `PickCameraMode` checks `GameTimeSeconds` against 0.1 s;
   - LE2 and LE3: `SFXPlayerCamera.UpdateViewTarget` calls `PickCameraMode` only when
     `TimeSeconds - LastTransitionTime > TransitionTimeout` (0.25 s in LE2).

   One `UpdateCamera` then switches to the photo camera. The switch is an "instant" transition: an
   `SFXCameraMode_Interpolate` that lasts 0.00001 s. An update of 0 s leaves it at its start, which still shows the old
   camera's view, so one more update of 0.001 s finishes it. The start of the switch (`InitializeTransition`) also
   turns the controller to the old camera mode's view. In a director's shot, that mode is the gameplay camera left
   behind, which looks elsewhere. So the controller's place and rotation are set back to the shot's before a last
   `UpdateCamera`. The photo cameras get the shot's field of view. They start at 54.4 degrees, and a conversation shot
   is often near 24.

On each frame of the session, Pause Anytime sets again what the game may have changed: the ignore counters,
`bAlwaysTick`, the controller's timers, and in LE3 the conversation movie that the game shows again. When no
`PlayerTick` drove the camera in a frame, the camera is updated from `BioHUD.PostRender` instead.

## Photo mode, leaving

`ExitPhoto` calls the game's exit, `HidePhotoMode`, and `DisableMode(Photo)` if the mode is still on. That turns the
mode off and pops the controller's state. Then Pause Anytime gives back:

- the static console binds. Photo mode's `Deactivated` resets them to the defaults, which undoes changes such as the
  conversation's stick swap for a southpaw profile. A key bound twice (told apart by Ctrl, Shift or Alt) is left
  alone, because the game's setter edits only the first bind. The PC binds are not touched: neither a conversation nor
  a cutscene changes them, and their defaults contain such pairs (Enter and Alt+Enter);
- every field changed on entry;
- the subtitle line, when the entry could not put it back (`BioSubtitles.DisplaySubtitle`), unless another line shows
  now;
- the controller's place, then one `UpdateCamera`, and one of 0.001 s that ends the instant transition back to the
  conversation's or the director's camera (entering, step 7). A camera that the conversation had frozen is frozen
  again after that, on the saved shot. A held blend is put back with its time to go, and the saved shot stays on
  screen until the world resumes.

The session is checked on each frame and on each key too. It ends when the world pause ends without Pause Anytime,
when the world or the controller is gone, or when the mode leaves Photo. When the world is gone, the session only
writes back fields on what is still alive, and calls nothing.

## Pause screen

### Drawing

`Overlay.cpp` draws into the game's back buffer from the hooked `IDXGISwapChain::Present`, on the render thread. The
blur passes come first (`Blur.cpp`, which sets back every piece of D3D11 state it changes). Then Dear ImGui draws the
text and the icons. Nothing on that thread touches a game object, only the D3D11 views of two of its textures. The
game thread publishes what to show each frame, after `BioHUD.PostRender`. Sizes follow the back buffer's height, laid
out for 1080 lines. When the game replaces its swap chain (a display mode change), the overlay moves to the new one
once the old one has been silent for 0.5 s.

### When it shows

The pause screen shows under the world pause while the HUD draws. A flag left over from a load or a fault shows
nothing. It shows under the movie pause while the HUD does not draw. It goes at once, with no fade, when photo mode
starts (before photo mode shows anything) and while the console is open. It comes back three frames after photo mode
ends.

### A paused movie's frame

A paused Bink movie presents nothing: its thread only polls `BinkWait`. So while a movie that Esc could pause is open
and the HUD is silent, each frame is copied before anything is drawn on it. Such a movie was opened from a file, and in
LE2 and LE3 its sound is known. Copying starts once the HUD has been silent for 0.125 s, half the 0.25 s that the movie
pause waits for. So a frame is already kept when the pause can start. A movie that stays the game's, such as LE3's
loading screens, costs no copy. When the game thread latches or clears a Kismet movie action, the next `BinkWait` counts
the movies again.

While the movie pause holds and the game has presented nothing for 0.1 s, the movie's `BinkWait` hook puts that frame
back. It presents the frame with the pause screen over it, 60 times a second at most. It does this only on the thread
that presents the game's frames, which in LE1 is the movie's thread. On any other thread the pause screen shows only as
the movie pauses, and the log says so. A window resized while the movie is paused keeps its frame until the movie
resumes.

### Look

The look is the game's. `Game::Look` in `game/LE<n>.hpp` picks one `NativeLook` table in `Overlay.cpp` for each game.
LE2 and LE3 have the same layout as LE1, with their own tables ([games/LE2.md](games/LE2.md#pause-screen),
[games/LE3.md](games/LE3.md#pause-screen)). The colours were read from the game's files; nothing of those files is
shipped.

The backdrop (`Blur.cpp`, with its settings in `Blur.hpp`):

- The whole frame is blurred, the conversation wheel and the subtitle with it. The blur is a dual Kawase filter with 4
  downsample levels at 1080 lines, and one more per doubling of the height (1 to 6 levels). The blur covers the same
  share of the screen at any resolution.
- The frame is half as bright (a multiplier of 0.5 in display terms), 30 % desaturated, and darker toward the corners
  (a vignette of 0.4).
- A tint per game pulls it toward blue in LE1 and LE3, and toward warmer colours in LE2.
- The pause screen fades in over 0.2 s and out over 0.15 s, eased at both ends (smoothstep).

LE1's centre is the title in #CCFFFF on a band like the game's menu bars. The band is a gradient from #174868 to #226E8F
at 55 % opacity, with a #3199C6 rule along each edge. The band and its rules fade out over 120 px at each end.

The bottom left shows each key to the left of its label. The labels are in the game's label colour (#3199C6 in LE1):

- "Resume" names A or Space. Esc and Start resume too.
- "Photo mode" names Y or F7. It shows only where photo mode opens and responds.
- The keys are the gamepad's or the keyboard's, whichever device pressed one of Pause Anytime's keys last
  (`Overlay::UsePadPrompts`). Esc, F7 and Space count for the keyboard; Start, Y and A count for the gamepad.
- The gamepad's A and Y are the game's own textures ([Gamepad buttons](#gamepad-buttons)). Without them, the pause
  screen draws discs to match: green (#7B9E3F) and yellow (#FCD305), darker toward a black rim, each with a dark
  letter.
- The keyboard's keys are translucent grey keycaps, as in `GUI_SF_PhotoMode`, in the game's light font. The space
  bar's keycap is wider than a key, and named in the text language ([Words and sound](#words-and-sound)).

### Gamepad buttons

The pause screen shows the game's own textures of the gamepad's A and Y (`Game::PauseIcons`). In LE1 they are
`XBox_ControllerIcons_I4` and `_I9` of `GUI_SF_SharedAssets`, which load with `Startup_<lang>.pcc` and stay for good
(RootSet). LE2's and LE3's are in their tables. The overlay draws the view that the renderer already holds, because
the textures' CPU copy is freed.

On the game thread:

- `Pause.cpp` finds the two textures by name in the object list, at the first HUD frame.
- It follows `UTexture.Resource` to the RHI texture and its view (`Game::TextureView`). It checks the method table at
  each of the two steps.
- This runs under its own access-violation guard. While a view is missing, it tries again on the next two frames. A
  fault ends the tries.
- It hands the raw pointers to the overlay (`Overlay::UseGameIcons`).

On the render thread:

- A pointer counts as a view only if its method table is that of a view the overlay makes itself on the same device.
  Only then is it called.
- It must also be on the device the overlay draws with, and be a view of a 2D texture. The overlay then holds a
  reference.
- These checks have their own guard too: a fault costs the icons, not the overlay.
- Both views must pass, or neither is used and the pause screen draws its own discs. The discs then stay, even after
  a change of device. A change of device also drops the game's views: the discs are drawn from then on.

In LE1 the textures are BC3_UNORM with one mip and no sRGB view, so ImGui samples them as they are. The disc is 95 of a
texture's 128 texels across, so the texture is drawn 128/95 larger than the disc (`NativeLook::Button`).

### Words and sound

The words come from the game's string table (`Game::PauseTexts`):

- The two labels only, in every game. LE1's are 157151 ("Resume") and 184397 ("Photo Mode"), "Continuer" and "Mode
  photo" in French.
- The title stays Pause Anytime's own, "PAUSE" in English. LE1's word, 170645 (its unused paused box), says "PAUSED".
  LE2's and LE3's string tables have no such word in every language.

On the first frame whose world has a game object (`BioSPGame` in LE1, `SFXGame` in LE2 and LE3), `Pause.cpp` calls the
native `GetSimpleString` for those ids. It tries again on the next two frames while all of them come back empty, under
its own access-violation guard. It hands the words to the overlay as UTF-8 (`Overlay::UseGameTexts`). The overlay uses
a game word only when the font it is set in has every letter of that word (`ImFont::IsGlyphInFont`). Otherwise it keeps
its own word.

Pause Anytime's own words come in the text language that the game's launcher passes: `-OVERRIDELANGUAGE=<code>` to LE1
and LE2, and `-language=<code>` to LE3. They exist in English, French, German, Italian, Spanish, Polish and Russian.
Pause Anytime uses English for Japanese, because Exo 2 has no kana. It also uses English when the game starts without
the launcher's language switch.

The pause, and a key's resume, play the game's menu sound: `InGameGuiEnter` and `InGameGuiExit` (`Game::PauseSound`
and `Game::ResumeSound`).

- LE1: the GUI manager's `PlayGuiSound` rows, both `snd_gui.c_snd_guiswish`. The GUI sound hooks mark the sound
  `bNonRealtime`, so the pause's sound is heard at once under the world pause. The resume's sound plays once the world
  is unpaused.
- LE2 uses `BioSFManager.PlaySound`, and LE3 uses `SFXGUIInteraction.PlayGuiSound`. These sounds are Wwise events that
  also hold and give back the game's audio, so every end of a pause posts `InGameGuiExit` ([World pause](#world-pause)).

The movie pause plays no sound: its keys are read on the movie's thread, which cannot call the game.

### Fonts

The text is set in the game's own fonts (`Game::PauseFonts`):

- LE1 keeps both in one GFx movie, `GUI_SF_Fonts.Fonts` of `Startup_<lang>.pcc`. The game loads it and keeps it for
  good (RootSet).
- LE2 keeps one font in each of two movies.
- LE3 keeps both in its font library, `GUI_SF_Fonts.gfxfonts`.

Their bytes stay in memory as `UGFxMovieInfo.RawData`. LE3's movie is the exception: it is gone from the object list
by the first HUD frame. So in LE3 the game's loader brings it back once, on the game thread (`PauseFonts.LoadPath`),
and the next garbage collection drops it again.

Once the words are known, `Pause.cpp` finds the movies by name and hands their bytes to `Overlay::UseGameFonts`. This
runs on the game thread, under an access-violation guard, and is tried on three frames while there are none.
`GameFont.cpp` walks the movie's tags to the `DefineFont3` tag of each font. It writes a TrueType file in memory that
holds only the letters of the screen: Pause Anytime's own words, the game's words, `F7`, a space and `?`. That is some
3 KB a font, made in under a millisecond. A whole font would cost 2 MB that ImGui keeps for the session, and 800 times
the time. Nothing is written to disk, and neither the ASI nor this repo holds any of it.

The movie is untrusted input, since a mod can replace it. Every read in `GameFont.cpp` is checked and every size is
capped. Anything unexpected gives an empty result, so Exo 2 stays.

The fonts are found by name, because the order and the ids differ in Japanese. In LE1 the movie has no export names:
`$AeroLight` and `$BioMass` are aliases in the exe, of `Tigensu_jpn` and `Biomass_jpn`. The outlines need no
reversing:

- every glyph uses one fill style, its outer contours run opposite to its holes, and flipping y gives TrueType's
  convention;
- curves are quadratic in both formats;
- coordinates are multiples of 10 on a 20480 EM, so 2048 units per EM keep them exactly;
- Biomass's advances must be read unsigned, or W goes backwards.

ImGui has no kerning, so `GameFont::ReadLayout` hands the font's pairs to the overlay, which sets a line letter by
letter. ImGui's size spans the ascent to the descent, not one EM, so the same call gives the factor between the two.

The render thread adds the two fonts to ImGui's atlas before the first frame that shows the screen, and judges the
words again with them. Both fonts must load and draw Pause Anytime's own words, or Exo 2 stays for all the text. LE1's
sizes are the sizes of the game's movies on their 720-line stage, times 1.5:

- the title: Aero Light at 67.5 px per EM, the size of the game's unused paused box (`WindowPop`'s
  `ShowForcedPausedBox`);
- the labels: BioMass at 22.8 px, the size `PC_MainWheel` uses for the same two words next to its B and Y buttons.
  They have the 0.9 px letter spacing of its Resume field (its Photo Mode field gives none);
- a key's name: Aero Light at 18 px, with a spacing of 0.75 px, as in the key bindings of `PCOptions`.

Myriad, LE1's third font, is not used there. LE2 and LE3 use it for the keys. Each game's sizes are in its table
([games/LE2.md](games/LE2.md#pause-screen), [games/LE3.md](games/LE3.md#pause-screen)).

### Exo 2 and the shaders

Exo 2 is compiled into `LE<n>PauseAnytime.asi` as resources (`src/PauseAnytime.rc`). It holds Latin, Latin Extended-A
and Cyrillic only, about 68 KB a weight. If Exo 2 does not load, ImGui's default font stands in.

The shaders, the blur's and Dear ImGui's, compile at the game's first frame, in some 20 ms. They compile once more if a
display mode change gives the game a new device, at that change, not at the next pause. They use Windows' own
`d3dcompiler_47.dll`. Dear
ImGui's import of it is delay-loaded (`src/CMakeLists.txt`) and bound to the copy in System32 first. So a copy placed
next to the game is never used, and a Windows without one gets the banner.

### Fallbacks

The fallback banner is drawn instead of the pause screen when:

- the swap chain cannot be hooked;
- the overlay cannot start (for example without `d3dcompiler_47.dll` in System32), or it faults;
- the overlay gets no view on the back buffer 10 frames in a row.

The banner is drawn on the HUD's Canvas (`Canvas.TextSize` and `Canvas.DrawText`), in GEngine's `MediumFont`, scaled
for 1080 lines. It shows "PAUSE" near the top centre, with the keys under it. These are "Start: resume" and "Y: photo
mode" when Start paused or the player uses a gamepad (`PlayerInput.bUsingGamepad`). Otherwise they are "Esc: resume"
and "F7: photo mode". There is no banner during a movie pause, since the HUD does not draw over a fullscreen movie.

A back buffer format the blur does not handle gets a plain darkening: black at 60 % opacity. The blur handles 8-bit
RGBA, BGRA and BGRX (UNORM or sRGB), R10G10B10A2_UNORM and R16G16B16A16_FLOAT. Nothing of the back buffer is held
between pauses.

## Crash guards

Each of these runs under its own access-violation guard, and each fault is logged. The line names the module and offset
where the fault was raised, and whether that is Pause Anytime's code, the game's or another module's (a driver, an
overlay). A fault in the game's code can still come from a bad pointer that Pause Anytime handed it.

| Where the fault happens | What Pause Anytime does |
|---|---|
| The key handling | Passes the key to the game, and leaves photo mode. |
| The per-frame checks | Leaves photo mode, resumes and drops its pause, and ends a movie pause. |
| Driving the photo camera | Leaves photo mode. |
| The Kismet movie latch | Latches or clears nothing for that call. |
| Reading the game's words, icons or fonts | Stops trying, and keeps its own words, discs or Exo 2. |
| Checking the game's icons on the render thread | Draws its own discs. |
| The resume after a fault | Logs it: if the game stays paused, the console's `pause` resumes it. |
| The pause screen | Turns it off for the session; the banner takes over. |
| The banner | Turns the banner off. |

After three faults, the per-frame checks stay off for the session. Esc and Start still pause and resume. The pause
screen then shows a pause only while its world still exists and is paused, and without photo mode's key.

A missing `Canvas.DrawText` or `Canvas.TextSize` turns the banner off too. Without the Bink module, or without its
`BinkPause`, `BinkClose` or `BinkWait`, only the movie pause is off. The world pause works without it.

## Engine facts it relies on

- In LE1 the game loads `XINPUT1_4.dll`. In a conversation, Start reaches `Console.InputKey` as `XboxTypeS_Start` with
  `bGamepad` TRUE, press and release. During the travel movie, Start does nothing in the game without mods. The first
  Start event that reaches `Console.InputKey` there is a repeat (event 2): the engine takes the press.
- LE1's galaxy map travel movie (`..\..\BioGame\Movies\UCW_Loading_Flyby.bik`, flags `0x01004400`) plays from a file,
  at 3840x2160, 150 frames, about 5 s. It plays on Bink's own thread, which calls `BinkWait` in a loop (some 250 a
  second) and never `BinkDoFrame`. `BioHUD.PostRender` does not run while it plays. The game thread still runs
  `ProcessEvent` and `Console.InputKey`, which gets Esc's release alone. Loading-screen movies play from memory (flags
  `0x04004400`).
- The engine's movie input gets Esc's press before anything else. In UE3's code (`FFullScreenMovieBink::InputKey`) it
  skips the movie with that press when the movie is skippable. LE1's travel movie is not skippable. In LE1 the press
  that the poll reads also skips a skippable movie. The pause that this press asks for ends when the movie closes. In
  the log, such a movie closes right after an "Esc pressed, read on thread ..." line. In LE2 and LE3, `MovieInput.cpp`
  hooks that input and keeps Esc and Start from it on a movie that Pause Anytime can pause
  ([games/LE2.md](games/LE2.md#movie-keys), [games/LE3.md](games/LE3.md#movie-keys)).
- Pause Anytime uses the HUD's silence to tell a fullscreen movie apart from a movie in a level. If the HUD kept
  drawing over some fullscreen movie, Esc would keep the game's own behaviour during it. The movie's close line then
  says "never a fullscreen movie (the HUD draws)". A movie in a level, opened from a file while the HUD is silent for
  another reason, would take Esc. No such movie is known.
- A Scaleform movie with input focus may take Esc before the console. Then nothing reaches Pause Anytime, and the log
  has no "Esc pressed" line. The movie pause does not depend on this.
- Bink 2022.05, the version LE1 ships, can hang a movie player that calls `BinkPause` and then stops calling
  `BinkWait`. RAD fixed this in 2025.05. Pause Anytime only applies a pause to a movie that calls `BinkWait`, and its
  reports during a pause count those calls. Without them nothing can be applied to that movie, and Esc cannot be read
  for it. After 2 s, Esc goes back to the game ("has made no Bink call" in the log). The movie stays paused until it
  calls Bink again (the resume is applied then) or closes.
- Under the world pause, `PlayerTick` runs only on a controller with `bAlwaysTick`, and the engine does not update the
  camera.

## Open questions

- Does a TRUE return from `Console.InputKey` keep Esc from PlayerInput's bindings? This is not verified, hence the skip
  guard. The log says, for each `SkipMovie` and `SkipCinematic`, whether it was dropped. The LE2 and LE3 tests record
  only that the Esc that paused or resumed did not skip the scene ([testing.md](testing.md)). They do not say whether
  its `SkipCinematic` was dropped or never came. A kept log with a line "SkipCinematic on ...: dropped, our Esc" would
  answer it.
