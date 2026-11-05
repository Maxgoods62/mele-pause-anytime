# Using Pause Anytime

Pause Anytime pauses conversations, in-engine cutscenes and some fullscreen movies in Mass Effect Legendary Edition. It
has no settings: the keys below are fixed. To install it, see [install.md](install.md).

## Keys

| Key | Where | LE1 | LE2 | LE3 |
|---|---|---|---|---|
| Esc or Start | Conversation | Pauses | Pauses | Pauses |
| Esc or Start | In-engine cutscene | Pauses | Pauses | Pauses |
| Esc or Start | Fullscreen movie | Pauses some movies | Pauses some movies | Pauses some movies |
| Esc, Start, Space or A | While paused | Resumes | Resumes | Resumes |
| Y or F7 | Paused conversation or cutscene | Opens photo mode | Opens photo mode | Opens photo mode |
| Y, F7, Esc or Start | Photo mode | Back to the paused scene | Back to the paused scene | Back to the paused scene |
| Space or X | Skippable cutscene, nothing paused | As in the normal game | Skips it, as in the normal game | Skips it, as in the normal game |
| Space or X | Skippable movie, nothing paused | As in the normal game | Skips it | Skips it, as in the normal game |
| Any key | Gameplay, menus, galaxy map, console | No change | No change | No change |

