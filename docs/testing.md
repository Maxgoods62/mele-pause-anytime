# Testing Pause Anytime in the game

Each game is tested with its own build. Close the game, then build and install with
`./build.sh --game LE<n> --install` ([BUILDING.md](../BUILDING.md)). `--install` needs the ASI loader in that game:
`bink2w64_original.dll` in `Game\ME<n>\Binaries\Win64`, which ME3Tweaks Mod Manager installs with any mod. Check that
the game's ASI folder holds `LE<n>PauseAnytime.asi`.

Then run the sheet below in that game and read the log, `<LE folder>\Game\ME<n>\Binaries\Win64\Logs\PauseAnytime.log`.
Pause Anytime rewrites the log each time the game starts.

Keep the game's window in front for the whole test. In the background, LE1 holds the world still every frame, and a
pause tested then proves nothing.

## Release gate

Before you tag a version, test each game on the `Games:` line of its CHANGELOG entry. Use the zip from the latest page,
which `package.sh` builds ([releasing.md](releasing.md#publish-a-version), step 3).

- Full sheet: S1 to S8 in every game, and S9 in LE2 and LE3. Run it once with a gamepad and once with keyboard and
  mouse. It is due in each game that the version adds or reworks: a new feature, moved code, or a new file name.
- Smoke sheet: S1, S3 and S6, in every other game that the version covers. The code is shared, so a change made for
  one game can break another.

S8, the look, is part of the full sheet, because a version can change the look in every game. v18 does: LE3 gets the
game's look, and in all three games the short dash left and right of the title's band is gone. L0 is a one-time check
outside the gate.

## Sheet

Run each step with keyboard and mouse and with a gamepad. The steps name LE1's scenes. A note that starts with a
game's name (LE1, LE2, LE3) holds that game's scenes and what differs there.

- L0, the Steam overlay without Pause Anytime: once in LE2 and once in LE3, outside the gate.
  - Before you install Pause Anytime, open the Steam overlay (Shift+Tab) for 15 s during a conversation line, then
    during an in-engine cutscene.
  - Note whether voice, lips and music stop together and resume in sync. In LE1 and LE2 the overlay sets the world
    pause that Pause Anytime uses (`WorldInfo.Pauser`).
- S1, conversation.
  - During a spoken line, press Esc. Picture, voice, music and lips stop, and the game's menu sound plays.
  - The picture blurs and darkens. The title stands at the centre, on a band in the game's menu colours.
  - The keys stand left of their labels at the bottom left. The labels are in the game's language: "Resume" and
    "Photo Mode" in English. In French they are "Mode photo", with "Continuer" in LE1 and "Reprendre" in LE2 and LE3.
  - In LE1 the title is the game's word too: "PAUSED" in English, "PAUSE" in French. In LE2 and LE3 the title is
    Pause Anytime's own.
  - Wait a few seconds, then press Esc. The line resumes at the same word, in sync. The picture is sharp again, and the
    menu sound plays again.
  - Do the same with Start on a gamepad. The pause screen shows the A icon, and the Y icon where photo mode opens. They
    are the game's own pictures, with no dark box around them, and they fade in and out with the screen.
- S2, in-engine cutscene, such as the Noveria undocking scene. Do as in S1: Esc, wait, Esc, then Start, wait, Start.
  - LE2 and LE3: Esc, Space and the pad's X are the cutscene's skip keys. The Esc that pauses and the Space that
    resumes must not skip the cutscene.
  - LE2: with nothing paused, Space and X skip a skippable cutscene as in the game without mods.
  - LE3: with nothing paused, note what Esc, Space and X do in a skippable cutscene. The log names each
    `SkipCinematic` it passes.
- S3, fullscreen movie: the galaxy map travel movie, on a trip within a cluster or through a relay.
  - Half a second into the movie, press Esc. The movie stops, picture and sound. Wait a few seconds, then press Esc:
    the movie resumes and plays to its end.
  - Keep a pause for 15 s or more: the movie must stay paused. The reports while paused say how long the game thread
    has been silent.
  - LE1, with a gamepad: a Start tap pauses the movie when you let go, and a held Start pauses it about 0.2 s after
    the press. Start again resumes.
  - On another trip, press Space during the movie and note whether it skips the movie.
  - If Esc does nothing, the log shows why, in one of two ways:
    - there is no "is a fullscreen movie" line for this movie, and its close line says "never a fullscreen movie
      (...)";
    - there is an "Esc pressed, read on thread ...: not taken, ..." line.
  - LE2: use the relay jump and the Citadel arrival or departure. Esc pauses the picture and the sound together, and
    Esc resumes them in sync. Do the same with Start. A resumes too, and Space resumes the Citadel movie without
    skipping it. Pause five times in one movie and hold one pause for 15 s: picture and sound must stay in sync.
  - LE2: with nothing paused, Space and X skip the Citadel movie and do nothing in the relay jump. Under the pause, X,
    Enter and A skip nothing. The world never pauses under a movie.
  - LE2: in a movie with no known sound ("no known sound" in its fullscreen line), Esc skips the movie as in the game
    without mods, if the game lets you skip it. In the Kasumi DLC's montage, note what Esc and Start do
    ([games/LE2.md](games/LE2.md#open-questions), Matinee-track movies).
  - LE3: use the relay jump (galaxy map, to another cluster). Esc and Start pause the picture and the whoosh together,
    under the pause screen. Esc, Start, Space or A resumes them in sync. Run 5 cycles and one 15 s hold.
  - LE3: with nothing paused, Space and X skip nothing in the relay jump, which is not skippable.
  - LE3: in a loading screen, Esc and Start do what they do in the game without mods, and the world does not pause
    under the movie. In a movie inside a cutscene or a conversation, note what Esc and Start do and whether the world
    pauses. That case is not tested yet ([games/LE3.md](games/LE3.md#open-questions)).
- S4, gameplay. Esc still opens the pause menu, and closes it. The same with Start.
- S5, resume keys and the console.
  - In a conversation, pause with Esc and resume with Space. Then pause with Start and resume with A.
  - Pause with Esc and resume with Start, and the other way round.
  - With nothing paused, Space skips the line as in the game without mods.
  - Open the console during a pause. The pause screen hides, and Esc closes the console as in the game without mods.
    The log has "Esc pressed in ...: the console is open, vanilla".
  - With a PlayStation pad through Steam Input, Options does what Start does.
- S6, photo mode.
  - In a conversation, press Start, then Y. Expected: the same shot, sharp, with no wheel, no subtitle and no pause
    screen. Photo mode's controls are at the bottom, with the pad's buttons.
  - Move with both sticks and LT/RT. B hides and shows the options, and their tabs switch with the buttons the panel
    shows.
  - X takes a screenshot: a click, and a new file in `Documents\BioWare\Mass Effect Legendary Edition\ScreenShots`
    with no pause screen in it.
  - Y goes back to the paused shot, with the wheel, the subtitle and the pause screen. Y again, then Start: back to
    the paused shot too.
  - Start once more: the conversation resumes at the same word. Picking a reply with the stick works, with a southpaw
    profile too.
  - Do the same in an in-engine cutscene, and with Esc and F7 on the keyboard. Hold the right mouse button to look.
  - Y in gameplay, or in a conversation that is not paused, does what it does in the game without mods.
  - LE2: in a director's shot, the first Y or F7 of a pause opens photo mode facing the shot. The "photo mode on" line
    gives 0 uu and 0 deg for the view. The clicks and the screenshot sound play.
  - LE3: do it with the reply wheel up and without. The panel shows its tabs and options, never "TEST STRING", and the
    wheel stays hidden until you leave photo mode.
- S7, the log. Read it against [What the log should show](#what-the-log-should-show), the photo mode counters
  included.
  - LE2 and LE3: each press logs that game's mode numbers and names, such as "Esc pressed: mode 5 (Conversation)" in
    LE2 and "Esc pressed: mode 7 (Conversation)" in LE3.
- S8, the look.
  - Each key stands left of its label, and the keyboard's keys are grey keycaps. With a gamepad, the game's A and Y
    sit at the height of their labels, not drawn discs. There is no dash left or right of the title's band.
  - LE1: the title in pale cyan and Aero Light, on a dark blue band with a light blue rule along each edge. The labels
    in blue and BioMass.
  - LE2: the title in gold and BioMass, on a brown band with an orange rule along each edge. The labels in orange and
    BioMass.
  - LE3: the title in white and BioMass, on a blue band with a bright blue rule along each edge. The labels in pale
    cyan and BioMass.
  - The log has "overlay ready: ..., LE<n> look", "overlay: the game's gamepad icons: A fine, Y fine: used" and
    "overlay: the game's fonts: ... used". The "overlay: the game's texts: ..." line has "used" after each word; in
    LE2 and LE3 it also has `title "" kept ours`. "GUI sound InGameGuiEnter: played" follows "world paused".
  - LE3: "the game's fonts: loaded their package in ... ms: SFXGUI_Fonts.ObjectReferencer_448" at the main menu.
- S9, the Steam overlay, in LE2 and LE3.
  - During a long spoken line, press Esc, then Shift+Tab to open the overlay. Wait 2 s, then press Shift+Tab again to
    close it. Press no other key.
  - The game's menu sound plays as the overlay opens. About 2 s after the overlay closes, the scene resumes by itself,
    and the line goes on to its end.
  - The log has "game audio released: the Steam overlay is open, the world still held" with one "GUI sound
    InGameGuiExit: played", then "world pause ended".

## What the log should show

- At start:
  - a "Bink: ... hooked" line, with the movie pause "on (Esc read in BinkWait)";
  - LE2 and LE3: "movie sound: the exe's AK::SoundEngine::ExecuteActionOnEvent at ..." and "movie input hooked: ...";
  - "input hooked: ...", "GUI tick hooked: ..." and "GUI sounds hooked: ...", or a "not found at RVA" warning in
    place of one of them. LE2 and LE3 have "GUI sounds: not hooked in the LE<n> build, ..." instead of the third;
  - "overlay: swap chain hooks Present true ...";
  - a line that ends in "LE<n>PauseAnytime N ready", where N is the build's `ASI_VERSION`.
- At the first frame:
  - "overlay ready: back buffer ..., LE1 look (LE2 look, LE3 look), text in <language>, Exo 2 embedded, blur ready";
  - "first BioHUD.PostRender": the HUD owner and GEngine's controller should be the same;
  - "the game's string table: title ..., resume ..., photo mode ..." with the words in the game's language. LE2 and
    LE3 have no title there;
  - "overlay: the game's gamepad icons: A fine, Y fine: used". A view that failed its checks gives "refused" and "ours
    drawn instead". Textures that gave no view give "no view of the game's gamepad icons";
  - LE3: "the game's fonts: loaded their package in ... ms";
  - a line with "the pause screen keeps Exo 2" if the game's fonts could not be read.
- Each press that reaches `Console.InputKey`: one "Esc pressed: mode ..." or "Start pressed: mode ..." line, with what
  it did.
- Each pause:
  - "overlay shown: world pause" (or "movie pause"), with "gamepad" or "keyboard" prompts, then "overlay hidden"
    ("at once" for photo mode);
  - at the first pause only, "overlay: the game's fonts: ... used" ("Exo 2 stays" if not), then "overlay: the game's
    texts: ..." with "used" after each word. Japanese without the game's fonts gives "kept ours";
  - after "world paused", "GUI sound InGameGuiEnter: played";
  - after a key's "world resumed", "GUI sound InGameGuiExit: played";
  - LE2 and LE3, with the Steam overlay opened during a pause: "game audio released: the Steam overlay is open, the
    world still held", then that sound, and "world pause ended" once the overlay closes.
- Photo mode:
  - "photo mode enter, mode ...", with the view target, the shot and the subtitle;
  - "photo mode on: ...", with "pad prompts" after Y or "keyboard prompts" after F7. It gives the camera mode, and how
    far the controller was from the shot before Pause Anytime moved it there. It also gives how far the view is from
    the shot, which should be 0 uu (Unreal units) and 0 deg;
  - then "Y pressed ...: left photo mode", "photo mode exit: ignore counters ...", and "photo mode left (Y) after ...
    s: mode 5 (Conversation), camera mode ...";
  - the last of these gives the number of static binds set back and of camera steps. It also gives what got past the
    pause: "N key and M axis event(s), G GUI tick(s) (0 changed the Pauser: put back), S GUI sound(s)". M is in the
    thousands after moving a stick or the mouse, and G is about one per frame. In LE1, S is at least one per
    screenshot. LE2 and LE3 do not hook those sounds.
- A movie:
  - its open line ("from a file");
  - "is a fullscreen movie", with the HUD's silence;
  - "Console.InputKey runs while movie #N is open", with the game mode during it;
  - one "Esc pressed, read on thread ..." line per press (movie pause or resume asked);
  - the PAUSED and RESUMED lines, and the reports between them;
  - "Esc released during a fullscreen movie ..." lines;
  - LE2 and LE3: "kept from the engine's movie input" for each Esc and Start press on a movie that Pause Anytime can
    pause, and "given to the engine's movie input" for Space and X (LE2: "as Enter");
  - its close line.
- Start in a movie: "Start held (or released) during a fullscreen movie, its press unseen here on thread ...: movie
  pause asked" (or "resume asked").
- Skips: any `SkipMovie` or `SkipCinematic` line (dropped or passed), and any dropped `GuiKey` line. A passed `GuiKey`
  is not logged.

During a movie pause, the reports show the same frame number each time, and the number of `BinkWait` calls per
second. If there are no calls for 2 s, the pause is dropped. Below about 10 calls a second, a short Esc press can fall
between two polls and be missed. While you hold a pause, the log should have no "has made no Bink call" line and no
"the HUD has drawn for 1 s" line.

## Tested so far

Results per game, oldest first.

### v18 gate

v18 has not passed its gate yet. Its CHANGELOG entry names LE1, LE2 and LE3, so the full sheet is due in all three
games. None of the v18 runs recorded below used the zip from the latest page, which the gate asks for.

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
- LE2, S3: `CitHub_Arrival` only, on the build with the Space fix. 20 Esc and Space cycles, 5 A resumes, and Space
  with nothing paused. Not run: the relay jump, Start, a 15 s hold, and X with nothing paused.
- LE3, S1, S2 and S5: 7 pauses, 6 in conversations and 1 in a cutscene, resumed with A, Esc and Start. Not run: Space
  as the resume key and the console.
- LE3, S3: 15 Space resumes of one relay jump, on the build with the Space fix.
- LE3, S8: the band and its colours, plus the game's A and Y, fonts and label words. Not checked: the keycaps. The
  run came before the dashes beside the band were dropped (commit `89036bf`, 2026-11-01), so the check for no dash was
  not run either.

### LE1

- v2 to v5, 2026-10-28: tested with an Xbox pad for the gamepad steps. v6 changed only the build.
- v7, 2026-10-29: a first build passed the full sheet with keyboard and mouse and with an Xbox pad. Start in gameplay
  did what it does in the game without mods. The final v7 build, which only renamed the file to `LE1PauseAnytime.asi`,
  passed the sheet with keyboard and mouse; the gamepad steps were not run again.
- v8, the file from `LE1PauseAnytime-v8.zip`: passed the smoke sheet with keyboard and mouse and with a gamepad. Esc and
  Start worked in conversations and cutscenes, the travel movie paused with Start and resumed with A, and photo mode
  opened with Y and F7.
- A test build of the LE2 movie pause (v9's code, still numbered 8), 2026-10-30: passed S3 in the travel movie. Esc
  paused it, Start resumed it after 8 s, then Start paused it and A resumed it, twice. The log had no movie input hook
  line.
- A test build of LE2 photo mode (v10's code, still numbered 9), 2026-10-30: passed S6 in a conversation and in an
  in-engine cutscene, with Y and F7.
  - One entry was pressed in the first second of a cutscene, while the camera still blended from the player to the
    director's camera (the log's "pending" view target). It opened 51 units and 8 degrees off the shot, with "camera
    mode None" for the whole session: the camera did not respond.
  - Pause Anytime now holds that blend for the session ("photo mode: the blend to ... held" in the log). That hold has
    not been seen in the game yet: 46 later entries in an STA00 cutscene had no blend pending, and all opened on the
    shot.
- v14 before its last fix, 2026-10-31, in French, in a Noveria conversation. The last fix puts the GUI sound depth
  back after a fault. Without a fault, it changes nothing.
  - The LE1 look showed with the game's words: "PAUSE", "Continuer" and "Mode photo", all three "used" in the log.
  - The menu swish played as the pause started and as Space resumed. The gamepad prompts (A and Y discs) showed too.
- v14, the final build, 2026-10-31: ran in the game with no problem.
- v15, the final build, 2026-10-31, in a Noveria conversation: paused with Start several times in a row. The log has
  "the game's gamepad icons: A fine, Y fine: used" and no fault. A screenshot shows the game's own A and Y in place of
  v14's discs, at the same size and place. They fade with the screen.
- v16, the final build, 2026-10-31, in French, in a Noveria conversation: paused with Start, then with Esc.
  - The log has "the game's fonts: ... used" and all three words "used", with no warning and no fault.
  - Screenshots show the title in Aero Light, centred on its band, the labels in BioMass, and the `Espace` and `F7`
    keycaps in Aero Light.
  - Not run in the game: Russian, Japanese, the movie pause and the fallback to Exo 2. A headless test, with ImGui's
    asserts turned on, added the fonts between ImGui's frames in French, Russian and Japanese.
- v17, 2026-10-31, in French: paused with a gamepad and with the keyboard. The look is v16's. The log has "LE1 look",
  the game's icons and fonts "used", all three words "used", and both GUI sounds "played", with no warning.
- v18, the build with the Space fix (still numbered 18), 2026-11-01: 11 Space resumes of one relay jump worked, 9 of
  them with the movie's thread first. LE1 has no movie input hook.

Over these runs:

- Esc in a conversation and in an in-engine cutscene: pauses and resumes in sync. Esc in gameplay and in the pause
  menu does what it does in the game without mods.
- Esc in the travel movie: pauses and resumes. A 6.7 s pause held, with `BinkWait` still called 240 times a second
  while paused.
- Start in a conversation and in an in-engine cutscene: pauses and resumes.
- Start in the travel movie: over three trips, a tap paused it on its release, and the next tap resumed it. The release
  alone reached `Console.InputKey`, and the pause was applied at the next `BinkWait`, within 0 to 15 ms. The skip guard
  dropped a `SkipMovie` that the X button sent 160 ms after a Start release.
- Photo mode: entry and exit in a conversation and in an in-engine cutscene. The camera moves and turns with the pad
  (Y) and with keyboard and mouse (F7, right button held). The prompts, tabs and options work, and so do screenshots
  with their click.
- The pause screen shows under the world pause and over the paused travel movie. It hides for photo mode and the
  console.
- Space and A resume in a conversation, in a cutscene, and in the travel movie (A).

Not covered by a recorded test yet: a Start press held into its repeats during a movie, Esc and Start mixed during a
movie, a PlayStation pad, and a skippable fullscreen movie.

### LE2

- L0, before Pause Anytime was installed: the Steam overlay stops the picture, voice, lips and music. All of them
  resume together about 2 s after the overlay closes.
- v7, 2026-10-29, with photo mode and the movie pause off: passed the sheet of that time with keyboard and mouse and
  with a gamepad, over three runs.
- v8, the file from `LE2PauseAnytime-v8.zip`: passed it again with keyboard and mouse and with the gamepad's Start and
  A. The keyboard run covered a conversation, a cutscene, the Space resume, and Esc elsewhere.

Over the v7 and v8 runs:

- Esc and Start in a conversation and in an in-engine cutscene: pause and resume in sync. Space and A resume. The
  pause screen shows keyboard or gamepad prompts and no photo key. One pause held 11 minutes, with the console opened
  and closed during it.
- Skips: with nothing paused, the skip keys skip a skippable cutscene. The Esc or Start that paused or resumed a
  cutscene did not skip it.
- Esc and Start elsewhere do what they do in the game without mods: in gameplay (the pause menu), in menus and on the
  galaxy map. Esc skips a loading movie as in the game without mods, and the world never paused under a movie.
- Console: opening it hides the pause screen at once. Esc or the console key closes it, and the pause holds.
- Y and F7 do what they do in the game without mods.

Later runs:

- A test build of the movie pause (v9's code, still numbered 8), 2026-10-30: passed S3 with keyboard and mouse and
  with a gamepad.
  - Relay jump (`GLO_01_Relay`): Esc and Start pause the picture and the whoosh together, and resume them in sync
    (Wwise pause and resume: AKRESULT 1). Five pauses in one jump and a 15 s hold kept picture and sound in sync.
    Space does nothing: the game does not let you skip this movie.
  - Citadel arrival (`CitHub_Arrival`): Esc and Start pause and resume it, with no skip. Space and X skip it, and no
    sound plays on. Under the pause, X and Enter skip nothing, and Space and A resume.
  - The log has "movie input hooked" at start, "kept from the engine's movie input" for each Esc and Start press, and
    "given to the engine's movie input as Enter" for Space and X.
- A test build of photo mode (v10's code, still numbered 9), 2026-10-30: passed S6 in conversations, with keyboard and
  mouse and with a gamepad, over three runs. It fixes the two camera faults that the first runs showed
  ([how-it-works.md](how-it-works.md#photo-mode-entering), step 7).
  - Entry and exit: Y and F7 open photo mode on the paused shot. Y, F7, Esc and Start leave it, still paused. The
    camera moves with the pad and with keyboard and mouse, and the panel responds.
  - Camera: the first F7 of a pause in a director's shot opens facing the shot. The log has the controller 24 degrees
    off before it was put back, and the view at 0. Re-entries and the resume keep the shot, with no jump.
  - Sounds: photo mode's sounds play under the pause, with no hook.
- v17, 2026-10-31, in French, in a Normandy conversation (`Biop_Nor`): the LE2 look showed. The title was in BioMass
  and gold on its brown band, and the labels in BioMass and orange.
  - The log has "overlay: the game's gamepad icons: A fine, Y fine: used", "overlay: the game's fonts: ... used", and
    "Reprendre" and "Mode photo" both "used", with the title "kept ours".
  - A build without the texture addresses had both GUI sounds "played" and, as intended, "no view of the game's
    gamepad icons": the drawn discs stayed. With the addresses, a screenshot shows the game's A and Y at the same place
    as the discs.
  - Not checked on a screenshot or in the log: the keycaps, Russian, Japanese, the movie pause and the fallback to
    Exo 2.
- v18, the build with the Space fix (still numbered 18), 2026-11-01.
  - Before the fix, resuming `CitHub_Arrival` with Space skipped the movie on v18. The movie's thread resumed the
    movie first, then the game thread gave the same press to the movie input 4 ms later, and the movie closed at frame
    145. The 2026-10-30 test had the two threads in the other order, so it did not catch this.
  - The build with the fix ran 20 Esc and Space cycles over two arrivals, with no skip. 19 had the movie's thread
    first, 4 to 5 ms apart, and each was kept from the movie input. One had the game thread first.
  - A resumed 5 times with no skip, one of them after a 2 s hold. Space with nothing paused skipped the movie. Not run
    on that build: X with nothing paused.
- v18, 2026-11-02: S9 passed in a Normandy conversation, by ear. After Esc, Shift+Tab, a 2 s wait and Shift+Tab
  again, the scene resumed by itself and the line went on to its end.
  - The log had "game audio released: the Steam overlay is open, the world still held" with one `InGameGuiExit` while
    the world was held, then "world pause ended", and no second `InGameGuiExit`.
  - Resumes with Esc and Space each played one `InGameGuiExit`. Before the fix, the same keys lost the rest of the
    line, in two runs.

Not covered yet:

- the Kasumi DLC's montage ([games/LE2.md](games/LE2.md#open-questions), Matinee-track movies);
- a PlayStation pad;
- a movie with no known sound, where Esc must skip the movie as in the game without mods;
- the Prologue's FirstFlight movie, where level music plays beside it;
- `visionavi`, which has Bink audio;
- photo mode in an in-engine cutscene.

### LE3

- The first LE3 build (v10's code, with photo mode and the movie pause off), 2026-10-30: passed part of the sheet on
  the Normandy and the Citadel, with keyboard and mouse and with a gamepad.
  - Esc and Start in a conversation and in an in-engine cutscene: 23 pauses (19 in conversations, 4 in cutscenes),
    with picture, voice, lips and music in sync. Esc and Space resume. Start pauses with gamepad prompts, and A
    resumes.
  - Skips: LE3's cutscene mode binds Esc and Space to the skip (`PC_CineSkip`), and X too (`Console_CineSkip`). The
    Esc that paused or resumed a cutscene did not skip it.
  - Esc and Start elsewhere do what they do in the game without mods: in gameplay (the pause menu), in menus, on the
    galaxy map, and in the game's own photo mode.
  - The log has the start lines that the sheet lists, "LE3PauseAnytime 10 ready", the first `BioHUD.PostRender` with the
    same controller, and the text in French.
- Photo mode under the pause, 2026-10-30, on the Normandy and the Citadel, with keyboard and mouse and with a gamepad:
  - The first photo build (input hooks, no GUI tick hook): Y and F7 open and leave photo mode in a conversation and in
    a cutscene. The camera responds to the mouse, the keys and the sticks. The panel never loads: its labels read
    "TEST STRING", and the reply wheel and the subtitle stay on screen. The log counts 0 GUI ticks.
  - With `GfxClock` on the GFx movies' `Tick`: the panel loads and responds (tabs, options). The conversation movie is
    set aside each time ("set aside" line). With the reply wheel up at the entry, the wheel came back now and then
    while photo mode ran.
  - v12, with `PhotoGui::Hold` each frame: in 35 photo sessions, with the wheel up or not, the wheel stays hidden. The
    game showed the movie again in 20 of them, on 25 to 170303 frames (a long session). Each time it was hidden again.
    The log has no warning or error.
  - v12, in an in-engine cutscene on Omega: 4 photo sessions on a `CameraActor`'s shot, in game mode 7
    (Conversation). The panel loads and responds, with 435 to 18877 GUI ticks, one per frame. The conversation movie
    was set aside and never shown again. Left with F7 and Esc.
- v12, 2026-10-30: the console works under the pause (played by hand).
- A test build of the movie pause (v13's code), 2026-10-30, with the keyboard. Played by hand, it worked well.
  - Relay jump (`GLO_01_Relay`), 2 jumps: the latch named its event ("id 2556726104 (hash of Play_cineanim_massrelay
    2556726104)"). The movie opened from a file and was fullscreen 0.2 s after its open. 8 Esc cycles each logged
    "PAUSED in BinkWait ... AKRESULT 1" and "RESUMED ... AKRESULT 1". The frame held, the pause screen showed over the
    paused frame ("presented from its BinkWait"), and nothing skipped. The longest hold was 2.3 s.
  - Space with nothing paused: 3 presses "given to the engine's movie input: not taken". The relay played to its end.
  - Modes: "modes on 0 8 9 10 11" during the relay, that is the cutscene and movie modes on top of the galaxy map's.
    The player's flags were 0x400 (keys ignored by the movie input), skippable 0.
  - Loading screens, Citadel arrival: these movies were opened by file handle (player flags 0x81). They had "no known
    sound", and Esc stayed the game's. A movie opened from memory during the arrival conversation was refused, and
    Esc paused the world as in v12.
- v18, 2026-10-31, in French, on the Normandy (`BioP_Nor`) and the Citadel (`BioP_CitHub`): the LE3 look showed. The
  title was in BioMass on the blue band, with the labels in pale cyan and the game's A and Y. Played by hand, it
  worked well.
  - The log has "LE3 look", "loaded their package in 31 ms" at the main menu, "A fine, Y fine: used", both fonts
    "used" and both labels "used", with no warning.
  - 7 pauses (6 in conversations, 1 in a cutscene), 5 with gamepad prompts and 2 with keyboard prompts, resumed with
    A, Esc and Start. Each played `InGameGuiEnter` and `InGameGuiExit`.
  - Not run on that build: Space as the resume key, photo mode under the pause, a movie pause, Russian, Japanese and
    the Exo 2 fallback.
- v18, the build with the Space fix (still numbered 18), 2026-11-01: in all 15 Space resumes of one relay jump, the
  game thread saw the press before the movie's thread did. That order is safe: the skip in LE2 came from the other
  order. So the fix's new check never ran. The relay cannot be skipped, and it played on.
- v18, 2026-11-02: S9 passed in a Citadel conversation, by ear: the line went on to its end.
  - Each of two pauses had "game audio released: the Steam overlay is open, the world still held", with one
    `InGameGuiExit` while the world was held.
  - In the second pause, Esc with the overlay open logged "still paused by ..." and posted no second `InGameGuiExit`.
  - A plain key resume is not in that log. LE2 ran that path on the same build.

Not covered yet:

- photo mode's GUI sounds;
- a movie inside a cutscene or a conversation;
- a 15 s movie hold;
- Start and A in a movie;
- the skips with nothing paused;
- L0, the Steam overlay without Pause Anytime.
