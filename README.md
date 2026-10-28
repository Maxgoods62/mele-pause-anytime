# mele-pause

`MelePause.asi`, our own ASI for Mass Effect 1 Legendary Edition: Esc, or a gamepad's Start, pauses a conversation, an
in-engine cutscene or a Bink movie, and pressing it again resumes it. Everywhere else both keys do what they always did. It stands alone: it needs neither
MeleBridge nor anything else of this repo, and runs side by side with MeleBridge (both hook `ProcessEvent` through the ASI
loader's shared hook manager).

## What Esc and Start do

| Where | Vanilla Esc / Start | With MelePause (Esc or Start) |
|---|---|---|
| Conversation (game mode 5) | nothing | Pauses everything: picture, voice-over, music, lips. Esc or Start again resumes at the same word, in sync. A "PAUSE" banner shows at the top, with "Start: resume" when Start paused it or the game uses the gamepad, else "Esc: resume". Esc and Start tested. |
| In-engine cutscene (mode 6, `GameMode_Cinematic`) | Esc: `SkipCinematic`, which only plays an error sound (`bEnableCinematicSkip` is off); Start: nothing | Same pause as a conversation. Esc and Start tested. |
| Fullscreen Bink movie the engine doesn't skip on Esc: opened from a file, while the HUD draws nothing (the galaxy map's travel movie) | the travel movie: nothing, it plays to its end (Esc and Start tested) | Pauses the movie (picture and sound). The key again resumes it. No banner. Esc (v2) and Start (v3) tested; Start reacts on its first repeat or its release, not its press (see [How it works](#how-it-works)). |
| Fullscreen Bink movie the engine skips on Esc (a skippable one, in UE3's code) | skipped | still skipped: the engine's movie input acts on the press before us |
| Loading-screen movie (played from memory) | nothing | unchanged |
| Anywhere else (gameplay, menus, GUI, galaxy map, console open) | pause menu, close, ... | unchanged (tested in gameplay and the pause menu) |

- **Keys:** Esc on the keyboard, and Start on a gamepad: the game's `XboxTypeS_Start`, which an Xbox pad sends and a
  PlayStation pad too through Steam Input or DS4Windows (Options, and the touchpad click by default). Space still skips a
  conversation line and a movie, paused or not (skipping a paused movie resumes it first), except while a press of Esc or
  Start we took is held down and a quarter second after its release: the skips Esc is bound to are held back then,
  whichever key sent them.
- **Not handled:** Loading-screen movies. Esc while the console is open (it closes the console, as in vanilla). During a
  movie: Esc while another program's window is in the foreground, and a key in the first quarter second of the movie (the
  HUD's silence isn't long enough yet).
- **Changed:** while a fullscreen movie plays, the Esc and Start events that get past the engine's movie input (in the
  travel movie: everything but the press) no longer reach the game's bindings, outside the console. Space still skips. And
  any Esc pressed with one of the game's windows in the foreground toggles the movie pause, also one that closes the console
  or the Steam overlay: Esc is read from the keyboard, not from the game. Start is read through the game.
- **The pause is the game's own:** the world pause the `pause` console command sets (`PlayerController.Pause` ->
  `ServerPause` -> `GameInfo.SetPause`: `WorldInfo.Pauser`). Not `WorldInfo.bPlayersOnly` (the pause menu's and photo
  mode's): the conversation's clock, `TimeSeconds` and the music keep running under it.

## Build and install

```bash
./build.sh              # build, stage in D:\MELE\tools\mele-pause
./build.sh --install    # also copy it to Game\ME1\Binaries\Win64\ASI (the game must be closed)
./build.sh --uninstall  # remove it from there (the game must be closed)
```

- **Toolchain:** Visual Studio Build Tools 2026, like [mele-bridge](../mele-bridge/README.md#build-and-install).
- **Where it builds:** in mele-bridge's checkout of [ME3Tweaks/LExASIs](https://github.com/ME3Tweaks/LExASIs) at
  `D:\MELE\asi-build\LExASIs`, pinned to `aaad03a` (cloned if missing). `src/` is copied in as the `MelePause` project and
  only the `MelePause-LE1` target is built, with that tree's flags (`/W4 /WX`).
  - The tree is shared with mele-bridge. Both scripts take one lock (`D:\MELE\asi-build\.lexasis-build.lock`), so a second
    build waits for the first.
  - The project line it adds to the tree's `CMakeLists.txt` only counts while `LExASIs\MelePause` exists: deleting that
    folder takes the project out of the tree.
- **Install rule:** `MelePause.asi` in the ASI folder is the only file this tool writes inside the game install, and only
  while `MassEffect1.exe` is closed. The game check counts a failed `tasklist` as "running".
- **Log:** `Binaries\Win64\Logs\MelePause.log`, rewritten at each start.
- **Sharing:** copy `MelePause.asi` into `Binaries\Win64\ASI`. It needs the ASI loader (the `bink2w64.dll` proxy) and a
  current [Microsoft Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe), 14.50 or newer: it is
  built with VS 2026 against the shared runtime (`MSVCP140.dll`, `VCRUNTIME140.dll`), and an older `msvcp140.dll` can
  crash it at its first lock.

## How it works

| Hook | What it does |
|---|---|
| `ProcessEvent`: `Console.InputKey` (`Pause.cpp`) | Every key that gets past the engine reaches it, also while the world is paused. On an Esc or Start press it reads the local player's controller (GEngine's first `LocalPlayer`), its `GameModeManager.CurrentMode` and `WorldInfo.Pauser`, then pauses or resumes the world, or lets the key through. A press it acted on is consumed (`ReturnValue` TRUE, the original not called), and so are that key's repeats and its release. While a fullscreen movie plays (or our movie pause is on), every Esc event is consumed without acting, except in the open console: the movie's thread toggles the movie pause. Start's press never comes then: the first repeat or release of a Start press it didn't see toggles the movie pause (`Bink::Toggle`), and the rest of that press is consumed. A Start press it saw and left to the game stays the game's to its release, also if a movie starts under it. |
| `ProcessEvent`: `SkipMovie`, `SkipCinematic`, `GuiKey` on an `SFXGameMode` | Dropped while our Esc or Start is down (from the press we took to its release, 2 s at most, plus 250 ms after the release), whether `Console.InputKey` or the movie's thread took it. `GuiKey` only for the Escape and Start events the always-on Escape and Start binds send to Scaleform. A second line of defence in case the consumed key still reaches PlayerInput's mode bindings. |
| `ProcessEvent`: after `BioHUD.PostRender` | Notes the time (the movie's thread tells a fullscreen movie by the HUD's silence), checks the world pause every frame, lifts a movie pause once the HUD has drawn for a second, and draws the banner with the HUD's Canvas (`Canvas.TextSize` and `Canvas.DrawText`, GEngine's `MediumFont`). |
| `ProcessEvent`: every call on the game thread | Notes the time, for the movie reports only: does the game thread run under a fullscreen movie? |
| `bink2w64_original.dll`: `BinkOpen`, `BinkOpenWithOptions`, `BinkClose`, `BinkWait`, `BinkDoFrame`, `BinkDoFrameAsync` (`Bink.cpp`) | Tracks the open movies (from a file or from memory) and logs who plays them. `BinkWait` also reads Esc, toggles the movie pause on its press and applies a pause. `BinkDoFrame` and `BinkDoFrameAsync` count calls and apply a resume, never a pause. |

