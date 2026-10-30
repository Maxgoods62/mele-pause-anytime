# Testing MelePause in the game

Each game is tested with its own build. With the game closed, build and install with
`./build.sh --game LE<n> --install` ([BUILDING.md](../BUILDING.md)), and check that the game's ASI folder holds
`LE<n>MelePause.asi` and no `MelePause.asi` (the file of version 6 and older). Then run the sheet below in that game and
read the log, `<LE folder>\Game\ME<n>\Binaries\Win64\Logs\MelePause.log`. MelePause rewrites the log each time the game
starts. Keep the game's window in front for the whole test: in the background, LE1 holds the world still every frame,
and a pause tested then proves nothing.

The steps name LE1's scenes. In another game, use a conversation, an in-engine cutscene and a fullscreen movie of that
game; [games/LE2.md](games/LE2.md) and [games/LE3.md](games/LE3.md) have LE2's and LE3's sheets. A feature that the
game's `src/game/LE<n>.hpp` turns off (`MELEPAUSE_MOVIES` or `MELEPAUSE_PHOTO` at 0) has a "not in the LE<n> build" line
in the log instead of its hooks, and its step checks that its keys keep their vanilla job.

## Release gate

Before a version is tagged, each game its CHANGELOG entry names (the `Games:` line) passes a sheet, with the build that
`package.sh` makes:

- the full sheet, S1 to S7, with a gamepad and with keyboard and mouse, in each game the version adds or reworks (a new
  feature, moved code, a new file name);
- the smoke sheet, S1, S3 and S6, in every other game it covers. The code is shared, so a change made for one game can
  break another.

## Sheet

- S1, conversation. During a spoken line, press Esc. Picture, voice, music and lips stop. The picture blurs and darkens,
  "PAUSE" stands in its frame at the centre, and "Space Resume" and "F7 Photo mode" are at the bottom left, in the
  game's language. Wait a few seconds, then press Esc: the line resumes at the same word, in sync, and the picture is
  sharp again. Then the same with Start on a gamepad: the pause screen shows the A icon, and the Y icon where photo
  mode opens.
- S2, in-engine cutscene, such as the Noveria undocking scene: the same, Esc, wait, Esc, then Start, wait, Start.
- S3, galaxy map travel movie (a trip within a cluster, or through a relay). Half a second into it, press Esc. The
  movie stops, picture and sound. Wait a few seconds, then press Esc: it resumes and plays to its end. Then hold a pause
  for 15 s or more: it must hold (the reports while paused say how long the game thread has been silent). With a
  gamepad, a Start tap pauses it on its release and a held press about 0.2 s in; Start again resumes. On another trip,
  press Space during the movie and note whether it skips it. If Esc does nothing, the log says why: there is no "is a
  fullscreen movie" line for it and its close line says "never a fullscreen movie (...)", or there is an "Esc pressed,
  read on thread ...: not taken, ..." line.
- S4, gameplay. Esc still opens the pause menu, and closes it. The same with Start.
- S5, resume keys and the console. In a conversation, pause with Esc and resume with Space, then pause with Start and
  resume with A. Pause with Esc and resume with Start, and the other way round. With nothing paused, Space skips the
  line as in the vanilla game. Open the console during a pause: the pause screen hides, and Esc closes the console as
  in the vanilla game ("Esc pressed in ...: the console is open, vanilla" in the log). With a PlayStation pad through
  Steam Input, Options does what Start does.
- S6, photo mode. In a conversation, press Start, then Y. Expected: the same shot, sharp, with no wheel, no subtitle and
  no pause screen, and photo mode's controls at the bottom with the pad's buttons. Move with both sticks and LT/RT. B
  hides and shows the options, and their tabs switch with the buttons the panel shows. X takes a screenshot: a click,
  and a new file in `Documents\BioWare\Mass Effect Legendary Edition\ScreenShots` with no pause screen in it. Y goes
  back to the paused shot with the wheel, the subtitle and the pause screen. Y again, then Start: back too. Start once
  more: the conversation resumes at the same word, and picking a reply with the stick works (a southpaw profile too).
  Then do the same in an in-engine cutscene, and with Esc and F7 on the keyboard (hold the right mouse button to look).
  Y in gameplay, or in a conversation that is not paused, does what it does in the vanilla game.
- S7, the log. Read it against the table below, the photo mode counters included.

