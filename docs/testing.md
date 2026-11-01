# Testing MelePause in the game

Each game is tested with its own build. With the game closed, build and install with
`./build.sh --game LE<n> --install` ([BUILDING.md](../BUILDING.md)), and check that the game's ASI folder holds
`LE<n>MelePause.asi` and no `MelePause.asi` (the file of version 6 and older). Then run the sheet below in that game and
read the log, `<LE folder>\Game\ME<n>\Binaries\Win64\Logs\MelePause.log`. MelePause rewrites the log each time the game
starts. Keep the game's window in front for the whole test: in the background, LE1 holds the world still every frame,
and a pause tested then proves nothing.

The steps name LE1's scenes. In another game, use a conversation, an in-engine cutscene and a fullscreen movie of that
game; [games/LE2.md](games/LE2.md) and [games/LE3.md](games/LE3.md) have LE2's and LE3's sheets.

## Release gate

Before a version is tagged, each game its CHANGELOG entry names (the `Games:` line) passes a sheet, with the build that
`package.sh` makes:

- the full sheet, S1 to S7, with a gamepad and with keyboard and mouse, in each game the version adds or reworks (a new
  feature, moved code, a new file name);
- the smoke sheet, S1, S3 and S6, in every other game it covers. The code is shared, so a change made for one game can
  break another.

## Sheet

- S1, conversation. During a spoken line, press Esc. Picture, voice, music and lips stop. The picture blurs and darkens,
  the title stands on a band in the game's menu colours at the centre, and the keys are left of their labels at the
  bottom left, the labels in the game's language ("Resume" and "Photo Mode"; in French "Mode photo", with "Continuer" in
  LE1 and "Reprendre" in LE2 and LE3). In LE1 the title is the game's word too ("PAUSED" in English, "PAUSE" in French).
  The game's menu sound plays as the pause starts. Wait a few seconds, then press Esc: the line resumes at the same word,
  in sync, the picture is sharp again, and the sound plays again. Then the same with Start on a gamepad: the pause screen
  shows the A icon, and the Y icon where photo mode opens (the game's own pictures, with no dark box around them, and they
  fade in and out with the screen).
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
| At start | A "Bink: ... hooked" line, with the movie pause "on (Esc read in BinkWait)". "input hooked: ...", "GUI tick hooked: ..." and "GUI sounds hooked: ..." (LE2 and LE3: "GUI sounds: not hooked in the LE<n> build, ..."), or a "not found at RVA" warning instead. "overlay: swap chain hooks Present true ...". A line that ends in "LE<n>MelePause N ready", where N is the build's `ASI_VERSION`. In a build without the movie pause or photo mode, "movie pause: not in the LE<n> build" stands for the Bink line, and "photo mode under the pause: not in the LE<n> build" for the three hook lines. |
| At the first frame | "overlay ready: back buffer ..., LE1 look (LE2 look, LE3 look), text in <language>, Exo 2 embedded, blur ready". "first BioHUD.PostRender": the HUD owner and GEngine's controller should be the same. "the game's string table: title ..., resume ..., photo mode ..." with the words in the game's language (LE2 and LE3 have no title there), and "overlay: the game's gamepad icons: A fine, Y fine: used" ("refused" and "ours drawn instead" if a view failed its checks, and "no view of the game's gamepad icons" if the textures gave none). LE3: "the game's fonts: loaded their package in ... ms". A line with "the pause screen keeps Exo 2" if the game's fonts could not be read. |
| Each press that reaches `Console.InputKey` | One "Esc pressed: mode ..." or "Start pressed: mode ..." line, with what it did. |
| Each pause | "overlay shown: world pause (or movie pause), gamepad (or keyboard) prompts", then "overlay hidden" ("at once" for photo mode). At the first one, "overlay: the game's fonts: ... used" ("Exo 2 stays" if not), then "overlay: the game's texts: ..." with "used" after each word (Japanese without the game's fonts: "kept ours"); after "world paused", "GUI sound InGameGuiEnter: played"; after a key's "world resumed", "GUI sound InGameGuiExit: played". |
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
On 2026-10-31, v14's code before its last fix (the GUI sound depth put back after a fault, which changes nothing
without one), in French, in a Noveria conversation: the LE1 look showed with the game's words ("PAUSE", "Continuer",
"Mode photo", all three "used" in the log), and the menu swish was heard as the pause started and as Space resumed.
The gamepad prompts (A and Y discs) showed too. The final v14 build then ran in the game with no problem.
On 2026-10-31, the final v15 build, in a Noveria conversation, paused with Start several times in a row: the log has "the
game's gamepad icons: A fine, Y fine: used" and no fault, and a screenshot shows the game's own A and Y in place of v14's
discs, at the same size and place. They fade with the screen.
On 2026-10-31, the final v16 build, in French, in a Noveria conversation, paused with Start and then with Esc: the log
has "the game's fonts: ... used" and all three words "used", with no warning and no fault, and screenshots show the title in
Aero Light, centred on its band, the labels in BioMass, and the `Espace` and `F7` keycaps in Aero Light. Russian and
Japanese, the movie pause and the fall back to Exo 2 were not run in the game. The fonts were added between ImGui's frames
in French, Russian and Japanese in a headless test, with ImGui's asserts on.
On 2026-10-31, v17, in French, paused with a gamepad and with the keyboard: the look is v16's. The log has "LE1 look", the
game's icons and fonts "used", all three words "used", and both GUI sounds "played", with no warning.

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