- **World pause:** Esc calls the controller's `Pause` exec (what the `pause` command runs), then checks that
  `WorldInfo.Pauser` is set. Esc again calls it once more: `ServerPause` toggles it off. The WorldInfo paused is kept with
  its object index.
- **It never leaves the game paused:** each frame and each Esc, our pause ends when `Pauser` became None without us (a load,
  the game, the Steam overlay closing: `ClearPause` drops every `CanUnpause` that agrees, ours too), when that WorldInfo is
  gone (map change), or when the mode is no longer Conversation or Cinematic (then it resumes). It only pauses a world
  nobody else paused.
- **Movie pause:** Bink movies keep playing under the world pause, so Esc pauses the movie itself.
  - Esc: read by the thread that plays the movie, in each `BinkWait` (some 250 a second while a movie plays, paused or
    not): `GetAsyncKeyState(VK_ESCAPE)`, acted on when it goes down (repeats don't count, a press within 150 ms of the
    last is a bounce), and only while one of the game's windows is in the foreground. The first test showed why: during
    the travel movie the press never reached `Console.InputKey` (the engine's fullscreen-movie input takes it first; only
    the release came through). After half a second without a poll, a key already down is no press, except while our
    movie pause is on: a press seen late then resumes.
  - When: a fullscreen movie plays. That is a movie opened from a file (loading screens are played from memory) that
    called `BinkWait` in the last half second, while `BioHUD.PostRender` hasn't run for a quarter second: the HUD draws
    every frame in play and not at all over a fullscreen movie, so an in-level texture movie never takes Esc. Nothing of
    the game but those two times is read on the movie's thread: no game object, no game mode.
  - Which: the newest fullscreen movie. Other open movies are left alone.
  - Each press toggles: it pauses that movie, or resumes what we paused. `Console.InputKey` takes no part in it: an Esc
    press, repeat or release that does reach it then is only consumed, and arms the skip guard.
  - How: the thread that plays it calls `BinkPause(movie, 1)` at its `BinkWait` (the one that read the press, when it is
    the movie's), and only there. `BinkPause` only makes `BinkWait` answer "wait" and stops the sound, so a player that
    doesn't call `BinkWait` couldn't be paused anyway. A resume (`BinkPause(movie, 0)`) is applied at the movie's next call
    of any kind.
  - Start: the movie's thread can't read it (XInput goes through the game, and Steam Input may stand in for it), and the
    engine's movie input takes its press as it takes Esc's. What it leaves reaches `Console.InputKey` on the game thread,
    which keeps running under the movie: the repeats (after 0.2 s held, then every 0.1 s) and the release. The first of them
    toggles the same pause there, through `Bink::Toggle`: a tap acts on its release, a held press on its first repeat. A
    press with no event for half a second is over (its release lost to a focus change), unless the game thread stalled
    since its last event (no `ProcessEvent` for a quarter second): the pad is read once a frame, so a stall holds its
    repeats back too, and one press must not toggle twice.
  - Ends: on Esc or Start, when the movie closes, when Space's `SkipMovie` reaches the game, or when the HUD has drawn again for a
    second (the game moved on). A pause whose movie has made no Bink call for 2 s is dropped, by the next Esc at the
    latest, which then goes back to the game: nothing reads Esc for that movie any more. A silent game thread doesn't end
    it: Esc is read on the movie's thread.
- **Crash guards:** the key handling, the per-frame checks and the banner each run under an access-violation guard. A fault
  passes the key to the game; in the per-frame checks it resumes and drops our pause (three of them turn the checks off for
  the session); while drawing it turns the banner off. Each is logged. A missing Bink module or export only turns the movie
  pause off.

## Limits and unknowns

- **Tested in game (v1, 2026-10-28):** Esc pauses and resumes a conversation and an in-engine cutscene; Esc in gameplay
  and in the pause menu stays vanilla. The galaxy map's travel movie failed: v1 took Esc in `Console.InputKey`, which
  never got the press during the movie. v2 reads Esc on the movie's thread: the travel movie pauses and resumes (tested,
  a 6.7 s pause held, `BinkWait` kept coming at 240 a second while paused).
