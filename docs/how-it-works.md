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
| `src/CMakeLists.txt` | One target per game, Dear ImGui, the delay-loaded `d3dcompiler_47.dll` |
| `src/Api.hpp` | What the parts ask of each other; no game class |
| `src/Pause.hpp`, `Pause.cpp` | Game thread: the `ProcessEvent` hook, keys, world pause, skip guard, movie latch, what the pause screen shows, fallback banner |
| `src/Engine.hpp`, `Engine.cpp` | Calls into the game (`CallGame`), object checks (`Alive`, `Kept`), the fault guard |
| `src/Photo.hpp`, `Photo.cpp` | Photo mode under the world pause |
| `src/PhotoGui.hpp`, `PhotoGuiSF.cpp`, `PhotoGuiGFx.cpp` | Photo mode's GUI side: ScaleForm (LE1, LE2) or GFx (LE3), picked by `PAUSEANYTIME_PHOTO_GFX` |
| `src/Bink.cpp` | The movie pause, on the movie's thread |
| `src/MovieSound.cpp` | LE2, LE3: a movie's Wwise event |
| `src/MovieInput.cpp` | LE2, LE3: the engine's movie input |
| `src/Overlay.cpp`, `Blur.hpp`, `Blur.cpp` | The pause screen and its blur, on the render thread |
| `src/GameFont.hpp`, `GameFont.cpp` | A game font from a GFx movie to TrueType in memory; plain C++ |
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

A part that cannot hook logs what it loses and stays off. Only the core checks are fatal (GEngine, `ProcessEvent` and
its hook, the SDK's globals): a failed one logs `failed to resolve <name>` and aborts the game. `ProcessEvent` and
GEngine are at fixed RVAs of the pinned SDK, and nothing checks their bytes. Every other native has its first bytes
compared before it is hooked, and on another build the log says `not found at RVA ... (another build of the game?)`.
The RVAs come from a memory dump of the running game, since the exe's code is encrypted on disk.

## The ProcessEvent hook

One hook on `UObject::ProcessEvent` sees every UnrealScript call. Calls that Pause Anytime makes itself (`InOwnCall`)
go straight to the game. On the game thread, each call notes the time, and a gap over 250 ms marks a stall. Then:

- LE2, LE3: `Activated` and `Deactivated` go to the Kismet movie latch, before the original.
- Before the original: `InputKey` on a `Console` (the keys), `SkipMovie`, `SkipCinematic` and `GuiKey` on an
  `SFXGameModeBase` (the skip guard), and photo mode's menu bind while our photo session runs (dropped). A key taken or
  a call dropped skips the original.
- After it: `PlayerTick` on our photo session's controller (the photo camera), and `BioHUD.PostRender`.

After each `BioHUD.PostRender` the hook notes the HUD's time, which the movie code reads. Then, under one guard, it:

1. lifts a movie pause once the HUD has drawn for 1 s;
2. runs `Validate` and `PhotoMode::Frame`;
3. publishes what the pause screen shows;
4. reads the game's words, icons and fonts (3 tries each, each guarded).

`CallGame` zeroes a function's `iNative` for the call, so `ProcessEvent` runs natives too, and puts it back in a
`__finally`. A game object kept across frames (`Kept<T>`) holds its object index and is checked before each use,
since a map change or a load replaces it.

## Keys

Pause Anytime watches six keys in `Console.InputKey`: `Escape`, `XboxTypeS_Start`, `XboxTypeS_Y`, `F7`, `SpaceBar` and
`XboxTypeS_A`. A key it takes gets `ReturnValue` TRUE, and the game's `InputKey` is skipped. A press it acts on is
taken with its repeats and its release. The controller is GEngine's first `LocalPlayer`. In the open console (`Typing`
or `Open` state) the keys are the console's, but the movie's thread has no such check.

A press of Esc or Start runs `Validate`, then the first of these that applies:

1. A fullscreen movie owns the pause keys (`Bink::OwnsKeys`). Esc is taken without a toggle, since the movie's thread
   saw the same press. Start toggles the movie pause through `Bink::Toggle`.
2. Our photo session runs: it ends, and the world stays paused.
3. Our world pause holds: resume.
4. The mode is Conversation or Cinematic and `Pauser` is None: pause.