On 2026-11-01 Space's resume of `CitHub_Arrival` skipped the movie on v18: the movie's thread resumed it, the game thread gave
the same press to the movie input 4 ms later, and the movie closed at frame 145. The row above had met the other order. A
test build of the fix, still numbered 18, ran 20 Esc and Space cycles over two arrivals with no skip: 19 in that order, 4 to 5
ms apart, each kept from the movie input, and one with the game thread first. A resumed 5 times with no skip, one of them held
for 2 s, and Space with nothing paused skipped the movie. Not run on that build: X with nothing paused. In LE3, 15 Space resumes
of one relay jump all met the safe order, so the fix did not run there; the relay cannot skip, and it played on. In LE1, which
has no movie input hook, 11 Space resumes of one relay jump worked, 9 of them with the movie's thread first.

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

On 2026-10-31, v17, in French, in a Normandy conversation (`Biop_Nor`): the LE2 look showed, with the title in Biomass and
gold on its brown band, the labels in Biomass and orange. The log has "overlay: the game's gamepad icons: A fine, Y fine:
used", "overlay: the game's fonts: ... used", and "Reprendre" and "Mode photo" both "used", with the title "kept ours". On
a build without the texture addresses it had both GUI sounds "played" and, as meant, "no view of the game's gamepad
icons": the drawn discs stayed. A screenshot shows the game's A and Y with the addresses, at the same place as the discs.
Keycaps, Russian, Japanese, the movie pause and the fall back to Exo 2 were not checked on a screenshot or in the log.

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

Photo mode under the pause, on the Normandy and the Citadel (2026-10-30), with keyboard and mouse and with a gamepad:

| Build | Result |
|---|---|
| First photo build (input hooks, no GUI tick hook) | Y and F7 open and leave photo mode in a conversation and in a cutscene; the camera answers the mouse, the keys and the sticks. The panel never loads: its labels read "TEST STRING", and the reply wheel and the subtitle stay on screen. The log counts 0 GUI ticks. |
| `GfxClock` on the GFx movies' `Tick` | The panel loads and answers (tabs, options). The conversation movie is set aside each time ("set aside" line). With the reply wheel up at the entry, the wheel came back now and then while photo mode ran. |
| v12, `PhotoGui::Hold` each frame | 35 photo sessions, the wheel up or not: the wheel stays hidden. The game showed the movie again in 20 of them, on 25 to 170303 frames (a long session); each time it was hidden again. No warning or error in the log. |
| v12, in-engine cutscene (Omega) | 4 photo sessions on a `CameraActor`'s shot, in game mode 7: the panel loads and answers. 435 to 18877 GUI ticks, one per frame; the conversation movie set aside, never shown again. Left with F7 and Esc. |

The console under the pause works in v12 (user report, 2026-10-30).

The movie pause, a test build of v13's code (2026-10-30), with the keyboard: user "works well".

| Area | Result |
|---|---|
| Relay jump (`GLO_01_Relay`), 2 jumps | The latch named its event ("id 2556726104 (hash of Play_cineanim_massrelay 2556726104)"), the movie opened from a file and was fullscreen 0.2 s after its open. 8 Esc cycles: each "PAUSED in BinkWait ... AKRESULT 1" and "RESUMED ... AKRESULT 1", the frame held, the pause screen over the paused frame ("presented from its BinkWait"), no skip. Longest hold 2.3 s. |
| Space with nothing paused | 3 presses "given to the engine's movie input: not taken": the relay played to its end. |
| Modes | "modes on 0 8 9 10 11" during the relay: the cutscene and movie modes on top of the galaxy map's. The player's flags 0x400 (keys ignored by the movie input), skippable 0. |
| Loading screens, Citadel arrival | Movies opened by file handle (player flags 0x81): "no known sound", Esc stayed the game's. A movie opened from memory during the arrival conversation: refused, and Esc paused the world as in v12. |

Not covered yet: photo mode's GUI sounds, a movie inside a cutscene or a conversation, a 15 s movie hold, Start and A in
a movie, and the skips with nothing paused.

On 2026-10-31, v18, in French, on the Normandy (`BioP_Nor`) and the Citadel (`BioP_CitHub`): the LE3 look showed, with the
title in Biomass on the blue band, the labels in pale cyan and the game's A and Y (user: "works
well"). The log has "LE3 look", "loaded their package in 31 ms" at the main menu, "A fine, Y fine: used", both fonts "used" and
both labels "used", with no warning. 7 pauses (6 in conversations, 1 in a cutscene), 5 with gamepad prompts and 2 with
keyboard prompts, resumed with A, Esc and Start; each played `InGameGuiEnter` and `InGameGuiExit`. Not run with v18: Space as
the resume key, photo mode under the pause, a movie pause, Russian, Japanese, the Exo 2 fallback.
