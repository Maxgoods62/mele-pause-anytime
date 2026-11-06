# Troubleshooting Pause Anytime

## Read the log first

Pause Anytime writes a log at `<LE folder>\Game\ME<n>\Binaries\Win64\Logs\PauseAnytime.log`, where `<n>` is the
game's number. It rewrites the file each time the game starts, so copy it before you start the game again.

- A line that ends in `LE<n>PauseAnytime N ready` means that Pause Anytime loaded. N is the version. The line is there
  even when a part of Pause Anytime turned itself off, so read the warnings above it too.
- No log, or a log from before you started the game, means that Pause Anytime did not load.
- Each press of Esc or Start that reaches Pause Anytime adds a line with `Esc pressed` or `Start pressed`. The line
  says what Pause Anytime did, such as `paused the world` or `resumed the world`. `vanilla` there means that it left
  the key to the game, as in the game without mods.
- A pause adds `world paused in mode ...`, and its end adds `world resumed ...` or `world pause ended ...`.
- Lines marked `warning` or `error` say what went wrong and what Pause Anytime turned off.

Warnings and errors reach the file at once, and other lines within a few seconds. After a crash, the last seconds of
the log can be missing.

## Common problems

| Problem | Likely cause | What to do |
|---|---|---|
| Nothing pauses, and there is no new `PauseAnytime.log` | No ASI loader, or the file is in the wrong folder | Check both ([install.md](install.md#before-you-start)). Each file works only in its own game. |
| Esc opens the pause menu, or does nothing | The game was not in a conversation or a cutscene | The `Esc pressed` line names the game's mode. Pause Anytime pauses only in a conversation or a cutscene. |
| A movie does not pause | Pause Anytime cannot pause that movie | See [which movies pause](using.md#movies). |
| Esc closes the console | The console was open | Esc closes it, as in the game without mods. Press Esc again to pause. |
| The game crashes with Pause Anytime installed | An old Visual C++ runtime | Install the latest [Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe). |
| The log says `the loader refused the ProcessEvent hook` | Two copies of Pause Anytime are in one `ASI` folder, under different file names | Keep only one copy in each `ASI` folder. The copy that loaded first still works. |
| The game closes at start, and the log ends with `failed to resolve` and `Fatal assertion` lines | Pause Anytime could not start in this build of the game | Report it with the log. |
| The scene stays paused after a resume | Another pause holds the game, or Pause Anytime met an error | See [The scene stays paused](#the-scene-stays-paused). |
| A plain "PAUSE" text shows instead of the pause screen | The pause screen cannot be drawn | The pause still works. See [Plain PAUSE text](#plain-pause-text). |
| The picture is only darker, not blurred | The blur could not start on this screen | The pause still works. The log says why, in lines that start with `pause blur:`. |
| No photo mode key on the pause screen, or the photo camera does not move | A part of photo mode turned itself off | See [After a game update](#after-a-game-update). |
| The gamepad buttons on the pause screen are drawn circles | The game's own button pictures could not be used | The pause still works. The log says why. |

### The scene stays paused

The log says why. `the world stays paused by` means that the game no longer has the player who paused.
`still paused by` means that another pause holds the game, such as the Steam overlay: closing the overlay ends it. If
the scene still does not go on, and you have turned on the game's console, its `pause` command resumes the game.
Please report it, with the log.

### Plain PAUSE text

When Pause Anytime cannot draw its pause screen, it shows a plain "PAUSE" text with two keys under it. This text is
always in English, and it names the photo mode key even where photo mode cannot open. One cause is a Windows install
with no `d3dcompiler_47.dll` in `System32`. Pause Anytime also shows this text when its pause screen fails to start,
or fails while the game runs. The log then has a line with `the Canvas banner instead`.

A paused movie gets no such text. If the pause screen cannot be drawn, the movie only stops on its picture.

### After a game update

Before it changes some parts of the game, Pause Anytime checks that they are the ones it knows: photo mode's camera,
panel and sounds, and in LE2 and LE3 the keys in movies. If one differs, for example after a game update, the log has
a warning with `another build of the game?`, and that part turns off:

- Photo mode still opens, but its camera, its panel or (in LE1) its sounds may not answer.
- Without the camera part or the panel part, the pause screen leaves out the photo mode key.
- In LE1, without the sound part, the pause makes no menu sound either.
- In LE2 and LE3, Esc and Start skip a movie that the game lets you skip, as in the game without mods. In LE2, Space
  and X then skip no movie.

The game's button pictures are checked too. If they can't be used, the pause screen draws its own circles, and the log
has a line that says why.

If the game crashes at start after an update, delete the Pause Anytime file and report it.

## Known issues

- Loading-screen movies do not pause.
- In LE1, a movie that Esc skips in the game without mods is still skipped, because the game acts on Esc first.
- In LE2 and LE3, a movie pauses only when Pause Anytime can stop its sound with the picture. In the other movies,
  Esc and Start work as in the game without mods.
- In LE3, Esc during a movie in a conversation can pause the conversation while the movie plays on. Most other movies
  inside a cutscene or a conversation are not tested yet.
- In the first moment of a movie (a quarter second at most), Esc works as in the game without mods, so it can skip a
  movie that the game lets you skip.
- During a movie that Pause Anytime can pause, Esc counts only while the game's window is in front. Then every Esc
  counts, even one meant to close the console or the Steam overlay.
- With a menu open over a fullscreen movie, Esc and Start go to the movie.
- In an LE1 movie, Start acts when you let go of it, or after about 0.2 s if you hold it.
- Closing the Steam overlay during a pause ends the pause, and the scene goes on by itself.
- If you resize the window while a movie is paused, the pause screen is not drawn again until the movie goes on.

## Compatibility

- Other ASI mods: Pause Anytime shares the ASI loader with them. MeleBridge, from
  [mele-maxgoods-toolkit](https://github.com/Maxgoods62/mele-maxgoods-toolkit), works side by side with it. Pause
  Anytime does not need it, or any other mod.
- The Steam overlay and ReShade draw over the picture too. Whichever one draws last is on top.
- Resolutions: the pause screen works at any resolution and aspect ratio. Its sizes follow the height of the picture,
  and its layout is made for a picture 1080 pixels high.
- Gamepads: only an Xbox pad is tested, and the pause screen shows Xbox buttons. A PlayStation pad has to reach the
  game as an Xbox pad, through Steam Input or DS4Windows. That is not tested yet.

## Report a bug

Open an [issue](https://github.com/Maxgoods62/mele-pause-anytime/issues) and fill in the bug report form. It asks for:

- the game, and what happened;
- the steps that make it happen again: where in the game, and the keys or buttons you pressed;
- the Pause Anytime version, which is the number in the log's `ready` line;
- Steam or the EA app, and the game version if you know it;
- the files in the game's `ASI` folder, and your other mods;
- keyboard, or which gamepad you use.

Attach `Logs\PauseAnytime.log` from `<LE folder>\Game\ME<n>\Binaries\Win64`. Pause Anytime rewrites it each time the
game starts, so copy it before you start the game again. If the game crashed, say so.
