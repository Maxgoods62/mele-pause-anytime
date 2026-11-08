# Using Pause Anytime

Pause Anytime lets you pause Mass Effect Legendary Edition during conversations, in-engine cutscenes (the ones the
game draws live) and some fullscreen movies. Picture, voice, music and lip sync stop together, and they go on in sync
from the same word. It works in LE1, LE2 and LE3, and it has no settings. To install it, see [install.md](install.md).

## Keys

Pause Anytime uses six keys: Esc, Space and F7 on the keyboard, Start, A and Y on a gamepad. Esc and Start pause and
resume. Space and A only resume. F7 and Y open and leave photo mode. All other keys work as in the game without mods,
except X in LE2 movies.

In the table, "No change" means that the key does what it does in the game without mods.

| Where you are | Esc or Start | Space or A | F7 or Y |
|---|---|---|---|
| Gameplay, menus, galaxy map | No change | No change | No change |
| Conversation or cutscene | Pauses | No change | No change |
| Paused conversation or cutscene | Resumes | Resumes | Opens photo mode |
| Photo mode, opened from a pause | Back to the pause | No change | Back to the pause |
| A movie that pauses | Pauses | See [Movies](#movies) | No change |
| A paused movie | Resumes | Resumes | No change |
| Other movies, loading screens | See [Movies](#movies) | See [Movies](#movies) | No change |
| Console open | No change | No change | No change |

The games differ in a few places:

- In LE1 without mods, Esc and Start do nothing in a conversation or a cutscene, except that Esc plays an error sound
  in a cutscene.
- In LE2 and LE3 without mods, Esc skips a cutscene that the game lets you skip. With Pause Anytime, Esc pauses it.
  When nothing is paused, Space, or X on a gamepad, still skips it.
- In LE2, Space and X also skip some movies. [Movies](#movies) has the details for each game.

When nothing is paused, Space keeps its usual job: it skips a line, or a cutscene that the game lets you skip. The
press that pauses or resumes a cutscene does not also skip it. In photo mode, Space takes a picture. While the console
is open, Esc closes it.

If you open the Steam overlay during a pause, closing the overlay ends the pause, and the scene goes on.

## Movies

A paused movie stops its picture and its sound together. Esc, Start, Space or A resumes it. The pause screen shows
over the frozen picture with the resume key only, because photo mode does not open in a movie. The movie pause makes
no menu sound.

Loading-screen movies never pause, in any game. Nor do videos shown on a screen inside a level.

### LE1

- Every LE1 movie has its sound inside the movie file, so Esc or Start can pause any fullscreen movie, such as the
  galaxy map travel movie.
- A movie that Esc skips in the game without mods is still skipped, because the game acts on Esc first.
- During a movie the game keeps the press of Start for itself. So Start pauses or resumes a movie when you let go of
  it, or after about 0.2 s if you hold it down.

### LE2

- In most LE2 movies, the game plays the sound apart from the picture. Pause Anytime pauses a movie only when it can
  stop that sound with the picture. The mass relay jump pauses, and so do the arrivals and departures at the hubs,
  such as the Citadel, Omega, Tuchanka and Illium.
- Some movies never pause, because voices, music or other sounds play beside them. They include movies of the
  Prologue and the suicide mission, and Tali's and Grunt's arrivals.
- In a movie that pauses, Esc and Start pause instead of skipping. Start acts as soon as you press it.
- When nothing is paused, Space, or X on a gamepad, skips a movie that the game lets you skip. In the game without
  mods these two keys skip nothing in a movie. The relay jump cannot be skipped.
- While a movie is paused, no key skips it.
- In the other movies, Esc and Start work as in the game without mods: Esc skips a movie that the game lets you skip.

### LE3

- Pause Anytime pauses an LE3 movie only when it can stop the movie's sound with the picture. This works for some
  movies, such as the mass relay jump, the first arrival at the Citadel, the intro movies of the Omega DLC and the
  arrivals in the Leviathan DLC.
- Some movies never pause, because music, voices or other sounds play beside them. They include the Mars arrival,
  the two Normandy movies of the Citadel DLC and the intro of Hackett's speech.
- In a movie that pauses, Esc and Start pause. Start acts as soon as you press it.
- Most LE3 movies are part of a cutscene or a conversation, and Pause Anytime never pauses them as movies. In a test,
  Esc during a movie in a conversation paused the conversation, and the movie played on. The other cases are not
  tested yet.
- In loading screens, Esc and Start work as in the game without mods. With no movie paused, Space and X work as in
  the game without mods, in every movie.

## Photo mode

In the game without mods, photo mode opens only from the pause menu, and that menu does not open in a conversation or
a cutscene. Pause Anytime opens photo mode from its own pause:

1. Pause a conversation or a cutscene with Esc or Start.
2. Press F7, or Y on a gamepad. Photo mode opens on the shot that was on screen, with the same field of view. The
   reply wheel, the subtitle and the pause screen go away. The scene stays paused.
3. Use photo mode as usual: its controls are shown at the bottom of the screen. Space, or X on a gamepad, takes a
   picture. With mouse and keyboard, hold the right mouse button to look around.
4. Press F7, Y, Esc or Start to leave photo mode. The paused shot comes back, with the wheel, the subtitle and the
   pause screen. The scene is still paused.
5. Press Esc, Start, Space or A to resume.

Photo mode shows gamepad prompts when you open it with Y, and keyboard prompts when you open it with F7. The pause
screen goes away before photo mode shows anything, and it comes back only after photo mode has closed, so it is never
in your pictures. Photo mode opened from the game's own pause menu works as in the game without mods.

## The pause screen

While the game is paused, the whole picture is blurred, darker and a little less colourful, with darker corners. The
reply wheel and the subtitle are blurred with it. The blur has a blue tint in LE1 and LE3, and a warm tint in LE2. It
looks the same at any resolution.

The title stands in the middle, on a band like the bars of the game's menus. It is pale cyan on dark blue in LE1, gold
on brown in LE2, and white on blue in LE3.

The bottom left shows the key that resumes, Space or A. Where photo mode can open, it also shows the photo mode key,
F7 or Y. Esc and Start resume too, but the screen does not show them. The keys shown follow the last of the six keys
you pressed: Esc, Space or F7 for the keyboard, Start, A or Y for a gamepad. Until you press one, the screen shows the
keyboard keys.

- Keyboard keys are grey keycaps.
- Gamepad buttons are the game's own pictures of A and Y, as on an Xbox pad. If the game cannot give them, Pause
  Anytime draws green and yellow circles instead.

The words follow the text language that you pick in the game's launcher. The two labels are the game's own words,
such as "Resume" and "Photo Mode" in English. The title and the Space key are Pause Anytime's own words:

| Language | Title | Space key |
|---|---|---|
| English | PAUSE | Space |
| French | PAUSE | Espace |
| German | PAUSE | Leertaste |
| Italian | PAUSA | Spazio |
| Spanish | PAUSA | Espacio |
| Polish | PAUZA | Spacja |
| Russian | ПАУЗА | Пробел |

The Japanese game shows the title and the Space key in English, and so does a game started without the launcher. "F7"
is never translated. If the font cannot draw a label, Pause Anytime shows its own word for it.

The text uses the game's own menu fonts, read from the game's memory while it runs. Pause Anytime holds no copy of
them and never writes them to disk. If it cannot read them, it uses its own font, Exo 2. Exo 2 has no Japanese
letters, so the Japanese game then shows the labels in English.

A mod that changes the game's fonts, such as Myriad Pro Begone, changes the pause screen too. Pause Anytime reads the
fonts as the mod leaves them. Myriad Pro Begone swaps the game's Myriad for Domyouji. In LE2 and LE3 the pause screen
uses Myriad only for the key names, so those change. In LE1 the title and key names were already in Domyouji. The mod
redraws some of their letters and drops their kerning, so the title gets a little wider. The labels use Biomass in
every game, and the mod leaves Biomass alone.

The game's menu sound plays as the pause starts and as you resume with a key. In LE2 and LE3, it also plays when you
open the Steam overlay during a pause, and when the game ends the pause itself, for example with a load.

The pause screen hides while the console is open. If the pause screen cannot be drawn, a plain white "PAUSE" shows
near the top instead, with the keys under it, in English. If the blur does not work with the game's screen format,
the picture is only darkened. [troubleshooting.md](troubleshooting.md) lists the causes.

## Gamepads

Only an Xbox pad has been tested. Steam Input or DS4Windows can show a PlayStation pad to the game as an Xbox pad. Then
Options should work as Start, Cross as A and Triangle as Y, but this is not tested yet. The pause screen always shows
Xbox buttons.