## What the log should show

| When | Lines |
|---|---|
| At start | A "Bink: ... hooked" line, with the movie pause "on (Esc read in BinkWait)". "input hooked: ...", "GUI tick hooked: ..." and "GUI sounds hooked: ..." (LE2: "GUI sounds: not hooked in the LE2 build, ..."), or a "not found at RVA" warning instead. "overlay: swap chain hooks Present true ...". A line that ends in "LE<n>MelePause N ready", where N is the build's `ASI_VERSION`. In a build without the movie pause or photo mode, "movie pause: not in the LE<n> build" stands for the Bink line, and "photo mode under the pause: not in the LE<n> build" for the three hook lines. |
| At the first frame | "overlay ready: back buffer ..., text in <language>, Exo 2 embedded, blur ready". "first BioHUD.PostRender": the HUD owner and GEngine's controller should be the same. |
| Each press that reaches `Console.InputKey` | One "Esc pressed: mode ..." or "Start pressed: mode ..." line, with what it did. |
| Each pause | "overlay shown: world pause (or movie pause), gamepad (or keyboard) prompts", then "overlay hidden" ("at once" for photo mode). |
| Photo mode | "photo mode enter, mode ..." (the view target, the shot, the subtitle). "photo mode on: ..." (the camera mode; how far the controller was from the shot, in units and degrees, before it was put on it; and how far the view is: 0 uu and 0 deg expected), with "pad prompts" after Y or "keyboard prompts" after F7. Then "Y pressed ...: left photo mode", "photo mode exit: ignore counters ...", and "photo mode left (Y) after ... s: mode 5 (Conversation), camera mode ...". The last one gives the number of static binds set back, of camera steps, and what got past the pause: "N key and M axis event(s), G GUI tick(s) (0 changed the Pauser: put back), S GUI sound(s)". M is in the thousands after moving a stick or the mouse, G about one per frame, S at least one per screenshot. |
| A movie | Its open line ("from a file"); "is a fullscreen movie" with the HUD's silence; "Console.InputKey runs while movie #N is open" (the game mode during it); one "Esc pressed, read on thread ..." line per press (movie pause or resume asked); the PAUSED and RESUMED lines and the reports between them; "Esc released during a fullscreen movie ..." lines; its close line. |
| Start in a movie | "Start held (or released) during a fullscreen movie, its press unseen here on thread ...: movie pause asked" (or resume asked). |
| Skips | Any `SkipMovie` or `SkipCinematic` line (dropped or passed), and any dropped `GuiKey` line. A passed `GuiKey` is not logged. |

During a movie pause, the reports show the frame number staying put and the `BinkWait` calls per second. None for 2 s
drops the pause. Below some 10 a second, a short Esc press can fall between two polls and be missed. No "has made no
Bink call" and no "the HUD has drawn for 1 s" line should appear during a pause you hold.

## Tested so far

### LE1

On 2026-10-28, builds v2 to v5, with an Xbox pad for the gamepad steps; v6 changed only the build. On 2026-10-29, the
v7 code before the rename passed the full sheet with keyboard and mouse and with an Xbox pad, and Start in gameplay
stayed vanilla. The renamed `LE1MelePause.asi` passed it with keyboard and mouse; the gamepad steps were not rerun.
The v8 file from `LE1MelePause-v8.zip` passed the smoke sheet with keyboard and mouse and with a gamepad: Esc and
Start in conversations and cutscenes, the travel movie paused with Start and resumed with A, photo mode with Y and F7.

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
movie, a PlayStation pad, and a skippable fullscreen movie.

### LE2

