# Troubleshooting Pause Anytime

## Read the log first

Pause Anytime writes a log at `<LE folder>\Game\ME<n>\Binaries\Win64\Logs\PauseAnytime.log`, where `<n>` is the
game's number. It rewrites the file each time the game starts, so copy it before you start the game again.

- A line that ends in `LE<n>PauseAnytime N ready` means that Pause Anytime loaded. N is the version.
- Each press of Esc or Start that reaches Pause Anytime adds a line with `Esc pressed` or `Start pressed`. The line
  says what Pause Anytime did, such as `paused the world`, or `vanilla` when it left the key to the game.
- A line with `not found at RVA` and `another build of the game?` means that a part of Pause Anytime turned itself off.
  See [Another version of the game](#another-version-of-the-game).

No log, or a log from before you started the game, means that Pause Anytime did not load.

## Common problems

| Problem | Likely cause | What to do |
|---|---|---|
| Nothing pauses, and there is no new `PauseAnytime.log` | No ASI loader, or the file is in the wrong folder | Check both ([install.md](install.md#before-you-start)). |
| Esc does nothing in a conversation, and the log has no `Esc pressed` line | A game menu that was taking the keys got Esc first | Known issue, with no fix yet. |
| Esc closes the console | The console was open | Esc closes it, as in the normal game. Press Esc again. |
| A movie does not pause | Pause Anytime cannot pause that movie | See [which movies pause](using.md#movies). |
| The game crashes with Pause Anytime installed | An old Visual C++ runtime (`msvcp140.dll`) | Install the latest [Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe). |
| A plain "PAUSE" text shows instead of the pause screen | The pause screen cannot be drawn | The pause still works. See [below](#plain-pause-text). |
| The picture is only darker, not blurred | The blur does not handle that screen format | The pause still works. |
| No photo mode key on the pause screen, or the photo camera does not move | Another version of the game | See [below](#another-version-of-the-game). |
| The gamepad buttons on the pause screen are drawn circles | The game could not give its button pictures | The log says why. The pause still works. |

### Plain PAUSE text

Pause Anytime falls back to a plain "PAUSE" text, with the keys under it, when it cannot draw its pause screen. One
cause is a Windows without `d3dcompiler_47.dll` in `System32`. Pause Anytime also falls back when its pause screen fails
to start or fails while the game runs. The log then has a line about it. During a movie pause there is no such text,
because the game's HUD does not draw over a fullscreen movie.

### Another version of the game

Pause Anytime checks the game's code before it hooks photo mode, and in LE2 and LE3 before it hooks the movie keys. If
the code differs, for example after a game update, the log shows a warning and that part turns off:

- Photo mode still opens, but its camera, its panel or its sounds may not respond.
- Without the camera part or the panel part, the pause screen leaves out the photo mode key.
- In LE2 and LE3, Esc and Start skip a skippable movie as in the normal game. In LE2, Space and X then skip nothing.
- The gamepad buttons on the pause screen are drawn circles, and the log has a warning.

## Known issues

- Loading-screen movies do not pause.
- In LE1, a movie that Esc skips in the normal game is still skipped. The game acts on Esc before Pause Anytime sees it.
- In LE2 and LE3, a movie whose sound Pause Anytime cannot stop with the picture does not pause. Esc and Start work as
  in the normal game there.
- In LE3, Esc in a movie that plays inside a cutscene or a conversation is not tested yet.
- In the open console, Esc closes the console, as in the normal game.
- In a conversation or a cutscene, a game menu that is taking the keys can get Esc before Pause Anytime sees it. Then
  nothing pauses.
- During a movie, Pause Anytime reads Esc straight from the keyboard. It ignores a press while another program's
  window is in front.
- During a movie, any Esc pressed while the game's window is in front pauses or resumes the movie. That includes an Esc
  meant to close the console or the Steam overlay.
- Pause Anytime does not take a key pressed in the first quarter second of a movie. That key works as in the normal
  game, so Esc still skips a movie that the game lets you skip.
- With a menu open over a fullscreen movie, Esc and Start go to the movie.
- In an LE1 movie, Start acts when you let go of it, or after about 0.2 s if you hold it. The game can lose the release
  of Start, for example when another window takes the focus. So if a Start press sends nothing for half a second, Pause
  Anytime counts it as over. The next press of Start then counts as a new one.
- Closing the Steam overlay during a pause ends the pause, and the scene resumes by itself.
- If you resize the window while a movie is paused, the old picture stays until the movie resumes.

## Compatibility

- MeleBridge: works side by side with Pause Anytime. Pause Anytime does not need it, or any other mod.
- The Steam overlay, ReShade and MeleBridge's editor: each of them draws over the picture too. Whichever one draws last
  is on top.
- Resolutions: the pause screen works at any resolution and aspect ratio. Its sizes follow the height of the picture,
  and its layout is made for a picture 1080 pixels high.
- Gamepads: tested with an Xbox pad only. A PlayStation pad through Steam Input or DS4Windows is not tested yet
  ([Gamepads](using.md#gamepads)).
- Another version of the game: some parts turn off ([above](#another-version-of-the-game)).

## Report a bug

Open an [issue](https://github.com/Maxgoods62/mele-pause-anytime/issues) and fill in the bug report form. It asks for:

- the game;
- what happened, and the steps that make it happen again;
- the Pause Anytime version, which is the number in the log's `ready` line;
- Steam or the EA app;
- the game version, if you know it;
- the files in the game's `ASI` folder;
- your other mods;
- keyboard, or which gamepad you use;
- the log file.

Attach `Logs\PauseAnytime.log` from `<LE folder>\Game\ME<n>\Binaries\Win64`. Pause Anytime rewrites it each time the
game starts, so copy it before you start the game again.