- **Gamepad facts (live, before v3, Xbox pad):** the game loads `XINPUT1_4.dll`. In a conversation Start reaches
  `Console.InputKey` as `XboxTypeS_Start` with `bGamepad` TRUE, press and release. During the travel movie vanilla Start
  does nothing, and the first Start event that reached `Console.InputKey` was a repeat (event 2): its press was taken by
  the engine.
- **Tested in game (v3, 2026-10-28, Xbox pad):** Start pauses and resumes a conversation and an in-engine cutscene. In
  the travel movie (three trips) a tap pauses it on its release, and the next tap resumes it: the log shows the release
  alone reaching `Console.InputKey` (event 1), the pause applied at the next `BinkWait` within 0 to 15 ms. A `SkipMovie`
  from the X button 160 ms after a Start release was dropped, as the guard says. Not seen in that log: a Start held into
  its repeats in the movie, Esc and Start mixed, and Start in gameplay.
- **What v1's log showed of the travel movie** (`..\..\BioGame\Movies\UCW_Loading_Flyby.bik`, flags `0x01004400`: from a
  file, 3840x2160, 150 frames, about 5 s, played to its end): it plays on Bink's own thread, not the game thread, which
  calls `BinkWait` in a loop (some 250 a second, `BinkDoFrame` never); `BioHUD.PostRender` doesn't run while it plays; the
  game thread still runs `ProcessEvent` and `Console.InputKey`, which got Esc's release alone. Its game mode is unknown:
  the "Console.InputKey runs while movie #N is open" line now gives it, with the movie mode. The loading screens of that
  session were played from memory (flags `0x04004400`).
- **Skippable fullscreen movies are skipped, not paused:** the engine's movie input gets Esc's press before anything else
  and, in UE3's code (`FFullScreenMovieBink::InputKey`), skips the movie with it when the movie is skippable; the travel
  movie isn't (its press was taken and nothing happened). Such a movie is skipped as the poll reads the press, and the
  pause asked then ends with its close. The log shows it: a movie closed right after an "Esc pressed, read on thread ..."
  line.
