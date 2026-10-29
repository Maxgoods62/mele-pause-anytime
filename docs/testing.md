# Testing MelePause in the game

Build and install with `./build.sh --install` while the game is closed ([BUILDING.md](../BUILDING.md)). Then run the
steps below and read the log, `<LE folder>\Game\ME1\Binaries\Win64\Logs\MelePause.log`. MelePause rewrites the log each
time the game starts.

## Steps

1. Conversation. During a spoken line, press Esc. Picture, voice, music and lips stop. The picture blurs and darkens,
   "PAUSE" stands in its frame at the centre, and "Space Resume" and "F7 Photo mode" are at the bottom left, in the
   game's language. Wait a few seconds, then press Esc: the line resumes at the same word, in sync, and the picture is
   sharp again.
2. In-engine cutscene, such as the Noveria undocking scene: the same, Esc, wait, Esc.
3. Galaxy map travel movie (a trip within a cluster, or through a relay). Half a second into it, press Esc. The movie
   stops, picture and sound. Wait a few seconds, then press Esc: it resumes and plays to its end. Then hold a pause for
   15 s or more: it must hold (the reports while paused say how long the game thread has been silent). On another trip,
   press Space during the movie and note whether it skips it. If Esc does nothing, the log says why: there is no "is a
   fullscreen movie" line for it and its close line says "never a fullscreen movie (...)", or there is an "Esc pressed,
   read on thread ...: not taken, ..." line.
4. Gameplay. Esc still opens the pause menu, and closes it. The same with Start.
5. Start, with a gamepad. Steps 1 to 3 with Start instead of Esc. The pause screen shows the A icon, and the Y icon
   where photo mode opens. In the travel movie, a tap pauses it on its release and a held press about 0.2 s in; Start
   again resumes. Then pause with Esc and resume with Start, and the other way round. With a PlayStation pad through
   Steam Input, Options does the same.
6. Photo mode. In a conversation, press Start, then Y. Expected: the same shot, sharp, with no wheel, no subtitle and no
   pause screen, and photo mode's controls at the bottom with the pad's buttons. Move with both sticks and LT/RT. B
   hides and shows the options, and their tabs switch with the buttons the panel shows. X takes a screenshot: a click,
   and a new file in `Documents\BioWare\Mass Effect Legendary Edition\ScreenShots` with no pause screen in it. Y goes
   back to the paused shot with the wheel, the subtitle and the pause screen. Y again, then Start: back too. Start once
   more: the conversation resumes at the same word, and picking a reply with the stick works (a southpaw profile too).
   Then do the same in an in-engine cutscene, and with Esc and F7 on the keyboard (hold the right mouse button to look).
   Y in gameplay, or in a conversation that is not paused, does what it does in the vanilla game.
7. Read the log (below).

## What the log should show

| When | Lines |
|---|---|
| At start | A "Bink: ... hooked" line, with the movie pause "on (Esc read in BinkWait)". "input hooked: ...", "GUI tick hooked: ..." and "GUI sounds hooked: ...", or a "not found at RVA" warning instead. "overlay: swap chain hooks Present true ...". A line that ends in "MelePause 6 ready". |
| At the first frame | "overlay ready: back buffer ..., text in <language>, Exo 2 embedded, blur ready". "first BioHUD.PostRender": the HUD owner and GEngine's controller should be the same. |
| Each press that reaches `Console.InputKey` | One "Esc pressed: mode ..." or "Start pressed: mode ..." line, with what it did. |
| Each pause | "overlay shown: world pause (or movie pause), gamepad (or keyboard) prompts", then "overlay hidden" ("at once" for photo mode). |
| Photo mode | "photo mode enter, mode ..." (the view target, the shot, the subtitle). "photo mode on: ..." (the camera mode, and how far the controller and the view are from the shot: 0 expected), with "pad prompts" after Y or "keyboard prompts" after F7. Then "Y pressed ...: left photo mode", "photo mode exit: ignore counters ...", and "photo mode left (Y) after ... s: mode 5 (Conversation), camera mode ...". The last one gives the number of static binds set back, of camera steps, and what got past the pause: "N key and M axis event(s), G GUI tick(s) (0 changed the Pauser: put back), S GUI sound(s)". M is in the thousands after moving a stick or the mouse, G about one per frame, S at least one per screenshot. |
| A movie | Its open line ("from a file"); "is a fullscreen movie" with the HUD's silence; "Console.InputKey runs while movie #N is open" (the game mode during it); one "Esc pressed, read on thread ..." line per press (movie pause or resume asked); the PAUSED and RESUMED lines and the reports between them; "Esc released during a fullscreen movie ..." lines; its close line. |
| Start in a movie | "Start held (or released) during a fullscreen movie, its press unseen here on thread ...: movie pause asked" (or resume asked). |
| Skips | Any `SkipMovie` or `SkipCinematic` line (dropped or passed), and any dropped `GuiKey` line. A passed `GuiKey` is not logged. |

During a movie pause, the reports show the frame number staying put and the `BinkWait` calls per second. None for 2 s
drops the pause. Below some 10 a second, a short Esc press can fall between two polls and be missed. No "has made no
Bink call" and no "the HUD has drawn for 1 s" line should appear during a pause you hold.

## Tested so far

All in the game on 2026-10-28, with builds v2 to v5 and an Xbox pad for the gamepad steps. v6 changed only the build.

| Area | Result |
|---|---|
| Esc in a conversation and in an in-engine cutscene | Pauses and resumes in sync. Esc in gameplay and in the pause menu stays vanilla. |
| Esc in the travel movie | Pauses and resumes. A 6.7 s pause held, with `BinkWait` still called 240 times a second while paused. |
| Start in a conversation and in an in-engine cutscene | Pauses and resumes. |
| Start in the travel movie | Over three trips, a tap paused it on its release and the next tap resumed it. The release alone reached `Console.InputKey`, and the pause was applied at the next `BinkWait`, within 0 to 15 ms. A `SkipMovie` from the X button 160 ms after a Start release was dropped by the skip guard. |
| Photo mode | Entry and exit in a conversation and in an in-engine cutscene. The camera moves and turns with the pad (Y) and with keyboard and mouse (F7, right button held). Prompts, tabs, options, and screenshots with their click work. |
| Pause screen | Shows under the world pause and over the paused travel movie, and hides for photo mode and the console. |
| Space and A | Resume in a conversation, in a cutscene, and in the travel movie (A). |

Not covered by a recorded test yet: a Start press held into its repeats during a movie, Esc and Start mixed during a
movie, Start in gameplay, a PlayStation pad, and a skippable fullscreen movie.
