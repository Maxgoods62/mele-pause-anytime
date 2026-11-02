# Pause Anytime

Pause Anytime lets you pause Mass Effect Legendary Edition where the game does not: during conversations, in-engine
cutscenes and some fullscreen movies. It works in LE1, LE2 and LE3. Press Esc, or Start on a gamepad, to pause, and
press the same key again to resume at the same word.

## What it does

- Picture, voice, music and lip sync stop together, and they resume in sync.
- Some fullscreen movies pause too, such as the galaxy map travel movie in LE1 and the mass relay jump in LE2 and LE3
  ([which movies](docs/using.md#movies)). Loading-screen movies do not pause.
- While the game is paused, the picture is blurred and darkened. A pause screen in the style of the game's own menus
  shows the keys, in the game's language.
- In a paused conversation or cutscene, Y on a gamepad or F7 opens the game's photo mode on the paused shot. The scene
  stays paused, and the pause screen never shows in your screenshots.
- Everywhere else the keys do what they always did. Pause Anytime has no settings and needs no other mod.

## Keys

| Keyboard | Gamepad | What it does |
|---|---|---|
| Esc | Start | Pauses a conversation, a cutscene or some movies. Press it again to resume. |
| Space | A | Resumes a pause. It never pauses. |
| F7 | Y | While a conversation or a cutscene is paused, opens photo mode and closes it again. |
| Space | X | In LE2 and LE3, skips a cutscene that the game lets you skip. Use it there instead of Esc, which pauses. |

In gameplay, in menus and on the galaxy map, the keys work as in the game without mods. Every key in each game:
[docs/using.md](docs/using.md#keys).

## Games

| | LE1 | LE2 | LE3 |
|---|---|---|---|
| Pauses conversations and cutscenes | Yes | Yes | Yes |
| Pauses fullscreen movies | Some, such as the travel movie | Some, such as the relay jump | Some, such as the relay jump |
| Photo mode while paused | Yes | Yes | Yes |
| Pause screen in the game's own look | Yes | Yes | Yes |

## Install

Pause Anytime is an ASI mod: one `.asi` file for each game, which an ASI loader starts with the game. `<LE folder>` is
the folder where Mass Effect Legendary Edition is installed. It contains a folder named `Game`.

1. Check that the game has an ASI loader. The folder `<LE folder>\Game\ME1\Binaries\Win64` (ME2 or ME3 for the other
   games) must hold `bink2w64_original.dll` and an `ASI` folder. If they are missing, install any mod once with
   ME3Tweaks Mod Manager: it adds the loader.
2. Install the latest [Microsoft Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe).
3. Download the zip from [Releases](https://github.com/Maxgoods62/mele-pause-anytime/releases).
4. Close the game. Copy the file of each game you play into that game's `ASI` folder:

   | File in the zip | Copy it into |
   |---|---|
   | `LE1\LE1PauseAnytime.asi` | `<LE folder>\Game\ME1\Binaries\Win64\ASI` |
   | `LE2\LE2PauseAnytime.asi` | `<LE folder>\Game\ME2\Binaries\Win64\ASI` |
   | `LE3\LE3PauseAnytime.asi` | `<LE folder>\Game\ME3\Binaries\Win64\ASI` |

5. Start the game. The file `Logs\PauseAnytime.log` in `Binaries\Win64` should have a line that ends in `ready`.

[docs/install.md](docs/install.md) explains how to update, uninstall and check a download. It also links the latest
build, which is less tested than a release. To build Pause Anytime yourself, see [BUILDING.md](BUILDING.md).

## Problems

[docs/troubleshooting.md](docs/troubleshooting.md) lists common problems and known issues. To report a bug, open an
[issue](https://github.com/Maxgoods62/mele-pause-anytime/issues) and attach `Logs\PauseAnytime.log`. Pause Anytime
rewrites that log each time the game starts, so copy it before you start the game again.

## Documentation

- For players: [installing](docs/install.md), [using it](docs/using.md), [troubleshooting](docs/troubleshooting.md)
  and the [changes in each version](CHANGELOG.md).
- For modders: [building from source](BUILDING.md), [how it works](docs/how-it-works.md), the notes on
  [LE2](docs/games/LE2.md) and [LE3](docs/games/LE3.md), and the [third-party code](THIRD_PARTY.md).
- For the maintainer: [testing a build](docs/testing.md) and [releasing](docs/releasing.md).

## License

GPL-3.0 ([LICENSE](LICENSE)). Third-party code and fonts: [THIRD_PARTY.md](THIRD_PARTY.md).