[Movies](#movies) says which movies pause in each game.

A paused conversation or cutscene resumes at the same word. Picture, voice, music and lip sync go on in sync.

In the game without mods:

- In an LE1 conversation, Esc and Start do nothing.
- In an LE1 cutscene, Esc only plays an error sound, because the game has its cutscene skip turned off. Start does
  nothing.
- In LE2 and LE3, Esc skips a cutscene that the game lets you skip.

With Pause Anytime, Esc pauses those LE2 and LE3 cutscenes instead. Space or X still skips them when nothing is paused.

Space and A only resume a pause that Pause Anytime started. They never pause. In photo mode, Space takes a screenshot
and does not resume. In the open console, Space and A do not resume either. When nothing is paused, Space keeps its
normal job: it skips a conversation line, or a movie or cutscene that the game lets you skip.

A key press that pauses or resumes never skips the line or the scene as well. While that key is down, and for a quarter
second after you let go of it, Pause Anytime blocks the game's skips.

Y and F7 keep their normal job everywhere else: in gameplay, in menus, in a conversation that is not paused and in a
movie. Esc and Start still open and close the pause menu in gameplay. In the open console, Esc closes the console, as
in the normal game.

## Movies

A paused movie stops picture and sound together. Esc, Start, Space or A resumes it. The pause screen comes up without
the photo mode key, because photo mode does not open in a movie. Loading-screen movies never pause.

In LE1:

- Esc or Start pauses a fullscreen movie that the game does not skip on Esc, such as the galaxy map travel movie.
- A movie that Esc skips in the normal game is still skipped. The game acts on Esc before Pause Anytime sees it.

In LE2:

- In most LE2 movies, the sound is not in the movie file: the game plays it separately. Pause Anytime pauses a movie
  only when it can stop that sound together with the picture. Among those movies are the mass relay jump, the Citadel
  arrival and departure, and movies of the Omega, Tuchanka and Illium hubs.
- In a movie that Pause Anytime can pause, Esc and Start pause instead of skipping. No key skips a paused movie.
- Space, or X on a gamepad, skips any movie that the game lets you skip. In the normal game these keys do nothing in a
  movie.
- In other movies, Esc and Start work as in the normal game: Esc skips the movie when the game lets you.

In LE3:

- As in LE2, Pause Anytime pauses a movie only when it can stop its sound together with the picture. This works for
  some of the movies that play on their own, outside a cutscene or a conversation. Among them are the mass relay jump,
  the first arrival at the Citadel, the Omega intro and the Leviathan arrivals.
- In the other movies that play on their own, and in loading screens, Esc and Start work as in the normal game.
- Space and X work as in the normal game: they skip a movie that the game lets you skip.
- Many LE3 movies play inside a cutscene or a conversation. What Esc does in those movies is not tested yet.

## Photo mode

The game opens photo mode only from its pause menu, and it refuses that menu in a conversation or a cutscene. Pause
Anytime opens photo mode under its own pause instead. [how-it-works.md](how-it-works.md#photo-mode-entering) explains
how.

1. Pause a conversation or a cutscene with Esc or Start.
2. Press Y on a gamepad, or F7. Photo mode opens on the shot that was on screen, with the same field of view. The
   dialogue wheel, the subtitle and the pause screen disappear. The scene stays paused.
3. Use photo mode's normal controls. Move with the sticks, LT and RT. Take a screenshot with X or Space. Open its
   options with B or Left Shift. With mouse and keyboard, hold the right mouse button to look and move.
4. Press Y, F7, Esc or Start to leave photo mode. The paused shot, the wheel, the subtitle and the pause screen come
   back, and the scene is still paused.
5. Press Esc or Start again to resume.

Photo mode shows gamepad prompts when you opened it with Y, and keyboard prompts when you opened it with F7. Photo mode
from the game's pause menu works as in the normal game.

## The pause screen

While the game is paused, the picture is blurred, darker and a little less colourful, with darker corners. The dialogue
wheel and the subtitle are blurred with it. The blur looks the same at any resolution. It has a blue tint in LE1 and
LE3, and a warm tint in LE2.

In the centre, the title stands on a band like the bars of the game's menus. The title is pale cyan on dark blue in LE1,
gold on brown in LE2, and white on blue in LE3.

The bottom left shows the key that resumes. Where photo mode can open, it also shows the photo mode key. The keys shown
depend on where you last pressed one of Pause Anytime's keys, on a gamepad or on the keyboard:

- For a gamepad, they are the game's own A and Y buttons, or green and yellow circles if the game cannot give them.
- For the keyboard, they are grey keycaps for Space and F7.

The words follow the game's language:

- The labels are the game's own words, in its text language: "Resume" and "Photo Mode" in English. In French they are
  "Mode photo", with "Continuer" in LE1 and "Reprendre" in LE2 and LE3.
- The title is Pause Anytime's own word, "PAUSE" in English, in the language that the launcher sets: English, French,
  German, Italian, Spanish, Polish or Russian. It is English in the Japanese game, and when the game starts without the
  launcher's language setting.
- In every game, the Space keycap is named in the launcher's language, and in English in the Japanese game.

The text uses the game's own fonts, which Pause Anytime reads from the game's memory while the game runs. The mod's
files hold no copy of them, and Pause Anytime never writes them to disk. If Pause Anytime cannot read them, it uses its
own font, Exo 2. Exo 2 has no Japanese letters, so the Japanese game then shows Pause Anytime's English words.

The game's menu sound plays as the pause starts and as you resume. In LE2 and LE3 it also plays when the Steam overlay
opens during a pause, and when the game itself ends the pause. The movie pause plays no sound.

The pause screen disappears at once when photo mode starts, so photo mode's screenshots never contain it. It comes back
right after photo mode ends. It also hides while the console is open.

If the pause screen cannot be drawn, Pause Anytime shows a plain "PAUSE" text with the keys under it. With a screen
format that the blur does not handle, the picture is only darkened. See
[troubleshooting.md](troubleshooting.md#plain-pause-text) for the causes.

## Gamepads

Start pauses and resumes, A resumes, and Y opens and closes photo mode. Only an Xbox pad has been tested. Steam Input or
DS4Windows can present a PlayStation pad to the game as an Xbox pad. Options should then work as Start, and Cross as A.
This is not tested yet.

In an LE1 movie, the game takes the press of Start for itself. Start then pauses when you let go of it, or after about
0.2 s if you hold it. In LE2 and LE3, Start pauses a movie as soon as you press it.