Otherwise the key is the game's. During a fullscreen movie, every Esc event that reaches the console is taken.

A pad button repeats after 0.2 s, then every 0.1 s. So a Start press with no event for 500 ms counts as over (its
release was lost), unless the game thread stalled.

Space and A only resume. Under the movie pause any of their events resumes. Under the world pause only a press does,
and not in photo mode, where Space takes the picture. Y and F7 open photo mode under our world pause, for the
controller that paused, in Conversation or Cinematic, and never during a fullscreen movie. In our photo mode they leave
it. Any event of Escape, F7 or SpaceBar switches the prompts to the keyboard, and any event of Start, Y or A to the
gamepad.

### Skip guard

The guard is armed while a press of Esc, Start, Space or A that Pause Anytime took is down (2 s at most), and for
250 ms after its release. It drops `SkipMovie`, `SkipCinematic`, and the `GuiKey` calls with the Escape and Start
events of the always-on binds (ids in `LE<n>.hpp`). An Esc or Space press that only the movie's thread saw arms it
too. Y and F7 do not arm it.

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
map change or a load), or when `Pauser` became None without us. That happens through the game, a load, or the Steam
overlay closing (`ClearPause` drops every `CanUnpause` that agrees). If the mode left Conversation and Cinematic outside
our photo session, `Validate` resumes the world.

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
what Pause Anytime paused, or asks for a pause of the newest fullscreen movie. Space only resumes.

This thread reads no game object. It reads only what the game thread shares with it: the HUD's time, the requests of
`Bink::Toggle` and `Bink::RequestResume`, and a copy of the movie latch. LE3's log lines also read two movie player
fields there, under a guard.

Start cannot be read there (XInput goes through the game, and Steam Input may stand in for it). So Start toggles on the
game thread, through `Bink::Toggle`. In LE1 the engine's movie input takes Start's press, so its first repeat or its
release toggles. In LE2 and LE3 the movie input hook lets the press through, and it toggles at once.

The movie's thread applies the pause with `BinkPause(movie, 1)` at its `BinkWait` and only there, since Bink's calls on
one movie belong on one thread. `BinkPause` only makes `BinkWait` answer "wait" and stops the Bink audio. The games'
Bink (2022.05) hangs a player that pauses and then stops calling `BinkWait`, a bug RAD fixed in 2025.05. So a movie
that has not called `BinkWait` lately is never paused. A resume is applied at its next Bink call of any kind. In LE2
and LE3 the movie's Wwise event is paused right after `BinkPause` and resumed with it.

The movie pause ends:

- on Esc, Start, Space or A;
- when the movie closes;
- on a `SkipMovie` that no guarded key sent;
- once the HUD has drawn for 1 s;
- after a fault in the per-frame checks.

If a movie makes no Bink call for 2 s under the pause, its pause is dropped: Esc and Start go back to the game, and the
resume is applied if it calls Bink again. A silent game thread (a load) does not end the pause, since Esc is read on
the movie's thread.

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

Each frame sets again what the game may have changed, such as LE3's conversation movie showing itself. The camera
follows each `PlayerTick` on our controller in the same frame.

The engine drops input while `Pauser` is set, and the GUI manager's tick stops. While the session runs, some natives
run with `Pauser` cleared, and it is put back as they return: `BioPlayerInput::InputKey` and `UPlayerInput::InputAxis`
on our controller's `PlayerInput`, and `UBioSFManager::Tick` on our GUI manager (LE1, LE2). The game thread runs them
between the world's ticks, so nothing else sees the world unpaused.

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
still alive, and calls nothing. Photo mode opened from the game's own pause menu stays the game's.

## Pause screen

The game thread publishes what the screen shows through atomics (`Overlay::Publish` and the `Use...` calls). The render
thread draws it from the hooked `Present`. It touches no game object there, only the D3D11 views of two game textures.

The screen shows under our world pause while the HUD has drawn in the last 250 ms, so a flag left over from a load
shows nothing. It shows under our movie pause while the HUD is silent. It goes at once, with no fade, during our photo
session, for 3 frames after it, and while the console is open. Photo mode's key shows only where photo mode answers
(its input and GUI tick hooks are installed), for the controller that paused, in Conversation or Cinematic, and never
over a movie.