On 2026-10-29, the v7 build, with photo mode and the movie pause off, passed the
[LE2 sheet](games/LE2.md#sheet-for-a-first-run) with keyboard and mouse and with a gamepad, over three runs. The v8 file
from `LE2MelePause-v8.zip` passed it again with keyboard and mouse (conversation, cutscene, Space resume, Esc vanilla
elsewhere) and with the gamepad's Start and A.

| Area | Result |
|---|---|
| Steam overlay, before MelePause was installed | Stops the picture, voice, lips and music; all resume together about 2 s after it closes. |
| Esc and Start in a conversation and in an in-engine cutscene | Pause and resume in sync. Space and A resume. The pause screen shows keyboard or gamepad prompts and no photo key. One pause held 11 minutes, with the console opened and closed during it. |
| Skips | With nothing paused, the skip keys skip a skippable cutscene. The Esc or Start that paused or resumed one did not skip it. |
| Esc and Start elsewhere | Vanilla in gameplay (the pause menu), in menus and on the galaxy map. Esc skips a loading movie as in the vanilla game, and the world never paused under a movie. |
| Console | Opening it hides the pause screen at once. Esc or the console key closes it, and the pause holds. |
| Y and F7 | Vanilla. |

Not covered yet: the Kasumi DLC's montage ([games/LE2.md](games/LE2.md#open-questions), question 2) and a PlayStation
pad.

On 2026-10-30 a test build of the LE2 movie pause (v9's code, still numbered 8) passed S3 of the
[LE2 sheet](games/LE2.md#sheet-for-a-first-run) with keyboard and mouse and with a gamepad.

| Area | Result |
|---|---|
| Relay jump (`GLO_01_Relay`) | Esc and Start pause the picture and the whoosh together, and resume them in sync (Wwise pause and resume: AKRESULT 1). Five pauses in one jump and a 15 s hold, with no offset. Space does nothing: the game does not let this movie skip. |
| Citadel arrival (`CitHub_Arrival`) | Esc and Start pause and resume it, with no skip. Space and X skip it, and no sound plays on. Under the pause, X and Enter skip nothing; Space and A resume. |
| Log | "movie input hooked" at start; "kept from the engine's movie input" for each Esc and Start press; "given to the engine's movie input as Enter" for Space and X. |

Not covered yet: a movie with no known sound (Esc must skip it as in the vanilla game), the Prologue's FirstFlight
movie (level music plays beside it) and `visionavi` (Bink audio). The same build passed S3 in LE1 (the travel movie):
Esc paused it, Start resumed it after 8 s, Start paused it and A resumed it, twice, with no movie-input hook line.

On 2026-10-30 a test build of LE2 photo mode (v10's code, still numbered 9) passed S6 in conversations, with keyboard and mouse
and with a gamepad, over three runs. The two camera faults the first runs showed are fixed in it
([how-it-works.md](how-it-works.md#photo-mode-entering), step 7).

| Area | Result |
|---|---|
| Entry and exit | Y and F7 open photo mode on the paused shot; Y, F7, Esc and Start leave it, still paused. The camera moves with the pad and with keyboard and mouse, and the panel answers. |
| Camera | The first F7 of a pause in a director's shot opens facing the shot (the log: the controller 24 degrees off before it was put back, the view 0). Re-entries and the resume keep the shot, with no jump. |
| Sounds | Photo mode's sounds play under the pause, with no hook. |

Not covered yet: LE2 photo mode in an in-engine cutscene. The same build passed S6 in LE1, in a conversation and in an
in-engine cutscene, with Y and F7. One entry pressed in the first second of a cutscene, while the camera still blended
from the player to the director's camera (the log's "pending" view target), opened 51 units and 8 degrees off the shot,
with "camera mode None" for the whole session: the camera did not answer. MelePause now holds that blend for the
session ("photo mode: the blend to ... held" in the log). Not seen in the game yet: 46 later entries in an STA00
cutscene had no blend pending, and all opened on the shot.

### LE3

On 2026-10-30 the first LE3 build (v10's code, with photo mode and the movie pause off) passed part of the
[LE3 sheet](games/LE3.md#sheet-for-a-first-run), on the Normandy and the Citadel, with keyboard and mouse and with a
gamepad.

| Area | Result |
|---|---|
| Esc and Start in a conversation and in an in-engine cutscene | 23 pauses (19 in conversations, 4 in cutscenes), with picture, voice, lips and music in sync. Esc and Space resume; Start pauses with gamepad prompts and A resumes. |
| Skips | LE3's cutscene mode binds Esc and Space to the skip (`PC_CineSkip`), and X (`Console_CineSkip`). The Esc that paused or resumed a cutscene did not skip it. |
| Esc and Start elsewhere | Vanilla in gameplay (the pause menu), in menus, on the galaxy map, and in the game's own photo mode. |
| Log | The start lines as the sheet says, "LE3MelePause 10 ready", the first `BioHUD.PostRender` with the same controller, and the text in French. |

Not covered yet: the console under the pause, Y and F7 under the pause, a fullscreen movie, and the skips with nothing
paused.
