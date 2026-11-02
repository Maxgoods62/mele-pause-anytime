# Installing Pause Anytime

Pause Anytime is an ASI mod for Mass Effect Legendary Edition. Each game has its own file: `LE1PauseAnytime.asi` for
LE1, `LE2PauseAnytime.asi` for LE2 and `LE3PauseAnytime.asi` for LE3. You install it by copying these files into the
game. It needs no other mod.

## Before you start

### Find the LE folder

These pages call the folder where Mass Effect Legendary Edition is installed `<LE folder>`. It contains a folder named
`Game`, and `Game` holds one folder for each game: `ME1`, `ME2` and `ME3`.

### The ASI loader

An ASI loader starts `.asi` files with the game. Pause Anytime needs
[LEBinkProxy](https://github.com/ME3Tweaks/LEBinkProxy), which loads every `.asi` file in the game's `ASI` folder.
Each game has its own loader and its own `ASI` folder.

To see whether the loader is there, open `<LE folder>\Game\ME1\Binaries\Win64` (or `ME2`, `ME3`). The loader is
installed when that folder holds a file named `bink2w64_original.dll` and a folder named `ASI`.

If they are missing, install any mod once with ME3Tweaks Mod Manager, which adds the loader along with the mod.

### The Visual C++ runtime

Install the latest [Microsoft Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe). With an older
one, Pause Anytime can make the game crash.

## Download

Download the zip from [Releases](https://github.com/Maxgoods62/mele-pause-anytime/releases). It is named
`PauseAnytime-v<N>.zip`, where N is the version. The zip holds:

- a folder for each game, `LE1`, `LE2` and `LE3`, with that game's `.asi` file;
- a `LICENSE` folder with the license texts;
- a `README.txt` with the short version of these steps.

The newest code is on the [latest page](https://maxgoods62.github.io/mele-pause-anytime/latest/), as
`PauseAnytime-latest.zip`. That build is less tested than a release.

## Check the download

You can check that the zip is the one GitHub published. Each release page lists the zip's SHA-256 hash, and the release
also has a `SHA256SUMS` file with the same hash. In PowerShell, in the folder that holds the zip, run:

```powershell
Get-FileHash .\PauseAnytime-v<N>.zip -Algorithm SHA256
```

The hash it prints must be the one on the release page. Upper or lower case does not matter. The GitHub CLI can also
check that the file belongs to the release and that GitHub built it. See
[Check a download](releasing.md#check-a-download).

## Copy the files

Close the game first. Then copy the file of each game you play into that game's `ASI` folder:

| File in the zip | Copy it into |
|---|---|
| `LE1\LE1PauseAnytime.asi` | `<LE folder>\Game\ME1\Binaries\Win64\ASI` |
| `LE2\LE2PauseAnytime.asi` | `<LE folder>\Game\ME2\Binaries\Win64\ASI` |
| `LE3\LE3PauseAnytime.asi` | `<LE folder>\Game\ME3\Binaries\Win64\ASI` |

Copy the `.asi` file itself, not the folder it is in. Each file works only in its own game. You can install it for
one game, two or all three.

## Check that it loaded

Start the game, then open `<LE folder>\Game\ME<n>\Binaries\Win64\Logs\PauseAnytime.log`, where `<n>` is the game's
number. It has a line that ends in `LE<n>PauseAnytime N ready`, where N is the version number.

If that log is missing, or its date and time are from before you started the game, Pause Anytime did not load.
[troubleshooting.md](troubleshooting.md) lists the causes.

## Update

Install the new version the same way: close the game and copy the new file over the old one. The file name has no
version number in it, so the new file replaces the old one.

## Uninstall

Close the game, then delete `LE<n>PauseAnytime.asi` from `<LE folder>\Game\ME<n>\Binaries\Win64\ASI` for each game. The
log `PauseAnytime.log` stays in `Binaries\Win64\Logs`. You can delete it too.

## Build from source

To build the files yourself, see [BUILDING.md](../BUILDING.md). It also explains `./build.sh --install` and
`./build.sh --uninstall`, which copy and delete the files for you.
