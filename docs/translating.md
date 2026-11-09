# Translating Pause Anytime

This page is for modders who want the pause screen in their own words. Pause Anytime needs no change for that. Each
word it shows has a string id. A translation is a small DLC mod whose talk files define some of those ids. Pause
Anytime asks the game for them at the main menu and shows your words.

Pause Anytime ships no talk file. Its own words are built in, and a translation is always a mod of its own.

## Where each word comes from

For each word, Pause Anytime takes the first of these that has a text:

1. Your string id, when a talk file of an installed mod defines it.
2. The game's own string, for the two words the game has: Resume and Photo mode.
3. Pause Anytime's own text, in the text language picked in the game's launcher.

Pause Anytime has its own text in English, French, German, Italian, Spanish, Polish, Russian and Japanese. A game
started without the launcher gets English.

## The string ids

The ids are the same in LE1, LE2 and LE3. [strings.tsv](strings.tsv) lists them with their English text,
one per line (the id, a tab, the text), so you can start your talk files from it. The build checks that the file
matches the code.

| Id | Word | Shown on | English text | The game's own string |
|---|---|---|---|---|
| 74300000 | The mod's name | nothing yet | | |
| 74300001 | Title | pause screen, banner | `PAUSE` | none |
| 74300002 | Resume | pause screen, banner | `Resume` | 157151 |
| 74300003 | Photo mode | pause screen, banner | `Photo mode` | 184397 in LE1, 392221 in LE2, 817979 in LE3 |
| 74300004 | The Space key | pause screen | `Space` | none |
| 74300005 | The Esc key | banner | `Esc` | none |
| 74300006 | The Start button | banner | `Start` | none |
| 74300007 | A key and what it does | banner | `%1: %2` | none |

Ids 74300008 to 74300099 are kept for later words. Do not use them.

- The pause screen is what you normally see: a title on a band, and the keys at the bottom left.
- The banner is plain text near the top of the screen. It shows only when the pause screen cannot be drawn
  ([troubleshooting.md](troubleshooting.md#plain-text-banner)).
- In 74300007, `%1` is the key and `%2` what it does, as in `Esc: Resume`. Each must be there once, and no digit may
  follow it (`%10` is not `%1`). A text without them is not used, and the log says so.
- `F7`, and the letters `A` and `Y` on the gamepad buttons, are never translated.

You can define only some of the ids. A word you leave out comes from the next source in the list above.

The banner's font has only the 256 characters of Latin-1, which covers Western European languages. A banner word
with any other letter (Russian, Japanese, and Polish letters such as `ł` or `ą`) is replaced there by Pause Anytime's
own word in the game's language, or by the English word when that one does not fit either. The pause screen draws
in the game's fonts, which showed Russian letters in LE1, LE2 and LE3, and Japanese in LE1.

## What a translation mod holds

[Translating my mods](https://github.com/Maxgoods62/mele-maxgoods-toolkit/blob/main/docs/translating.md) says how to
build a translation mod in LE1, LE2 and LE3. Pause Anytime is an ASI mod: your mod defines Pause Anytime's ids in
talk files of its own, at a mount no other mod uses.

Put Pause Anytime's ids only in the files of the languages you translate. Do not copy English text into the other
files: an id there would replace Pause Anytime's own French, German or other words. In LE1 that includes your English
package, unless you translate English ([why](https://github.com/Maxgoods62/mele-maxgoods-toolkit/blob/main/docs/translating.md#the-talk-files)).

When two installed mods define the same id, nobody has tested which one the game shows, in any of the three games.
Install one translation of Pause Anytime at a time.

## Check it in the game

1. Close the game, and install your mod with M3.
2. Start the game in the language you translated, and wait for the main menu.
3. Open `Logs\PauseAnytime.log` in `<LE folder>\Game\ME<n>\Binaries\Win64`.

Pause Anytime reads the words once a session, as the main menu shows, and logs one line that starts with `texts:`.
For each word it gives the text and its source:

```text
texts: title "PAUZA" ours 74300001; resume "Wznów" game 157151; photo mode "Tryb fotograficzny" game 392221;
space "Spacja" built-in Polish; esc "Esc" built-in Polish; start "Start" built-in Polish; prompt "%1: %2" built-in Polish
```

The log writes it on one line. `ours <id>` is a word from a talk file, `game <id>` the game's own string, and
`built-in <language>` Pause Anytime's own text. If your word shows as `built-in` or `game`, the game did not find
your id: check the file names, the language codes, the mount, in LE1 the `[Packages]` line of `AutoLoad.ini`, and in LE2 the
module number.

Then pause a conversation and look at the screen. At this first pause Pause Anytime asks for its ids once more, only
to check them, and logs `texts: asked again at the first pause: no new answer`. Other lines can follow:

- `texts: prompt 74300007 "..." skipped: a pattern holds %1 and %2 once each`: your pattern lacks `%1` or `%2`.
- `texts: asked again at the first pause, our ids answer otherwise: ...`: your talk file loaded after the main menu's
  first frame, so the words of the `texts:` line stay until the game restarts. Please report it, with the log.
- `overlay: <word> "<text>" (<source>) has letters the <font> font lacks: ... instead`: the pause screen's font cannot
  draw a letter of that word, so another text stands in. Pause Anytime falls back to its own font, Exo 2, only when
  it cannot read the game's fonts, and Exo 2 has no Japanese letters.
- `banner: <word> "<text>" (<source>) is not Latin-1, the Canvas font's range: built-in <language> "<text>"
  instead`: see the banner's font above.

The words are read once, so a change to your mod shows after the game restarts.

## What has been tested

On 2026-11-09, in LE1, LE2 and LE3: talk files defined all seven ids with test words, Russian letters included. The
title, Resume, Photo mode and Space showed on the pause screen in the test words, in the game's fonts, and the
`texts:` line gave all seven as `ours`, already at the main menu. At the first pause the log said `no new answer`. In
LE1 the talk files were a copy of another of my mods' GlobalTlk package, in LE2 and LE3 copies of a third mod's `.tlk`
files, in English games.

The same day, in LE1, a translation mod built by the steps now on the toolkit page (then on this page), with its own
mount and its own GlobalTlk packages, showed its words on the pause screen. With all seven ids in its English package
only, a French game showed them too. With only the translation mod's own name (id 74810000) in its English package and
the seven ids in `FR` and `FE`, a French game showed the translation's words and an English game Pause Anytime's own.
A Japanese game showed Pause Anytime's own ポーズ and スペース in the game's fonts.

Still untested: a translation mod built from these pages in LE2 or LE3, two mods that define the same id, the banner
with a translation, and Japanese on the pause screen in LE2 and LE3.
