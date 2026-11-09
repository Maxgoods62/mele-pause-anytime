# Pause Anytime

Pause Anytime lets you pause Mass Effect Legendary Edition where the game does not: during conversations, in-engine
cutscenes and some fullscreen movies. It works in LE1, LE2 and LE3. Press Esc, or Start on a gamepad, to pause. Press
it again to resume at the same word.

The picture, the voice, the music and the lips stop together and go on together. While the game is paused, the
picture is blurred and darkened under a pause screen in the colours of the game's menus. From a paused scene you can
open the game's photo mode on the same shot. The pause screen is hidden in photo mode, so it is never in your photos.
Everywhere else the keys do what they do in the game without mods, except Space and X in LE2 movies. Pause Anytime has
no settings.

All of the code here was written by an LLM, Claude Opus 5.5, through Claude Code, from my instructions. I decide what
to build and I test the results in the game.

## Keys

| Keyboard | Gamepad | What it does |
|---|---|---|
| Esc | Start | Pauses a conversation, a cutscene or a movie that can pause. Press it again to resume. |
| Space | A | Resumes. It never pauses. |
| F7 | Y | In a paused conversation or cutscene, opens photo mode. Press it again to leave. |
| Space | X | In LE2 and LE3, with nothing paused: skips a cutscene or a movie that the game lets you skip. |

In LE2 and LE3, Esc pauses a cutscene that it skips in the game without mods. Skip it with Space or X instead. The
keys in each game: [docs/using.md](docs/using.md#keys).

## Games

| | LE1 | LE2 | LE3 |
|---|---|---|---|
| Conversations and cutscenes | Pause | Pause | Pause |
| Fullscreen movies | Most pause, such as the galaxy map travel movie | Some pause, such as the mass relay jump | Some pause, such as the mass relay jump |
| Photo mode from a pause | Yes | Yes | Yes |
| Pause screen colours | Pale cyan on blue | Gold on brown | White on blue |

Loading-screen movies never pause. Which movies pause in each game: [docs/using.md](docs/using.md#movies).

## Install

Pause Anytime is an ASI mod: one `.asi` file for each game, which an ASI loader starts with the game. `<LE folder>` is
the folder where Mass Effect Legendary Edition is installed (it holds a folder named `Game`), and `<n>` is the game's
number: 1, 2 or 3.

1. Check that the game has an ASI loader: `<LE folder>\Game\ME<n>\Binaries\Win64` must hold `bink2w64_original.dll`
   and an `ASI` folder. If they are missing, install any mod once with ME3Tweaks Mod Manager. It adds the loader,
   [LEBinkProxy](https://github.com/ME3Tweaks/LEBinkProxy).
2. Install the latest [Microsoft Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe).
3. Download `PauseAnytime-v<N>.zip` from the
   [Nexus Mods page](https://www.nexusmods.com/masseffectlegendaryedition/mods/3539) or from
   [Releases](https://github.com/Maxgoods62/mele-pause-anytime/releases).
4. Close the game. Copy the file of each game you play into that game's `ASI` folder:

   | File in the zip | Copy it into |
   |---|---|
   | `LE1\LE1PauseAnytime.asi` | `<LE folder>\Game\ME1\Binaries\Win64\ASI` |
   | `LE2\LE2PauseAnytime.asi` | `<LE folder>\Game\ME2\Binaries\Win64\ASI` |
   | `LE3\LE3PauseAnytime.asi` | `<LE folder>\Game\ME3\Binaries\Win64\ASI` |

5. Start the game. The file `Logs\PauseAnytime.log` in `Binaries\Win64` should have a line that ends in
   `LE<n>PauseAnytime N ready`, where N is the version.

[docs/install.md](docs/install.md) explains how to update, uninstall and check a download, and links the latest build.

## Problems

[docs/troubleshooting.md](docs/troubleshooting.md) lists common problems and known issues. To report a bug, open an
[issue](https://github.com/Maxgoods62/mele-pause-anytime/issues) and attach `Logs\PauseAnytime.log`. Pause Anytime
rewrites that log each time the game starts, so copy it before you start the game again.

## Documentation

- For players: [installing](docs/install.md), [using it](docs/using.md), [troubleshooting](docs/troubleshooting.md)
  and the [changes in each version](CHANGELOG.md).
- For modders: [building from source](BUILDING.md), [how it works](docs/how-it-works.md), the pages on
  [LE1](docs/games/LE1.md), [LE2](docs/games/LE2.md) and [LE3](docs/games/LE3.md), and the
  [third-party code](THIRD_PARTY.md).
- For translators: [translating the pause screen](docs/translating.md).
- For the maintainer: [testing a build](docs/testing.md).
- How releases are built on GitHub and how to check them: the toolkit's
  [releasing page](https://github.com/Maxgoods62/mele-maxgoods-toolkit/blob/main/docs/releasing.md).

## License

GPL-3.0 ([LICENSE](LICENSE)). Third-party code and fonts: [THIRD_PARTY.md](THIRD_PARTY.md).