### Drawing

`Overlay::Install` reads the swap chain's methods from a throwaway device on a hidden window, and hooks
`ResizeBuffers`, `Present` and `Present1`. It starts on the first swap chain that presents, with no `imgui.ini` or ImGui
log, and compiles its shaders at the game's first frame. It moves to another swap chain (after a display mode change)
only once ours has been silent for 500 ms. `d3dcompiler_47.dll` is delay-loaded and bound to the copy in System32,
never to one next to the game.

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
mod can replace the movie, so every read is checked and every size capped. ImGui has no kerning, so each line is set
letter by letter with the font's kerning pairs. Both fonts must load and draw the words, or Exo 2 stays for all of the
text.

The A and Y prompts use the view that the renderer holds of the game's own textures. The game thread follows
`UTexture.Resource` to it (`Game::TextureView`). The render thread takes it only if its method table matches a view
the overlay makes on the same device. Both views must pass, or neither is used and drawn discs take their place.

### A paused movie's frame

A paused movie presents nothing: its thread only polls `BinkWait`. So while a pausable movie is open and the HUD has
been silent for 125 ms, each frame is copied before anything is drawn on it. While the movie pause holds and the game
has presented nothing for 100 ms, the `BinkWait` hook presents that copy with the pause screen over it, 60 times a
second at most.

This works only on the thread that presents the game's frames, which in LE1 is the movie's thread. Elsewhere the pause
screen shows only as the movie pauses. After a resize the copy no longer fits, and nothing more is presented.

### Fallbacks

- Exo 2 (Latin, Latin Extended-A, Cyrillic), compiled into the ASI, stands in for the game's fonts, and ImGui's default
  font stands in for Exo 2.
- Without the blur (a format it does not handle, a shader that fails), the backdrop is black at 60 %.
- Without the overlay (a hook that fails, no `d3dcompiler_47.dll` in System32, 10 frames with no view on the back
  buffer, a fault), `Pause.cpp` draws an English banner on the HUD's Canvas: `PAUSE`, then `Esc: resume` and
  `F7: photo mode`, or Start and Y when Start paused or a gamepad is in use. It shows under our world pause outside
  photo mode, never over a movie.

## Crash guards

Each part runs under a `__try` guard that takes access violations only, and other exceptions go on up to the game.
Each fault logs the module and offset where it was raised, and whose code that is. A fault in the game's code can
still come from a stale pointer of ours.

| Where the fault happens | What Pause Anytime does |
|---|---|
| Key handling | Passes the key to the game, leaves photo mode |
| Per-frame checks | Leaves photo mode, resumes and drops the world pause, resumes the movie |
| The resume after that fault | Logs it; if the world stays paused, the console's `pause` resumes it |
| The photo camera | Leaves photo mode |
| The Kismet movie latch | Latches or clears nothing for that call |
| Reading the game's words, icons or fonts | Stops trying, keeps its own words, discs or Exo 2 |
| The pause screen | Off for the session; the banner takes over |
| The banner | Off for the session |

After 3 faults the per-frame checks stay off for the session. Esc and Start still pause and resume. The pause screen
then shows a pause only while its world exists and is paused, and without photo mode's key. The HUD drawing again no
longer lifts a movie pause.

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
- Two copies of the ASI under one plugin name: the code has no check for it. If the loader refuses the second hook of
  the same name, the second copy aborts the game at start (`failed to resolve orig`). Not run.
- Other ASIs and overlays (ReShade, the Steam overlay): which hook sees a key first, or draws on top, depends on the
  loader's order and is not tested.
- A Matinee-track movie in a conversation or a cutscene: if the mode stays Conversation or Cinematic, Esc pauses the
  world while the movie plays on, with no pause screen. Not tested.
- X under a movie pause: the movie mode binds X to `SkipMovie`, which passes the guard and resumes the movie. In LE3
  that script also skips a skippable cutscene. Not tested.
- HDR: the text and band use the same 8-bit colours on every back buffer format, and nothing reads the swap chain's
  colour space.