- **Esc in a movie is read from the keyboard's state:** a press while another program's window is in the foreground is
  ignored (logged). Start goes through the game's input, so it reaches us when the game reads the pad. Any other Esc during a fullscreen
  movie toggles it, also one meant for the console or the Steam overlay (the overlay keeps the game's window in front).
- **The HUD's silence** tells a fullscreen movie from an in-level one. If the HUD went on drawing over some fullscreen
  movie, Esc would stay vanilla during it: its close line says "never a fullscreen movie (the HUD draws)". An in-level
  movie opened from a file while the HUD is silent for another reason would take Esc: none is known.
- **Does TRUE from `Console.InputKey` keep Esc from PlayerInput's bindings?** Unverified; hence the skip guard. The log
  says, for each `SkipMovie`/`SkipCinematic`, whether it was dropped.
- **Scaleform first:** if a Scaleform movie with input focus takes Esc before the console, nothing reaches us (no
  "Esc pressed" line in the log). The movie pause doesn't depend on it.
- **A GUI over a fullscreen movie:** Esc and Start go to the movie whatever the game mode is.
- **Start held into a movie:** a Start press the game kept (the pause menu's, say) stays the game's to its release, so
  a press held from before the movie doesn't toggle it. If that press's release is lost, the next Start event half a
  second later counts as a new press.
- **Bink 2022.05 hang:** RAD fixed in 2025.05 a hang of players that call `BinkPause` and then stop calling `BinkWait`.
  The pause is only applied to a movie that calls `BinkWait`, and the reports during a pause count those calls. Without
  them nothing can be applied to that movie and Esc can't be read for it: after 2 s Esc goes back to the game ("has made
  no Bink call" in the log), and the movie stays paused until it calls Bink again (the resume is applied then) or closes.
- **A game thread silent under the pause** (a load, a wait for the movie) no longer ends it, as v1's 3 s rule did: Esc
  resumes it from the movie's thread. The reports say how long the game thread has been silent.
- **The banner** uses `EngineFonts.MediumFont`, scaled; there is none during a movie pause (the HUD doesn't draw over a
  fullscreen movie).

## In-game test

Install with `./build.sh --install` (game closed), then:

1. **Conversation:** during a spoken line, Esc. Picture, voice, music and lips stop, "PAUSE" shows at the top. Wait a few
   seconds, Esc: the line resumes at the same word, in sync.
2. **In-engine cutscene** (e.g. the Noveria undocking scene): same, Esc, wait, Esc.
3. **Galaxy map travel movie** (FTL or relay): half a second into it, Esc. Expected: the movie stops (picture and sound);
   wait a few seconds, Esc: it resumes and plays to its end. Then hold a pause for 15 s or more: it must hold (the
   reports while paused say how long the game thread has been silent). Then, on another travel, Space during the movie:
   does it skip it? If Esc does nothing, the log says why: no "is a fullscreen movie" line for it and its close line's
   "never a fullscreen movie (...)", or an "Esc pressed, read on thread ...: not taken, ..." line.
4. **Gameplay:** Esc still opens the pause menu, and closes it. Same with Start.
5. **Start (v3), with a pad:** steps 1 to 3 with Start instead of Esc. The banner says "Start: resume". During the travel
   movie: a tap pauses it on the release, a held press about 0.2 s in; Start again resumes. Then Esc pauses and Start
   resumes, and the other way round. With a PlayStation pad through Steam Input: Options does the same.
6. **Read `Binaries\Win64\Logs\MelePause.log`:** the "Bink: ... hooked" line at start (movie pause "on (Esc read in
   BinkWait)"); "first BioHUD.PostRender" (the HUD owner and GEngine's controller should be the same); one "Esc pressed:
   mode ..." or "Start pressed: mode ..." line per press that reaches `Console.InputKey`, with what it did; during a
   movie, "Start held (or released) during a fullscreen movie, its press unseen here on thread ...: movie pause asked"
   (or resume asked). For the movie: its open line ("from a
   file"), "is a fullscreen movie" with the HUD's silence, "Console.InputKey runs while movie #N is open" (the game mode
   during it), one "Esc pressed, read on thread ..." line per press (movie pause or resume asked), the PAUSED/RESUMED
   lines and the reports between them (frame number staying put; `BinkWait` per second while paused: none for 2 s drops
   the pause, and below some 10 a second a short Esc press can fall between two polls and be missed), "Esc released
   during a fullscreen movie ..." lines, and its close line. No "has made no Bink call" and no "the HUD has drawn for 1 s"
   line should appear during a pause you hold. Also any `SkipMovie`/`SkipCinematic`
   line (dropped or passed) and any dropped `GuiKey` line (a passed `GuiKey` isn't logged).

## License

GPL-3.0 ([`LICENSE`](../../LICENSE)). Third-party code and what it is used for: [`THIRD_PARTY.md`](THIRD_PARTY.md). If
`MelePause.asi` is ever shared, ship the GPL-3.0 text with it.
