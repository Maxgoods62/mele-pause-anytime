# How Pause Anytime works

This page is for modders. It covers the design that LE1, LE2 and LE3 share: one set of sources builds the three files,
and `src/game/LE<n>.hpp` holds what differs. Each game's names, numbers and movie rules are on its own page:
[LE1](games/LE1.md), [LE2](games/LE2.md), [LE3](games/LE3.md). Building is in [BUILDING.md](../BUILDING.md), and test
results are in [testing.md](testing.md).

Each game has its own ASI, `LE<n>PauseAnytime.asi`, loaded by [LEBinkProxy](https://github.com/ME3Tweaks/LEBinkProxy).

## Source files

| File | What it holds |
|---|---|
| `src/Entry.cpp` | Loader entry: log, SDK, each part's hooks, the "ready" line |
| `src/SharedVersion.h`, `PauseAnytime.rc`, `Resources.h` | Loader name and version, Windows version info, Exo 2 resources |
| `src/CMakeLists.txt` | One target per game, Dear ImGui, the delay-loaded `d3dcompiler_47.dll`, the test program `PauseAnytimeTests` |
| `src/Api.hpp` | What the parts ask of each other; no game class |
| `src/Pause.hpp`, `Pause.cpp` | Game thread: the `ProcessEvent` hook, keys, world pause, skip guard, movie latch, what the pause screen shows, fallback banner |
| `src/Engine.hpp`, `Engine.cpp` | Calls into the game (`CallGame`), object checks (`Alive`, `Kept`), the fault guard |
| `src/Photo.hpp`, `Photo.cpp` | Photo mode under the world pause |
| `src/PhotoGui.hpp`, `PhotoGuiSF.cpp`, `PhotoGuiGFx.cpp` | Photo mode's GUI side: ScaleForm (LE1, LE2) or GFx (LE3), picked by `PAUSEANYTIME_PHOTO_GFX` |
| `src/Bink.cpp` | The movie pause, on the movie's thread |
| `src/MovieSound.cpp` | LE2, LE3: a movie's Wwise event |
| `src/MovieInput.cpp` | LE2, LE3: the engine's movie input |
| `src/Overlay.cpp`, `Blur.hpp`, `Blur.cpp` | The pause screen and its blur, in the game's presents, one thread at a time |
| `src/GameFont.hpp`, `GameFont.cpp` | A game font from a GFx movie to TrueType in memory; plain C++ |
| `src/KeyRules.hpp`, `MovieRules.hpp`, `OverlayRules.hpp` | The rules the threads race for (whose key press it is, a movie's pause and its Wwise event, the overlay's gate and swap chain), as plain functions with no game or Windows type |
| `src/tests/` | `PauseAnytimeTests`, the checks of those rules, run by each build ([testing.md](testing.md#the-automated-checks)) |
| `src/game/Game.hpp`, `LE<n>.hpp` | Per game: modes, key events, RVAs, string ids, look, sounds, movie lists. `Game.hpp` picks one |
| `src/game/KismetMovie.hpp` | LE2, LE3: reads a Kismet movie action |
| `src/fonts/` | Exo 2 Regular and SemiBold, `OFL.txt` |

Each `LE<n>.hpp` defines the same names in `PauseAnytime::Game`, plus two switches: `PAUSEANYTIME_PHOTO_GUI_SOUNDS`
(photo mode's GUI sounds need hooks) and `PAUSEANYTIME_PHOTO_GFX` (its panel is a GFx movie).

## Startup

`Entry.cpp` declares the plugin to the loader as `LE<n>PauseAnytime`, for one game (`SPI_GAME_SDK_TARGET`). It loads
after the game's DRM has decrypted the exe (`SPI_PLUGINSIDE_POSTLOAD`). It attaches on a thread of its own
(`SPI_PLUGINSIDE_ASYNCATTACH`), so a hook may be called before `InstallHook` hands back the original. A hook called
that early waits for it.

The attach sets up `Logs\PauseAnytime.log` (rewritten at each start) and the SDK, reads the globals and GEngine, and
hooks `ProcessEvent` and photo mode's natives. Then come `Bink::Install`, `MovieInput::Install`, `Overlay::Install`,
and the line `LE<n>PauseAnytime <version> ready`, even when a part turned itself off.

A part that cannot hook logs what it loses and stays off. Only the core checks are fatal (GEngine, `ProcessEvent`, the
SDK's globals): a failed one logs `failed to resolve <name>` and aborts the game.

The loader names each hook after the plugin name built into the file, so a second copy of the ASI under another file
name would ask for names that are taken. So before it sets up the log, each copy makes a named semaphore for its game
process. A copy that finds one there counts itself in it and stays off, with no log and no hook. After its "ready"
line, the first copy warns `a second copy of LE<n>PauseAnytime.asi is in the ASI folder under another file name` when a
copy has counted itself by then. If the loader still refuses the `ProcessEvent` hook, the attach logs `the loader
refused the ProcessEvent hook` and stops there: no other part is hooked, no "ready" line is written, and the game goes
on without Pause Anytime.

`ProcessEvent` and GEngine are at fixed RVAs of the pinned SDK, and nothing checks their bytes. Every other native has
its first bytes compared before it is hooked, and on another build the log says `not found at RVA ... (another build of
the game?)`. The RVAs come from a memory dump of the running game, since the exe's code is encrypted on disk.

## The ProcessEvent hook

One hook on `UObject::ProcessEvent` sees every UnrealScript call. Calls that Pause Anytime makes itself (`InOwnCall`)
go straight to the game. On the game thread, each call notes the time, and a gap over 250 ms marks a stall. When the
time has moved, it also reads, under a guard, whether the Steam overlay or the console is open, for the movie's thread.
Then:

- LE2, LE3: `Activated` and `Deactivated` go to the Kismet movie latch, before the original.
- Before the original: `InputKey` on a `Console` (the keys), `SkipMovie`, `SkipCinematic` and `GuiKey` on an
  `SFXGameModeBase` (the skip guard), and photo mode's menu bind while our photo session runs (dropped). A key taken or
  a call dropped skips the original.
- After it: `PlayerTick` on our photo session's controller (the photo camera), and `BioHUD.PostRender`.

After each `BioHUD.PostRender` the hook notes the HUD's time, which the movie code reads. Then, under one guard, it:

1. lifts a movie pause once the HUD has drawn for 1 s;
2. runs `Validate` and `PhotoMode::Frame`;
3. publishes what the pause screen shows;
4. reads the game's words and fonts (3 tries each) and its gamepad icons (60 tries, about a second, since the renderer
   may make their views a few frames late), each guarded.

`CallGame` zeroes a function's `iNative` for the call, so `ProcessEvent` runs natives too, and puts it back in a
`__finally`. A game object kept across frames (`Kept<T>`) holds its object index and its class, and is checked before
each use, since a map change or a load replaces it.

## Keys

Pause Anytime watches six keys in `Console.InputKey`: `Escape`, `XboxTypeS_Start`, `XboxTypeS_Y`, `F7`, `SpaceBar` and
`XboxTypeS_A`. A key it takes gets `ReturnValue` TRUE, and the game's `InputKey` is skipped. A press it acts on is
taken with its repeats and its release. The controller is GEngine's first `LocalPlayer`. In the open console (`Typing`
or `Open` state) the keys are the console's. The movie's thread refuses Esc and Space while the console or the Steam
overlay is open, as the game thread read them at its latest tick. It trusts that read only while the game thread has
ticked in the last 250 ms, so a load that began with the overlay open cannot keep a paused movie from resuming.

A press of Esc or Start runs `Validate`, then the first of these that applies:

1. Our photo session ran when the press came: the press ends it and does nothing more, so the world stays paused. If
   `Validate` ended the session first (the world pause ended, or the game left photo mode), the press is only taken.
2. `Validate` resumed our world pause because the mode changed: the press is taken, so the game does not act on it too.
3. A fullscreen movie owns the pause keys (`Bink::OwnsKeys`). Esc is taken without a toggle, since the movie's thread
   saw the same press. Start toggles the movie pause through `Bink::Toggle`.
4. Our world pause holds: resume.
5. The mode is Conversation or Cinematic and `Pauser` is None: pause.

Photo mode comes before the movie: a session runs under our world pause, with no fullscreen movie, and a stall in it
can make the HUD look silent. Otherwise the key is the game's. During a fullscreen movie, every Esc event that reaches
the console is taken. So is an Esc press that the movie's thread acted on, when it reaches the console after its movie
closed.

A pad button repeats after 0.2 s, then every 0.1 s. So a Start press with no event for 500 ms counts as over (its
release was lost). The 500 ms run from its last event, or from the end of a later game-thread stall, since the pad is
read once a frame.

Space and A only resume. Under the movie pause any of their events resumes. Under the world pause only a press does,
and not in photo mode, where Space takes the picture. As with Esc, a press on which `Validate` resumed the world is
taken. Y and F7 open photo mode under our world pause, for the controller that paused, in Conversation or Cinematic.
They never do during a fullscreen movie, or once the per-frame checks are off ([Crash guards](#crash-guards)). In our
photo mode they leave it, before any movie check. Any event of Escape, F7 or SpaceBar switches the prompts to the
keyboard, and any event of Start, Y or A to the gamepad.

### Skip guard

The guard is armed while a press of Esc, Start, Space or A that Pause Anytime took is down (2 s at most), and for
250 ms after its release. It drops `SkipMovie`, `SkipCinematic`, and the `GuiKey` calls with the Escape and Start
events of the always-on binds (ids in `LE<n>.hpp`). An Esc or Space press that only the movie's thread saw arms it
too. That press also counts when the game thread read no input since before it: a stall of over 250 ms that is still
on, or ended less than 250 ms ago. The key's queued events then reach the game thread late, and they are still that
press. Y and F7 do not arm it.

The guard exists for two reasons. The `OnRelease` half of a bind may run after the console saw the release. And it is
not settled whether the console's TRUE return keeps a key from PlayerInput's binds. `GuiKey` is matched by event
number, so while the guard is armed, any key that sends those events is dropped.

## World pause and resume

The pause is `PlayerController.Pause`, as the `pause` console command runs it: `ServerPause` calls
`GameInfo.SetPause(!IsPaused())`, which sets `WorldInfo.Pauser`. It stops the picture, the voice-over, the music and the
lips together, and they resume in sync. The pause menu and photo mode use `WorldInfo.bPlayersOnly` instead, and under
it the conversation's clock and the music keep running.

Pause Anytime pauses only a world whose `Pauser` is None. If `Pauser` is still None after the call, the pause is
refused with a warning. A resume first drops what it kept of the pause, so a fault inside cannot leave it armed. Then
it calls `Pause` again through the controller that paused. If that controller is gone, or `Pauser` is still set after
the call (another pause, such as the Steam overlay), the world stays paused with a warning. The console's `pause`
command then resumes it.

`Validate` runs every frame and on every press of a watched key. It ends our pause when the paused WorldInfo is gone (a
map change or a load), or when `Pauser` became None without us. That happens through the game, a load, the console's
`pause`, or the Steam overlay closing (`ClearPause` drops every `CanUnpause` that agrees). If the mode left
Conversation and Cinematic outside our photo session, `Validate` resumes the world. No mode at all (no mode manager to
read) is not a change.

### The audio hold and the Steam overlay

The pause plays the game's menu sound `InGameGuiEnter`, and a resume plays `InGameGuiExit`. In LE2 and LE3 these are
Wwise events: the first starts with a Pause All of the game's audio, and the second with a Resume All
(`Game::PauseSoundHoldsAudio`). So in those two games every end of the pause must post `InGameGuiExit`. LE1's sounds
hold no audio, and LE1 plays `InGameGuiExit` only when a key's resume really unpaused the world.

A voice line that the Pause All still holds when the world runs again is lost, and a Resume All posted after that does
not bring it back. A test in LE2, with the Steam overlay ending the pause, showed it. `InGameGuiExit` posted 0.7 s
after the world resumed lost the line. Posted during the pause, it kept it.

Closing the Steam overlay ends our pause with no key that Pause Anytime sees. So while the overlay is open
(`PlayerController.bIsExternalUIOpen`) and the world is still held, `Validate` posts `InGameGuiExit` at once.
`InGameGuiExit` is posted once per pause, at the first of these:

- a resume of ours (key, mode change or fault), in the frame the world resumes;
- `Validate` with the overlay open;
- `Validate` finding the pause already over, which gives back only the rest of the audio.

When it does not play (no controller or GUI manager, as after a load), the hold stays, and the next `Validate` tries
again, 3 times at most. Then the log warns `game audio still held`, and the audio waits for a game menu to play its own
sound. The console's `pause` ends our pause with no key of ours too, and nothing hears of it before the next frame's
`Validate`, so a voice line the hold still held may be lost there.

Posting it as each pause starts would also keep the line, but the pause would not be silent: ambient sound went on
under it (in LE2, a peak of 0.018 against 0.000).

## Movie pause

`Bink.cpp` hooks the Bink exports in `bink2w64_original.dll`, the module behind the loader's `bink2w64.dll` proxy.
`BinkPause` is called, not hooked. Without `BinkPause`, `BinkClose` or `BinkWait` the movie pause is off, and the world
pause still works.

A movie takes the pause keys while it was opened from a file, has called `BinkWait` in the last 0.5 s, and the HUD has
not drawn for 0.25 s. In LE2 and LE3 its sound must also be known. LE1 and LE2 play their loading screens from memory.
A movie on a texture in a level never takes the keys, because the HUD draws there. Only the newest such movie is
paused.

The thread that plays the movie reads Esc and Space in each `BinkWait`, with `GetAsyncKeyState`. A press is the key
going down while a game window is in front, at least 150 ms after the last one. One Esc press toggles: it resumes
what Pause Anytime paused, or asks for a pause of the newest fullscreen movie. Space only resumes. An Esc press read in
the same poll as a Space press that resumes is part of that resume. Neither key counts while the Steam overlay or the
console is open ([Keys](#keys)).

This thread reads no game object. It reads only what the game thread shares with it: the HUD's time, the requests of
`Bink::Toggle` and `Bink::RequestResume`, a copy of the movie latch, and whether the Steam overlay or the console is
open, with the time of the game thread's latest tick. LE3's log lines also read two movie player fields there, under a
guard.

Start cannot be read there (XInput goes through the game, and Steam Input may stand in for it). So Start toggles on the
game thread, through `Bink::Toggle`. In LE1 the engine's movie input takes Start's press, so its first repeat or its
release toggles. Such an event pauses no movie that opened after the press could have been made, 0.2 s before it: that
press may have skipped the movie before. In LE2 and LE3 the movie input hook lets the press through, and it toggles at
once.

The movie's thread applies the pause with `BinkPause(movie, 1)` at its `BinkWait` and only there, since Bink's calls on
one movie belong on one thread. `BinkPause` only makes `BinkWait` answer "wait" and stops the Bink audio. The games'
Bink (2022.05) hangs a player that pauses and then stops calling `BinkWait`, a bug RAD fixed in 2025.05. So a movie
that has not called `BinkWait` lately is never paused. A resume is applied at its next Bink call of any kind, on the
thread of its `BinkWait`. A movie that closes while `BinkPause` holds it is resumed right before `BinkClose`, in case
the close is such a stop. That call comes from the thread that closes the movie, the one exception to the rule, and no
other call on the movie can overlap its close.

In LE2 and LE3 the movie's Wwise event is paused first, and the picture only once Wwise has queued that pause: a pause
Wwise refuses leaves both playing, and the next press tries again. A resume runs the other way, `BinkPause(movie, 0)`
first, then the event. Wwise is taken to count pauses (`MovieSound.cpp` says why), so no pause of ours goes to an event
that one of ours still holds, and no resume goes without one. A resume Wwise refuses leaves the event paused: it is sent
again up to three times, 100 ms apart, while the picture plays. A movie that closes with its event still paused by us
sends it a resume, once, since nothing else would. Each answer from Wwise other than `AK_Success` is a warning in the
log.

The movie pause ends:

- on Esc, Start, Space or A;
- when the movie closes;
- on a `SkipMovie` that no guarded key sent;
- once the HUD has drawn for 1 s;
- after a fault in the per-frame checks.

If a movie makes no Bink call for 2 s under the pause, its pause is dropped: Esc and Start go back to the game, and the
resume is applied if it calls Bink again. A pause not applied yet counts only `BinkWait`, since only `BinkWait` applies
it. From the 2 s mark the pause reads as over everywhere (the pause screen, the movie input hook, the game thread), and
the next Esc, Start or movie poll drops it. A movie thread that only blocked that long is resumed the same way. A
silent game thread (a load) does not end the pause, since Esc is read on the movie's thread.

One press can do nothing when the two threads judge it apart: a second Esc within 150 ms, a tap shorter than the gap
between two `BinkWait` polls, or a press at the HUD's 250 ms or `BinkWait`'s 500 ms edge. Acting on both sides could
make one press two toggles, so it is left so, and the player presses again.

### LE2 and LE3: movie sound and movie input

Most LE2 and LE3 movies have no Bink audio. Their sound is a Wwise event, which plays beside the picture on a clock of
its own. A fullscreen movie takes the pause keys only when that sound can stop with the picture. That means the Kismet
movie action (`BioSeqAct_MovieBink`) latched last plays this movie, names an event, and runs nothing beside it that may
make a sound. `Game::MovieActionOf` in `KismetMovie.hpp` checks this. A movie on the Bink-audio list
(`BinkAudioMovies` in `LE<n>.hpp`) counts too, unless its latched action runs something beside it.

The latch is written at `Activated`, before the original runs, so the movie it starts finds it in place. A movie that a
Matinee track plays (`SFXInterpTrackMovieBink`) is never latched and stays the game's.

`MovieSound.cpp` pauses that event on all game objects, with no fade, through
`AK::SoundEngine::ExecuteActionOnEvent`. It finds that function by its mangled name among the exe's exports, which are
readable on disk. Wwise queues the call and runs no `ProcessEvent`, so the movie's thread can make it. Without that
export, no movie has a known sound.

During a fullscreen movie, a key press reaches the movie player's native `InputKey` before `Console.InputKey`. On one
of its keys, the player stops a movie that the game lets you skip. `MovieInput.cpp` hooks that native once its first
28 bytes match. The hook keeps these presses from it: Esc and Start while a movie owns the pause keys, every press under
the movie pause, and a Space press that the movie's thread just resumed on. Those presses go on to `Console.InputKey`
and skip nothing.

In LE2 the hook hands Space and X to the movie input as Enter, so they skip what Esc would skip. In LE3 they are its
keys already. LE1 has no such hook: its movie input takes Esc's press, and only the release reaches the console.

## Photo mode under the pause

The game opens photo mode only from its pause menu, which it refuses in a conversation or a cutscene. Its real entry,
`ShowPhotoMode`, does not check for this, but called alone there it fails. Photo mode's priority is below
Conversation's, the conversation panel takes the sticks, and nothing ticks under the world pause.

### Entering

The pause screen goes first, so photo mode's first screenshot cannot catch it. Each step of the entry sets a bit. So a
stop halfway gives back only what was done, and a refused entry brings the pause screen back in the same frame.

1. The controller's running timers are held, or the conversation's `SkipNode` timer would move it on.
2. The conversation panel is set aside: no input (A or Enter would pick a reply) and hidden (wheel and subtitle).
3. `SFXGameModePhoto.Priority` goes to 3 (`ModePriority_Menus`), above Conversation (2) and Cinematic (0).
4. A cutscene's director camera is swapped for the controller, a pending blend is held, a frozen camera is unfrozen, and
   the photo tether is widened when the shot is far from the pawn.
5. `PlayerInput.bUsingGamepad` is set as the key would have set it: Y the pad, F7 the keyboard.
6. `ShowPhotoMode` runs, and the mode must then be Photo.
7. The ignore counters are zeroed and `bAlwaysTick` is set, so `PlayerTick` runs under the pause.
8. The camera: `LastTransitionTime` is pushed back (the game's clock stands still), `UpdateCamera(0)` switches to the
   photo camera, a 0.001 s step ends its "instant" transition, and the photo cameras get the shot's field of view.

Each frame sets again what the game may have changed, such as LE3's conversation movie showing itself or turning its
input on. It holds the controller's new timers too, so a timer that other script sets during the session runs only
once the world resumes. The camera follows each `PlayerTick` on our controller in the same frame.

The engine drops input while `Pauser` is set, and the GUI manager's tick stops. While the session runs, some natives
run with `Pauser` cleared, and it is put back as they return: `BioPlayerInput::InputKey` and `UPlayerInput::InputAxis`
on our controller's `PlayerInput`, and `UBioSFManager::Tick` on our GUI manager (LE1, LE2). The game thread runs them
between the world's ticks, so nothing else sees the world unpaused. A `SetPause` with another delegate inside such a
call would add a pause that putting `Pauser` back does not undo. None of these natives is known to call it.

LE3's GFx movies advance on `WorldInfo.TimeSeconds`, which stands still, so `GfxClock` feeds their tick the real time.

In LE1 the audio device starts no sound under the pause unless its component is `bNonRealtime`. Two hooks mark the
components of our GUI manager's `PlayGuiSound`, for photo mode and for the pause screen's own sound. LE2 and LE3 hook
no GUI sound (LE2's are Wwise events, and LE3's go through `SFXGUIInteraction`).

### Leaving

Y, F7, Esc or Start leaves, and the world stays paused. The game's exit runs first (`HidePhotoMode`, then
`DisableMode(Photo)` if needed). Its `Deactivated` resets the static console binds to their defaults, so the binds the
session started with are set back. Then every field changed on entry is given back, and one camera update returns to
the conversation's or director's camera. The pause screen returns 3 frames later, since the render thread presents a
frame or two behind.

The session also ends when the world pause ends without us, when the world or the controller is gone, when the mode
leaves Photo, or on a fault. While the world is being torn down, the session writes fields only on objects that are
still alive, and calls nothing. If the game's own exit faults, its photo mode may stay current. `Validate` then
resumes the world, and the game's Esc leaves photo mode through its pause menu. Photo mode opened from the game's own
pause menu stays the game's.

## Pause screen

The game thread publishes what the screen shows through atomics (`Overlay::Publish` and the `Use...` calls). The render
thread draws it from the hooked `Present`. It touches no game object there, only the D3D11 views of two game textures.

The screen shows under our world pause while the HUD has drawn in the last 250 ms, so a flag left over from a load shows
nothing. It shows under our movie pause while the HUD is silent. Under our world pause it goes at once, with no fade,
during our photo session, for 3 frames after it, and while the console is open. The HUD's frames ask for that, so over a
movie pause, where the HUD is silent, a request left from before the movie is ignored. Photo mode's key shows only where
photo mode answers (its input and GUI tick hooks are installed), for the controller that paused, in Conversation or
Cinematic, and never over a movie.

### Drawing

`Overlay::Install` reads the swap chain's methods from a throwaway device on a hidden window, and hooks `ResizeBuffers`,
`ResizeBuffers1` (where DXGI has it), `Present` and `Present1`. It starts on the first D3D11 swap chain that presents to
the game's window: a window of the game's process made by its main thread (the thread made first), or by the game thread
once `Pause.cpp` knows it. A window that another ASI or a tool makes on a thread of its own is left alone. If that rule
refuses every swap chain for 5 s once the game thread is known, the next one that presents to the foreground window is
taken anyway, with a warning. The overlay starts with no `imgui.ini` or ImGui log, and compiles its shaders at that
first frame. It moves to another swap chain (after a display mode change) only once ours has been silent for 500 ms, and
only to one on the window it draws on or on a window of the game's. An overlay taken anyway may also move, once its
window is gone, to one on the foreground window, never to one in the background. While another swap chain presents and
ours has been silent for 250 ms, the Canvas banner shows instead, until ours presents again or the other one is taken. A
present that comes back `DXGI_ERROR_WAS_STILL_DRAWING` showed nothing, so the present that retries it is not drawn on
again. `d3dcompiler_47.dll` is delay-loaded and bound to the copy in System32, never to one next to the game.

The overlay's work runs on whichever thread calls it: the game's presents, the `BinkWait` hook's present for a paused
movie ([below](#a-paused-movies-frame)), and a resize. Each call takes a gate first, so that one thread at a time uses
the overlay's state, ImGui, the blur and the device's immediate context, and gives it back on every way out. A present
never waits for the gate: one that finds it held goes out without the pause screen, and a present of another swap chain
while ours presents goes out without taking it. A resize of the swap chain we draw on waits for the gate and keeps it
until the original returns, since our views of its back buffer must be let go of first. The `BinkWait` hook takes no
gate while a resize waits. The `BinkWait` hook's present and that resize keep the gate across their call to DXGI, a call
we don't control, and mark it while they are in there. A holder marked for 250 ms is stuck there: a resize of the swap
chain we draw on then takes the gate from it, with a warning, and a resize of any other swap chain goes on without it.
The holder, back from DXGI, sees the gate gone and touches nothing more. A holder in the overlay's own work is waited
for, since that work ends. Only after 2 s, when something we can't see holds it, does a resize take the gate from it all
the same, with an error in the log, so that the game never hangs.

Each game has its own look (`Game::Look`), with colours and sizes read from its menus. The layout is the same in all
three, set for 1080 lines and scaled by the height. The frame is blurred, half as bright, 30 % desaturated and
vignetted, with a slight tint. The title sits on a band in the game's menu colours. The bottom left shows each key left
of its label: Resume (A or the space bar), then Photo mode (Y or F7).

`Blur.cpp` runs a dual Kawase filter: 4 levels down at 1080 lines, one more per doubling of the height (1 to 6), then
up. A composite pass then darkens, desaturates, vignettes and dithers. It handles 8-bit RGBA, BGRA and BGRX (UNORM or
sRGB), `R10G10B10A2` and `R16G16B16A16_FLOAT`, on linear values for sRGB views and float targets. It sets back every
piece of pipeline state it changes.

### Words, fonts and buttons

The title is always Pause Anytime's own word. The labels come from the game's string table (`GetSimpleString`, up to
3 frames), and each is used only if its font has every letter. Pause Anytime's own words follow the launcher's
language switch (`-OVERRIDELANGUAGE=` in LE1 and LE2, `-language=` in LE3), in seven languages. Japanese gets English,
since Exo 2 has no kana.

The text is set in the game's two GUI fonts, a light one and a bold one. `GameFont.cpp` reads each from a GFx movie
(its `DefineFont3` tag) and writes a TrueType file in memory, never on disk, with only the letters the screen needs. A
mod can replace the movie, so every read is checked and every size capped. The movie may be Scaleform's GFX or a plain
uncompressed SWF (`FWS`), as Myriad Pro Begone saves LE1's. Compressed movies are refused. ImGui has no kerning, so
each line is set letter by letter with the font's kerning pairs. Each line's capitals are centred using the height of
the font's own H, so a font mod that redraws the capitals stays centred. The look's fixed height is used only if the
font has no H from 0.5 to 1 EM. Both fonts must load and draw the words, or Exo 2 stays for all of the text.

The A and Y prompts use the view that the renderer holds of the game's own textures. The game thread follows
`UTexture.Resource` to it (`Game::TextureView`). The render thread takes it only if its method table and the device
it reports match those of a view the overlay makes itself. A layer such as ReShade 6.8 hands the overlay its own device
through the swap chain, while every view reports the device beneath, so the game's views are compared with ours, not
with the swap chain's device. Both views must pass, or neither is used and drawn discs take their place.

### A paused movie's frame

A paused movie presents nothing: its thread only polls `BinkWait`. So while a pausable movie is open and the HUD has
been silent for 125 ms, each frame is copied before anything is drawn on it. The swap chain is referenced in one of
those presents and held until the game presents with no such movie, since the pause can come at any frame and the
movie then stops presenting. While the movie pause holds and the game has presented nothing for 100 ms, the `BinkWait`
hook presents that copy with the pause screen over it, 60 times a second at most.

This works only on the thread that presents the game's frames. In all three games that is the movie's thread, as their
logs show, but nothing in the engine promises it: elsewhere the pause screen shows only as the movie pauses. The hook
holds the gate from the copy to the end of its present, and skips its frame while a resize waits. After a resize the
copy no longer fits, and nothing more is presented.

### Fallbacks

- Exo 2 (Latin, Latin Extended-A, Cyrillic), compiled into the ASI, stands in for the game's fonts, and ImGui's default
  font stands in for Exo 2.
- Without the blur (a format it does not handle, a shader that fails), the backdrop is black at 60 %.
- Without the overlay (a hook that fails, no `d3dcompiler_47.dll` in System32, 10 frames with no view on the back
  buffer, a fault), before it has started on the game's swap chain, or while a new swap chain waits to be taken,
  `Pause.cpp` draws an English banner on the HUD's Canvas: `PAUSE`, then `Esc: resume` and, where photo mode opens,
  `F7: photo mode`. It names Start and Y instead when Start paused or a gamepad is in use. It shows under our world
  pause outside photo mode, never over a movie.

## Crash guards

Each part runs under a `__try` guard that takes access violations only, and other exceptions go on up to the game.
Each fault logs the module and offset where it was raised, and whose code that is. A fault in the game's code can
still come from a stale pointer of ours. The present that the `BinkWait` hook makes for a paused movie runs under the
pause screen's guard, also for a fault in DXGI, the driver or another overlay, since that present runs only because of
Pause Anytime. A fault below a present the game makes goes on up to the game.

| Where the fault happens | What Pause Anytime does |
|---|---|
| Key handling | Passes the key to the game, or takes it when its press already paused or resumed the world; leaves photo mode |
| Reading the Steam overlay's and the console's state | Off for the session; the movie's thread reads Esc and Space there too |
| Per-frame checks | Leaves photo mode, resumes and drops the world pause, resumes the movie |
| The resume after that fault | Logs it; if the world stays paused, the console's `pause` resumes it |
| The photo camera | Leaves photo mode |
| The Kismet movie latch | Latches or clears nothing for that call |
| Reading the game's words, icons or fonts | Stops trying, keeps its own words, discs or Exo 2 |
| The pause screen | Off for the session; the banner takes over |
| The banner | Off for the session |

After 3 faults the per-frame checks stay off for the session. Esc and Start still pause and resume, but Y and F7 no
longer open photo mode, since nothing would watch the session. The pause screen and the banner then show a pause only
while its world exists and is paused, and without photo mode's key. The HUD drawing again no longer lifts a movie
pause.

## Engine facts it relies on

- A Bink movie plays on under both `WorldInfo.Pauser` and `bPlayersOnly`.
- The Steam overlay sets `Pauser` too (seen in LE1 and LE2; in LE3 an Esc under it logged "still paused by").
- `Console.InputKey` sees every key that gets past the engine, also under the world pause. Its TRUE return tells the
  viewport the key was handled.
- `BioHUD.PostRender` runs every frame in play, hitches included, and not over a fullscreen movie.
- `SFXGameModeMovie` shares `ModePriority_Menus` with GUI and Galaxy, so `CurrentMode` may stay Galaxy during the galaxy
  map's travel movie. The movie code does not use the mode.
- Under the world pause, the engine drops input, stops the GUI tick, and ticks only a controller with `bAlwaysTick`.

## Open questions

- Does a TRUE return from `Console.InputKey` keep a key from PlayerInput's binds? A log line
  `SkipCinematic on ...: dropped, our Esc` would show that the bind still fired.
- Another build of the exe: the core hook and GEngine have no byte check, so the effect of a game update is not known.
- Other ASIs and overlays (ReShade, the Steam overlay): which hook sees a key first, or draws on top, depends on the
  loader's order and is not tested.
- A Matinee-track movie in a conversation or a cutscene: if the mode stays Conversation or Cinematic, Esc pauses the
  world while the movie plays on, with no pause screen. Not tested.
- X under a movie pause: the movie mode binds X to `SkipMovie`, which passes the guard and resumes the movie. In LE3
  that script also skips a skippable cutscene. Not tested.
- HDR: the text and band use the same 8-bit colours on every back buffer format, and nothing reads the swap chain's
  colour space.
