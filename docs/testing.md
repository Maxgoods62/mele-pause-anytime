# Testing Pause Anytime

This page is for the maintainer. It holds the release gate, the test sheet, what the log should show and every result
recorded so far. LE1, LE2 and LE3 each have their own file, so each game is tested on its own.

## Install a test build

Close the game, then build and install one game's file:

```bash
./build.sh --game LE<n> --install
```

[BUILDING.md](../BUILDING.md) explains the build and its checks. `--install` needs the ASI loader in that game:
`bink2w64_original.dll` and an `ASI` folder in `Game\ME<n>\Binaries\Win64`. ME3Tweaks Mod Manager installs the loader
with any mod. After the install, that `ASI` folder holds `LE<n>PauseAnytime.asi`.

For the release gate, test the zip instead, and copy its files as [install.md](install.md) says.

The log is `<LE folder>\Game\ME<n>\Binaries\Win64\Logs\PauseAnytime.log`. Pause Anytime rewrites it each time the game
starts, so copy it before you start the game again.

## Keep the game in front

Keep the game window in front for the whole test. In the background, LE1 holds the world still every frame, so a pause
tested then proves nothing. In every game, an Esc read during a movie while another program is in front is refused, and
the log says "not taken, another program's window is in the foreground".

## Release gate

Before you tag a version, test it in each game on the `Games:` line of its [CHANGELOG](../CHANGELOG.md) entry. Use the
zip from the latest page, as step 3 of
[Publish a version](https://github.com/Maxgoods62/mele-maxgoods-toolkit/blob/main/docs/releasing.md#publish-a-version)
on the toolkit's releasing page says. While the repo is private, `main` publishes no latest page: take the same zip
from the `dist` artifact of the workflow run on `main`. The same page says how that zip is built and checked.

- Full sheet: S1 to S8 in every game, and S9 in LE2 and LE3. It is due in each game that the version adds or
  reworks: a new feature, moved code or a new file name.
- Smoke sheet: S1, S3 and S6 in every other game that the version covers. The code is shared, so a change made for one
  game can break another.

S8, the look, is in the full sheet because one change can alter the look in every game. S9 is for LE2 and LE3, where the
pause sound holds the game's audio; LE1's does not. L0 is a one-time check outside the gate.

## The sheet

Run each step with keyboard and mouse, and again with a gamepad. A note that starts with a game's name holds what only
that game needs. The quoted log lines are listed in full under [What the log should show](#what-the-log-should-show).

### L0, the Steam overlay without Pause Anytime

Once in LE2 and once in LE3, before Pause Anytime is installed. In LE1 and LE2 the overlay sets the same world pause
that Pause Anytime uses (`WorldInfo.Pauser`).

1. During a conversation line, open the Steam overlay (Shift+Tab) for 15 s. Do it again during an in-engine cutscene.
2. Note whether voice, lips and music stop together, and whether they resume in sync.

### S1, conversation

1. During a spoken line, press Esc. Picture, voice, music and lips stop, and the game's menu sound plays.
2. The picture blurs and darkens. The title stands at the centre, on a band in the game's menu colours.
3. The keys stand left of their labels at the bottom left. The labels are in the game's language: "Resume" and "Photo
   Mode" in English. In French they are "Mode photo", with "Continuer" in LE1 and "Reprendre" in LE2 and LE3.
4. The title is Pause Anytime's own word in every game: "PAUSE" in English and in French.
5. Wait a few seconds, then press Esc. The line resumes at the same word, in sync. The picture is sharp again, and the
   menu sound plays again.
6. Do the same with Start on a gamepad. The pause screen shows the A button, and the Y button where photo mode opens.
   They are the game's own pictures, with no dark box around them, and they fade in and out with the screen.

### S2, in-engine cutscene

1. In any in-engine cutscene, do as in S1: Esc, wait, Esc. Then Start, wait, Start.

- LE2 and LE3: in the game without mods, Esc, Space and the pad's X skip a skippable cutscene. Here the Esc that pauses
  and the Space that resumes must not skip it. With nothing paused, Space and X still skip it. The log names each
  `SkipCinematic`, passed or dropped.

### S3, fullscreen movie

| Game | Movie |
|---|---|
| LE1 | the galaxy map travel movie, on a trip within a cluster or through a relay |
| LE2 | the relay jump (`GLO_01_Relay`), and the Citadel arrival (`CitHub_Arrival`) or departure |
| LE3 | the relay jump, from the galaxy map to another cluster |

1. Half a second into the movie, press Esc. The picture and the sound stop. The pause screen comes up over the still
   frame, with Resume and no photo mode key.
2. Wait a few seconds, then press Esc. The movie resumes and plays to its end.
3. On another trip, hold a pause for 15 s or more. The movie stays paused. The reports while paused say how long the
   game thread has been silent.
4. Pause five times in one movie. Picture and sound stay in sync.
5. Resume with Start, Space and A too. None of them skips the movie.

- If Esc does nothing, the log shows why. Either the movie has no "is a fullscreen movie" line and its close line says
  "never a fullscreen movie (...)", or there is an "Esc pressed, read on thread ...: not taken, ..." line.
- If the pause screen shows only as the movie pauses, look for "overlay: the paused movie plays on thread ..." in the
  log.
- LE1, with a gamepad: a Start tap pauses the movie when you let go, and a held Start pauses it about 0.2 s after the
  press. Start again resumes.
- LE1: on another trip, press Space with nothing paused, and note whether it skips the movie.
- LE2 and LE3: Start pauses the movie as soon as you press it. Picture and sound (the relay's whoosh) stop and resume
  together.
- LE2 and LE3: under the pause, press X. Note whether it resumes the movie, and in LE3 whether anything skips. The movie
  mode binds X to `SkipMovie`, and a `SkipMovie` that no key of Pause Anytime sent ends the movie pause. No test has
  recorded this yet.
- LE2: with nothing paused, Space and X skip the Citadel movie, and do nothing in the relay jump. Under the pause, X,
  Enter and A skip nothing. The world never pauses under a movie.
- LE2: in a movie with "no known sound" in its fullscreen line, Esc skips the movie as in the game without mods, if the
  game lets you skip it. In the Kasumi DLC's montage, note what Esc and Start do ([games/LE2.md](games/LE2.md)).
- LE3: with nothing paused, Space and X skip nothing in the relay jump, which is not skippable.
- LE3: in a loading screen, Esc and Start do what they do in the game without mods, and the world does not pause under
  the movie.
- LE3: in a movie inside a cutscene or a conversation, note what Esc and Start do, and whether the world pauses. This is
  not tested yet ([games/LE3.md](games/LE3.md)).

### S4, gameplay

1. In gameplay, press Esc. The pause menu opens, and Esc closes it. Do the same with Start.
2. On the galaxy map and in a menu, Esc and Start do what they do in the game without mods.

### S5, resume keys and the console

1. In a conversation, pause with Esc and resume with Space. Then pause with Start and resume with A.
2. Pause with Esc and resume with Start, then the other way round.
3. With nothing paused, Space skips the line as in the game without mods.
4. Open the console during a pause. The pause screen hides at once. Esc closes the console as in the game without mods,
   and the pause holds. The log has "Esc pressed in ...: the console is open, vanilla".
5. With a PlayStation pad through Steam Input, Options does what Start does.

### S6, photo mode

1. In a conversation, press Start, then Y. The same shot shows, sharp, with no reply wheel, no subtitle and no pause
   screen. Photo mode's controls are at the bottom, with the pad's buttons.
2. Move with both sticks and with LT and RT. B hides and shows the options, and their tabs switch with the buttons the
   panel shows.
3. Press X to take a screenshot. A click plays, and the new file in the game's screenshots folder has no pause screen.
4. Press Y. The paused shot is back, with the wheel, the subtitle and the pause screen. Press Y again, then Start: the
   paused shot is back too.
5. Press Start once more. The conversation resumes at the same word. Picking a reply with the stick works, with a
   southpaw profile too.
6. Do the same in an in-engine cutscene. Then do it all on the keyboard, with Esc and F7. Hold the right mouse button to
   look around.
7. Y in gameplay, or in a conversation that is not paused, does what it does in the game without mods.

- In every game, the "photo mode on" line gives the view 0 uu and 0 deg from the shot.
- In the first second of a cutscene, while the camera still blends to the director's camera, open photo mode. It opens
  on the shot, and the log has "photo mode: the blend to ... held". No test has seen this yet.
- LE2: in a director's shot, the first Y or F7 of a pause opens photo mode facing the shot. The clicks and the
  screenshot sound play.
- LE3: do it with the reply wheel up and without. The panel shows its tabs and options, never "TEST STRING". The wheel
  stays hidden until you leave photo mode.

### S7, the log

1. Read the log against [What the log should show](#what-the-log-should-show), the photo mode counters included.
2. Each press logs the game's mode number and name: "Esc pressed: mode 5 (Conversation)" in LE1 and LE2, "Esc pressed:
   mode 7 (Conversation)" in LE3.

### S8, the look

1. Each key stands left of its label. The keyboard's keys are grey keycaps.
2. With a gamepad, the game's A and Y sit at the height of their labels. They are not drawn discs.
3. There is no dash left or right of the title's band.
4. The colours and fonts match the table below.

| Game | Title | Band | Labels | Blur |
|---|---|---|---|---|
| LE1 | pale cyan, Aero Light | dark blue, a light blue rule along each edge | blue, BioMass | tinted blue |
| LE2 | gold, BioMass | brown, an orange rule along each edge | orange, BioMass | tinted warm |
| LE3 | white, BioMass | blue, a bright blue rule along each edge | pale cyan, BioMass | tinted blue |

In the log, the "overlay ready" line says "LE<n> look", and the game's icons and fonts are "used". The "overlay: the
game's texts" line reads `title "" kept ours`, since no game gives a title word, then "used" after the resume word and
the photo mode word. "GUI sound InGameGuiEnter: played" follows "world paused". In LE3, the main menu adds "the
game's fonts: loaded their package in ... ms: SFXGUI_Fonts.ObjectReferencer_448".

### S9, the Steam overlay

In LE2 and LE3.

1. During a long spoken line, press Esc, then Shift+Tab to open the overlay.
2. Wait 2 s, then press Shift+Tab again to close it. Press no other key.
3. The game's menu sound plays as the overlay opens. About 2 s after the overlay closes, the scene resumes by itself,
   and the line goes on to its end.
4. The log has "game audio released: the Steam overlay is open, the world still held" with one "GUI sound
   InGameGuiExit: played". Then it has "world pause ended", and no second `InGameGuiExit`.
5. Pause again, open the overlay, and press Esc while it is open. The log has "still paused by ...", and still no
   second `InGameGuiExit`.

## What the log should show

Each line starts with its time and level, then `(LE<n>PauseAnytime)`. Below, "..." stands for the parts that change.

### At start

In this order:

- `ProcessEvent hooked`;
- `input hooked: ...` and `GUI tick hooked: ...`, then `GUI sounds hooked: ...` in LE1, or `GUI sounds: not hooked in
  the LE<n> build, ...` in LE2 and LE3. A warning with "not found at RVA" stands in place of a hook that was left out;
- LE2 and LE3: `movie sound: the exe's AK::SoundEngine::ExecuteActionOnEvent at ...`, then `Bink: Esc and Start pause
  only a movie whose sound is known, ...`;
- `Bink: bink2w64_original.dll hooked (...), BinkPause true: movie pause on (Esc read in BinkWait)`;
- LE2 and LE3: `movie input hooked: ...`, which in LE2 ends with `given as Enter`;
- `overlay: swap chain hooks Present true ...`;
- a line that ends in `LE<n>PauseAnytime N ready`, where N is the build's version, such as 1.0.

### At the first frames

- `overlay ready: back buffer ..., LE<n> look, text in <language>, Exo 2 embedded, blur ready`;
- `first BioHUD.PostRender on thread ...: HUD owner ..., GEngine's local player controller ... (same)`. "DIFFERENT" in
  place of "same" means Esc would read the wrong controller;
- `the game's string table: title "", resume "...", photo mode "..."`, with the two words in the game's language. The
  title is empty in all three games;
- `overlay: the game's gamepad icons: A fine, Y fine: used`. A view that failed its checks gives "refused" and "ours
  drawn instead". Textures that gave no view give a warning that starts "no view of the game's gamepad icons";
- LE3: `the game's fonts: loaded their package in ... ms: ...`;
- a warning that ends in "the pause screen keeps Exo 2" if the game's fonts could not be read.

### Each press

- One `Esc pressed: mode ...` or `Start pressed: mode ...` line for each press that reaches `Console.InputKey`, with
  what it did. "vanilla" means the key was left to the game.
- A Start press in the open console logs nothing. An Esc press there logs the console line of S5.

### Each pause

- `world paused in mode ...`, then `GUI sound InGameGuiEnter: played`;
- `overlay shown: world pause` (or `movie pause`), with `gamepad` or `keyboard` prompts and, where photo mode opens,
  `photo mode offered`. Later `overlay hidden`, or `overlay hidden at once (photo mode or the console)`;
- at the first pause only: `overlay: the game's fonts: ... used` (or a warning that Exo 2 stays), then `overlay: the
  game's texts: ...` as in S8. Japanese without the game's fonts gives "kept ours" for the labels too;
- after a key's `world resumed (...)`, `GUI sound InGameGuiExit: played`;
- LE2 and LE3, with the Steam overlay opened during a pause: `game audio released: the Steam overlay is open, the
  world still held`, then that sound, and `world pause ended ...` once the overlay closes.

### Photo mode

- `photo mode enter, mode ...`, with the view target, the shot and the subtitle;
- `photo mode on: ...`, with `pad prompts` after Y or `keyboard prompts` after F7. It gives the camera mode, and how
  far the controller was from the shot before Pause Anytime put it there. It also gives how far the view is from the
  shot, which should be 0 uu (Unreal units) and 0 deg;
- then `Y pressed ...: left photo mode, the world stays paused`, `photo mode exit: ignore counters ...`, and
  `photo mode left (Y) after ... s: mode ...` with the camera mode;
- the last of these lists the static binds that were set back, and the camera steps. It also counts what got past
  the pause: `N key and M axis event(s), G GUI tick(s) (0 changed the Pauser: put back), S GUI sound(s)`. Then it
  gives the frames on which the conversation panel was hidden again. M is in the thousands after moving a stick or the
  mouse, and G is about one per frame. In LE1, S is at least one per screenshot. LE2 and LE3 do not hook those
  sounds.

### Each movie

- its open line, with "from a file";
- `is a fullscreen movie`, with how long the HUD has been silent;
- `Console.InputKey runs while movie #N is open`, with the game mode during it;
- one `Esc pressed, read on thread ...` line for each press: `movie pause asked` or `movie resume asked`;
- Start in LE1: `Start held during a fullscreen movie, its press unseen here on thread ...` (or `released`), then
  `movie pause asked` or `movie resume asked`. Start in LE2 and LE3: `Start pressed on thread ...: movie pause asked`;
- the `PAUSED` and `RESUMED` lines, which in LE2 and LE3 end with `AKRESULT 1`, and the reports between them;
- once, `overlay: the paused movie presents nothing: its last frame is presented from its BinkWait, with the pause
  screen`. Where another thread presents the frames, a warning `overlay: the paused movie plays on thread ...` comes
  instead, and the pause screen shows only as the movie pauses;
- LE1: `Esc released during a fullscreen movie ...` lines, since the engine's movie input takes Esc's press;
- LE2 and LE3: `kept from the engine's movie input` for each Esc and Start press on a movie that Pause Anytime can
  pause, and `given to the engine's movie input` for Space and X (in LE2, `as Enter`);
- its close line.

During a movie pause, each report shows the same frame number and the number of `BinkWait` calls a second. If the
movie makes no Bink call for 2 s, the pause is dropped. When there are few calls a second, a short Esc press can fall
between two polls and be missed. While you hold a pause, the log should have no "has made no Bink call" line and no
"the HUD has drawn for 1 s" line.

### Skips

Every `SkipMovie` or `SkipCinematic` line, passed or dropped, and every dropped `GuiKey` line. A passed `GuiKey` is not
logged.

## Tested so far

Results per game, oldest first. Some test builds carried the next version's code under the old number, such as "v9's
code, numbered 8". The v8 results name the per-game zips of that time. A release now has one zip.

### v18 gate

v18 has not passed its gate yet. Its CHANGELOG entry names LE1, LE2 and LE3, so the full sheet is due in all three
games. S8 is due everywhere too: v18 gives LE3 the game's look, drops the dashes beside the band in all three games and
gives LE1 the title "PAUSE". None of the v18 runs below used the zip from the latest page, as the gate asks.

| Step | LE1 | LE2 | LE3 |
|---|---|---|---|
| S1, conversation | Not run | Partial, 2026-11-02 | Partial, 2026-10-31 |
| S2, cutscene | Not run | Not run | Partial, 2026-10-31 |
| S3, movie | Partial, 2026-11-01 | Partial, 2026-11-01 | Partial, 2026-11-01 |
| S4, gameplay | Not run | Not run | Not run |
| S5, resume keys and console | Not run | Partial, 2026-11-02 | Partial, 2026-10-31 |
| S6, photo mode | Not run | Not run | Not run |
| S7, the log | Not run | Not run | Not run |
| S8, the look | Not run | Not run | Partial, 2026-10-31 |
| S9, Steam overlay | Not for LE1 | Passed, 2026-11-02 | Passed, 2026-11-02 |

What the partial runs covered:

- LE1, S3: 11 Space resumes of one relay jump, on the build with the Space fix.
- LE2, S1 and S5: Esc and Space resumes in a Normandy conversation, during the S9 run.
- LE2, S3: `CitHub_Arrival` only, on the build with the Space fix: 20 Esc and Space cycles, 5 A resumes, and Space with
  nothing paused. Not run: the relay jump, Start, a 15 s hold, and X with nothing paused.
- LE3, S1, S2 and S5: 7 pauses, 6 in conversations and 1 in a cutscene, resumed with A, Esc and Start. Not run: Space as
  the resume key, and the console.
- LE3, S3: 15 Space resumes of one relay jump, on the build with the Space fix.
- LE3, S8: the band and its colours, the game's A and Y, its fonts and its label words. Not checked: the keycaps. The
  run came before the dashes beside the band were dropped (commit `89036bf`, 2026-11-01), so the check for no dash was
  not run either.

### LE1

| Build | Date | What ran | Result |
|---|---|---|---|
| v2 to v5 | 2026-10-28 | the sheet of the time, an Xbox pad for the pad steps | tested |
| v6 | | nothing: it changed only the build | |
| v7, first build | 2026-10-29 | full sheet, keyboard and mouse, Xbox pad | passed |
| v7, final build | 2026-10-29 | the sheet, keyboard and mouse | passed |
| v8, `LE1PauseAnytime-v8.zip` | | smoke sheet, keyboard and mouse, gamepad | passed |
| v9's code, numbered 8 (LE2 movie pause) | 2026-10-30 | S3, travel movie | passed |
| v10's code, numbered 9 (LE2 photo mode) | 2026-10-30 | S6, conversation and cutscene, Y and F7 | passed, one camera fault |
| v14, before its last fix | 2026-10-31 | look, words, sounds; French, Noveria conversation | passed |
| v14, final build | 2026-10-31 | play | no problem |
| v15, final build | 2026-10-31 | Start, the game's icons; Noveria conversation | passed |
| v16, final build | 2026-10-31 | Start and Esc, the game's fonts; French, Noveria conversation | passed |
| v17 | 2026-10-31 | gamepad and keyboard; French | passed |
| v18, the Space fix | 2026-11-01 | S3, 11 Space resumes of one relay jump | passed |

- v7: in the first build, Start in gameplay did what it does in the game without mods. The final build only renamed the
  file to `LE1PauseAnytime.asi`, and its gamepad steps were not run again.
- v8: Esc and Start worked in conversations and cutscenes. Start paused the travel movie and A resumed it. Y and F7
  opened photo mode.
- v9's code: Esc paused the travel movie, and Start resumed it after 8 s. Then Start paused it and A resumed it, twice.
  The log had no movie input hook line.
- v10's code: one entry came in the first second of a cutscene, while the camera still blended to the director's camera
  (the log's "pending" view target). It opened 51 units and 8 degrees off the shot, with "camera mode None" for the
  whole session, and the camera did not respond. Pause Anytime now holds that blend ("photo mode: the blend to ...
  held"). No test has seen the hold yet: 46 later entries in an STA00 cutscene had no blend pending, and all opened on
  the shot.
- v14, before its last fix (that fix puts the GUI sound depth back after a fault, and changes nothing without one):
  the look showed with the game's words of that build, "PAUSE", "Continuer" and "Mode photo", all three "used". Since
  v18 the title is Pause Anytime's own word. The menu swish played as the pause started and as Space resumed. The A and
  Y discs showed too.
- v15: several Start pauses in a row. The log has "the game's gamepad icons: A fine, Y fine: used" and no fault. A
  screenshot shows the game's own A and Y in place of v14's discs, at the same size and place, fading with the screen.
- v16: Start, then Esc. The log has "the game's fonts: ... used" and all three words "used", with no warning or fault.
  Screenshots show the title in Aero Light, centred on its band, the labels in BioMass, and the `Espace` and `F7`
  keycaps in Aero Light. Not run in the game: Russian, Japanese, the movie pause and the Exo 2 fallback. A headless
  test, with ImGui's asserts on, added the fonts between ImGui's frames in French, Russian and Japanese.
- v17: v16's look. The log has "LE1 look", the game's icons, fonts and three words "used", and both GUI sounds
  "played", with no warning.
- v18, the Space fix (a build still numbered 18): all 11 resumes worked, 9 of them with the movie's thread first. LE1
  has no movie input hook.

Over these runs:

- Esc pauses and resumes a conversation and an in-engine cutscene in sync. In gameplay and in the pause menu it does
  what it does in the game without mods.
- Esc pauses and resumes the travel movie. A 6.7 s pause held, with `BinkWait` still called 240 times a second.
- Start pauses and resumes a conversation and an in-engine cutscene.
- Start in the travel movie, over three trips: a tap paused it on its release, and the next tap resumed it. The release
  alone reached `Console.InputKey`, and the pause was applied at the next `BinkWait`, within 0 to 15 ms. The skip guard
  dropped a `SkipMovie` that the X button sent 160 ms after a Start release.
- Photo mode opens and closes in a conversation and in an in-engine cutscene. The camera moves and turns with the pad
  (Y) and with keyboard and mouse (F7, right button held). The prompts, tabs and options work, and so do screenshots
  with their click.
- The pause screen shows under the world pause and over the paused travel movie. It hides for photo mode and the
  console.
- Space and A resume in a conversation and in a cutscene, and A in the travel movie.

Not covered yet: a Start press held into its repeats during a movie, Esc and Start mixed during a movie, a PlayStation
pad, and a skippable fullscreen movie.

### LE2

| Build | Date | What ran | Result |
|---|---|---|---|
| none (L0) | | the Steam overlay without Pause Anytime | see below |
| v7 | 2026-10-29 | the sheet of the time, photo mode and movie pause off; keyboard and mouse, gamepad; three runs | passed |
| v8, `LE2PauseAnytime-v8.zip` | | the same sheet; keyboard and mouse, the pad's Start and A | passed |
| v9's code, numbered 8 | 2026-10-30 | S3; keyboard and mouse, gamepad | passed |
| v10's code, numbered 9 | 2026-10-30 | S6 in conversations; keyboard and mouse, gamepad; three runs | passed |
| v17 | 2026-10-31 | the look; French, Normandy conversation (`BioP_Nor`) | passed |
| v18, the Space fix | 2026-11-01 | S3, `CitHub_Arrival` | passed with the fix |
| v18 | 2026-11-02 | S9, Normandy conversation, by ear | passed |

- L0: the Steam overlay stops the picture, voice, lips and music. All of them resume together about 2 s after the
  overlay closes.
- v8: the keyboard run covered a conversation, a cutscene, the Space resume, and Esc elsewhere.
- v7 and v8, the pause: Esc and Start pause and resume a conversation and an in-engine cutscene in sync, and Space and A
  resume. The pause screen shows keyboard or gamepad prompts, and no photo key. One pause held 11 minutes, with the
  console opened and closed during it. Opening the console hides the pause screen at once. Esc or the console key closes
  it, and the pause holds.
- v7 and v8, other keys: with nothing paused, the skip keys skip a skippable cutscene, and the Esc or Start that paused
  or resumed one did not skip it. Elsewhere Esc and Start do what they do in the game without mods: in gameplay (the
  pause menu), in menus and on the galaxy map. Esc skips a loading movie as in the game without mods, and the world
  never paused under a movie. Y and F7 did what they do in the game without mods, also under the pause.
- v9's code, relay jump (`GLO_01_Relay`): Esc and Start pause the picture and the whoosh together, and resume them in
  sync (Wwise pause and resume: AKRESULT 1). Five pauses in one jump and a 15 s hold kept them in sync. Space does
  nothing, because the game does not let you skip this movie.
- v9's code, Citadel arrival (`CitHub_Arrival`): Esc and Start pause and resume it, with no skip. Space and X skip it,
  and no sound plays on. Under the pause, X and Enter skip nothing, and Space and A resume.
- v9's code, log: "movie input hooked" at start, "kept from the engine's movie input" for each Esc and Start press, and
  "given to the engine's movie input as Enter" for Space and X.
- v10's code fixes the two camera faults that the first runs showed. Y and F7 open photo mode on the paused shot, and
  Y, F7, Esc and Start leave it, still paused. The camera moves with the pad and with keyboard and mouse, and the panel
  responds. Photo mode's sounds play under the pause, with no hook.
- v10's code, camera: the first F7 of a pause in a director's shot opens facing the shot. The log has the controller
  24 degrees off before it was put back, and the view at 0. Re-entries and the resume keep the shot, with no jump.
- v17: the title in BioMass and gold on its brown band, and the labels in BioMass and orange. The log has "A fine, Y
  fine: used", "the game's fonts: ... used", and "Reprendre" and "Mode photo" both "used", with the title "kept ours".
- v17: a build without the texture addresses had both GUI sounds "played" and, as intended, "no view of the game's
  gamepad icons", so the drawn discs stayed. With the addresses, a screenshot shows the game's A and Y at the same place
  as the discs. Not checked on a screenshot or in the log: the keycaps, Russian, Japanese, the movie pause and the
  Exo 2 fallback.
- v18, before the Space fix: resuming `CitHub_Arrival` with Space skipped the movie. The movie's thread resumed it
  first, the game thread gave the same press to the movie input 4 ms later, and the movie closed at frame 145. The
  2026-10-30 test had the two threads in the other order, so it did not catch this.
- v18, with the Space fix (a build still numbered 18): 20 Esc and Space cycles over two arrivals, with no skip. 19 had
  the movie's thread first, 4 to 5 ms apart, and each press was kept from the movie input. One had the game thread
  first. A resumed 5 times with no skip, once after a 2 s hold. Space with nothing paused skipped the movie. Not run on
  that build: X with nothing paused.
- v18, S9: after Esc, Shift+Tab, a 2 s wait and Shift+Tab again, the scene resumed by itself and the line went on to its
  end. The log had "game audio released: the Steam overlay is open, the world still held" with one `InGameGuiExit`
  while the world was held, then "world pause ended", and no second `InGameGuiExit`. Resumes with Esc and Space each
  played one `InGameGuiExit`. Before the fix, the same keys lost the rest of the line, in two runs.

Not covered yet: the Kasumi DLC's montage ([games/LE2.md](games/LE2.md)), a PlayStation pad, a movie with no known sound
(where Esc must skip the movie as in the game without mods), the Prologue's FirstFlight movie (level music plays beside
it), `visionavi` (which has Bink audio), and photo mode in an in-engine cutscene.

### LE3

| Build | Date | What ran | Result |
|---|---|---|---|
| first LE3 build (v10's code, photo mode and movie pause off) | 2026-10-30 | part of the sheet, Normandy and Citadel; keyboard and mouse, gamepad | passed |
| photo mode builds, up to v12 | 2026-10-30 | photo mode under the pause, Normandy and Citadel; keyboard and mouse, gamepad | passed in v12 |
| v12 | 2026-10-30 | the console under the pause, by hand | works |
| v13's code | 2026-10-30 | S3, keyboard, by hand | passed |
| v18 | 2026-10-31 | the look; French, Normandy (`BioP_Nor`) and Citadel (`BioP_CitHub`), by hand | passed |
| v18, the Space fix | 2026-11-01 | S3, 15 Space resumes of one relay jump | passed |
| v18 | 2026-11-02 | S9, Citadel conversation, by ear | passed |

- First LE3 build: 23 pauses with Esc and Start (19 in conversations, 4 in cutscenes), with picture, voice, lips and
  music in sync. Esc and Space resume. Start pauses with gamepad prompts, and A resumes.
- First LE3 build, skips: LE3's cutscene mode binds Esc and Space to the skip (`PC_CineSkip`), and X too
  (`Console_CineSkip`). The Esc that paused or resumed a cutscene did not skip it.
- First LE3 build: Esc and Start elsewhere do what they do in the game without mods: in gameplay (the pause menu), in
  menus, on the galaxy map, and in the game's own photo mode. The log has the start lines of that time,
  "LE3PauseAnytime 10 ready", the first `BioHUD.PostRender` with the same controller, and the text in French.
- The first photo build (input hooks, no GUI tick hook): Y and F7 open and leave photo mode in a conversation and in a
  cutscene, and the camera responds to the mouse, the keys and the sticks. The panel never loads: its labels read "TEST
  STRING", and the reply wheel and the subtitle stay on screen. The log counts 0 GUI ticks.
- With `GfxClock` on the GFx movies' `Tick`: the panel loads and responds (tabs, options). The conversation movie is set
  aside each time ("set aside" line). With the reply wheel up at the entry, the wheel came back now and then while
  photo mode ran.
- v12, with `PhotoGui::Hold` each frame: in 35 photo sessions, with the wheel up or not, the wheel stays hidden. The
  game showed the movie again in 20 of them, on 25 to 170303 frames (one long session), and each time it was hidden
  again. The log has no warning or error.
- v12, in an in-engine cutscene on Omega: 4 photo sessions on a `CameraActor`'s shot, in game mode 7 (Conversation).
  The panel loads and responds, with 435 to 18877 GUI ticks, one per frame. The conversation movie was set aside and
  never shown again. Left with F7 and Esc.
- v13's code, relay jump (`GLO_01_Relay`), 2 jumps: the latch named its event ("id 2556726104 (hash of
  Play_cineanim_massrelay 2556726104)"). The movie opened from a file and was fullscreen 0.2 s after its open. 8 Esc
  cycles each logged "PAUSED in BinkWait ... AKRESULT 1" and "RESUMED ... AKRESULT 1". The frame held, the pause screen
  showed over it ("presented from its BinkWait"), and nothing skipped. The longest hold was 2.3 s.
- v13's code: Space with nothing paused gave 3 presses "given to the engine's movie input: not taken", and the relay
  played to its end. During the relay the log had "modes on 0 8 9 10 11", the cutscene and movie modes on top of the
  galaxy map's. The player's flags were 0x400 (keys ignored by the movie input), skippable 0.
- v13's code, loading screens at the Citadel arrival: these movies were opened by file handle (player flags 0x81). They
  had "no known sound", and Esc stayed the game's. A movie opened from memory during the arrival conversation was
  refused, and Esc paused the world as in v12.
- v18, the look: the title in BioMass on the blue band, the labels in pale cyan, and the game's A and Y. The log has
  "LE3 look", "loaded their package in 31 ms" at the main menu, "A fine, Y fine: used", both fonts "used" and both
  labels "used", with no warning. 7 pauses (6 in conversations, 1 in a cutscene), 5 with gamepad prompts and 2 with
  keyboard prompts, resumed with A, Esc and Start. Each played `InGameGuiEnter` and `InGameGuiExit`. Not run on that
  build: Space as the resume key, photo mode under the pause, a movie pause, Russian, Japanese and the Exo 2 fallback.
- v18, the Space fix (a build still numbered 18): in all 15 resumes, the game thread saw the press before the movie's
  thread did. That order is safe (the skip in LE2 came from the other order), so the new check of the fix never ran.
  The relay cannot be skipped, and it played on.
- v18, S9: the line went on to its end. Each of two pauses had "game audio released: the Steam overlay is open, the
  world still held", with one `InGameGuiExit` while the world was held. In the second pause, Esc with the overlay open
  logged "still paused by ..." and posted no second `InGameGuiExit`. This log has no plain key resume; LE2 ran that path
  on the same build.

Not covered yet: photo mode's GUI sounds, a movie inside a cutscene or a conversation, a 15 s movie hold, Start and A in
a movie, the skips with nothing paused, and L0.
