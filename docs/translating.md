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

The ids are the same in LE1, LE2 and LE3.

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
own word in the game's language, or by the English word when that one does not fit either. The pause screen itself
draws every language.

## What a translation mod holds

A translation mod is a DLC mod for ME3Tweaks Mod Manager (M3). M3's Starter Kit can make the folder to start from, and
Legendary Explorer edits talk files. `<Name>` below is your mod's name, without spaces.

Put Pause Anytime's ids only in the files of the languages you translate. Do not copy English text into the other
files: an id there would replace Pause Anytime's own French, German or other words. Ship a file for every language
code all the same, as Galaxy Map Normandy does (below).

When two installed mods define the same id, the one with the higher mount wins in LE1. LE2 and LE3 should do the
same, but that is not tested yet.

### LE1

A folder `DLC_MOD_<Name>` with:

- `AutoLoad.ini`:

  ```ini
  [Packages]
  GlobalTalkTable1=DLC_MOD_<Name>_GlobalTlk.GlobalTlk_tlk

  [ME1DLCMOUNT]
  ModName=<Name>
  ModMount=<mount>
  ```

  `<mount>` is a number no other mod uses.
- In `CookedPCConsole`, one package of talk files per language code: `DLC_MOD_<Name>_GlobalTlk.pcc` for English, and
  `DLC_MOD_<Name>_GlobalTlk_<code>.pcc` for the others.

| Language | Codes |
|---|---|
| French | `FR`, `FE` |
| German | `DE`, `GE` |
| Italian | `IT`, `IE` |
| Spanish | `ES` |
| Polish | `PL`, `PLPC` |
| Russian | `RU`, `RA` |
| Japanese | `JA` |

The two codes of a language show the same text: one plays the game in that language's voices, the other over English
voices. Ship both, with the same text.

Each package holds two talk files, `GlobalTlk_tlk` and `GlobalTlk_tlk_M`. The game reads one for a female Shepard
and the other for a male one, so put every id in both.

LE1 loads a DLC mod through an ASI that M3 installs with it.

[Galaxy Map Normandy](https://github.com/Maxgoods62/mele-galaxy-map-normandy) is an LE1 mod built this way. Its
`mod/DLC_MOD_GalaxyMapNormandy/AutoLoad.ini` names its talk files. Its `build.sh` writes a package per language code
from a tab-separated text file per language, with `lectool tlkpackage` from
[mele-maxgoods-toolkit](https://github.com/Maxgoods62/mele-maxgoods-toolkit/blob/main/tools/lectool/README.md#strings),
which fills both talk files.

### LE2 and LE3

Start from a DLC folder made by M3's Starter Kit, or by the toolkit's `dlc_new`
([the toolkit's game page](https://github.com/Maxgoods62/mele-maxgoods-toolkit/blob/main/docs/games.md)). Either
makes every file the game needs to mount the DLC, such as `Mount.dlc`, LE2's `BIOEngine.ini` and LE3's
`Default_DLC_MOD_<Name>.bin`, and a talk file per language. Then add Pause Anytime's ids to the talk files, in
`DLC_MOD_<Name>\CookedPCConsole`:

- LE2: `DLC_<module>_<code>.tlk`, where `<module>` is the number that `BIOEngine.ini` gives the DLC under
  `[Engine.DLCModules]`;
- LE3: `DLC_MOD_<Name>_<code>.tlk`.

The codes are `INT` (English), `DEU`, `ESN`, `FRA`, `ITA`, `JPN`, `POL` and `RUS`. Neither game ships `FRE`, `DEE`,
`ITE` or `POE` talk files, so a game in French, German, Italian or Polish (LE2) over English voices reads the `FRA`,
`DEU`, `ITA` or `POL` file.

A `.tlk` file has a main table, and a second table for lines that change with a female Shepard. Pause Anytime's words
go in the main table. The toolkit's `tlk_set` writes `.tlk` files.

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
your id: check the file names, the language codes, the mount and, in LE2, the module number.

Then pause a conversation and look at the screen. At this first pause Pause Anytime asks for its ids once more, and
logs `texts: asked again at the first pause: no new answer`. Other lines can follow:

- `texts: prompt 74300007 "..." skipped: a pattern holds %1 and %2 once each`: your pattern lacks `%1` or `%2`.
- `texts: asked again at the first pause, our ids answer otherwise: ...`: your talk file loaded after the main menu's
  first frame, so the words of the `texts:` line stay until the game restarts. Please report it, with the log.
- `overlay: <word> "<text>" (<source>) has letters the <font> font lacks: ... instead`: the pause screen's font cannot
  draw a letter of that word, so another text stands in. The game's own fonts draw every language of the game. Pause
  Anytime's fallback font, Exo 2, used only when it cannot read the game's fonts, has no Japanese letters.
- `banner: <word> "<text>" (<source>) is not Latin-1, the Canvas font's range: built-in <language> "<text>"
  instead`: see the banner's font above.

The words are read once, so a change to your mod shows after the game restarts.
