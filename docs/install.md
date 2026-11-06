# Installing Pause Anytime

Pause Anytime is an ASI mod for Mass Effect Legendary Edition. Each game has its own file: `LE1PauseAnytime.asi`,
`LE2PauseAnytime.asi` and `LE3PauseAnytime.asi`. Each file works only in its own game. You can install it for one
game, two or all three.

## Before you start

### The LE folder

These pages call the folder where Mass Effect Legendary Edition is installed `<LE folder>`. It holds a folder named
`Game`, and `Game` holds one folder for each game: `ME1`, `ME2` and `ME3`. `<n>` is the game's number: 1, 2 or 3.

### The ASI loader

An ASI loader starts `.asi` files with the game. Pause Anytime needs
[LEBinkProxy](https://github.com/ME3Tweaks/LEBinkProxy), which loads every `.asi` file in the game's `ASI` folder.
Each game has its own loader and its own `ASI` folder.

Open `<LE folder>\Game\ME<n>\Binaries\Win64`. If it holds a file named `bink2w64_original.dll` and a folder named
`ASI`, the loader is there. If they are missing, install any mod once with ME3Tweaks Mod Manager. It installs the
loader with the mod.

### The Visual C++ runtime

Install the latest [Microsoft Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe). Pause Anytime
needs it, and an older one can make the game crash.

## Download

Download the zip from the [Nexus Mods page](https://www.nexusmods.com/masseffectlegendaryedition/mods/3539) or from
[Releases](https://github.com/Maxgoods62/mele-pause-anytime/releases). Both have the same file. It is named
`PauseAnytime-v<N>.zip`, where N is the version. It holds:

- one folder for each game, `LE1`, `LE2` and `LE3`, with that game's `.asi` file;
- a `LICENSE` folder with the license texts;
- a `README.txt` with a short version of these steps.

### The latest build

The [latest page](https://maxgoods62.github.io/mele-pause-anytime/latest/) has `PauseAnytime-latest.zip`, built from
the newest code. It is built again each time the code changes, and it is less tested than a release.

## Check a download

You can check that your zip is the one that was published. In PowerShell, in the folder that holds the zip, run:

```powershell
Get-FileHash .\PauseAnytime-v<N>.zip -Algorithm SHA256
```

Compare the hash with the zip's line in `SHA256SUMS` (upper or lower case does not matter). That file is next to the
zip, on the release page or on the latest page. A match only shows that your file is the one on the list. The GitHub
CLI can also check that the zip belongs to the release and that GitHub built it from the release's tag: see
[Check a download](https://github.com/Maxgoods62/mele-maxgoods-toolkit/blob/main/docs/releasing.md#check-a-download).

## Copy the files

Close the game first. Then copy the file of each game you play into that game's `ASI` folder:

| File in the zip | Copy it into |
|---|---|
| `LE1\LE1PauseAnytime.asi` | `<LE folder>\Game\ME1\Binaries\Win64\ASI` |
| `LE2\LE2PauseAnytime.asi` | `<LE folder>\Game\ME2\Binaries\Win64\ASI` |
| `LE3\LE3PauseAnytime.asi` | `<LE folder>\Game\ME3\Binaries\Win64\ASI` |

Copy the `.asi` file itself, not the folder it is in. Keep only one copy of it in each `ASI` folder.

## Check that it loaded

Start the game, then open `<LE folder>\Game\ME<n>\Binaries\Win64\Logs\PauseAnytime.log`. It has a line that ends in
`LE<n>PauseAnytime N ready`, where N is the version. For example:

```text
[14:02:11.532 info] (LE1PauseAnytime) LE1PauseAnytime 18 ready
```

If that log is missing, or its time is from before you started the game, Pause Anytime did not load.
[troubleshooting.md](troubleshooting.md) lists the causes.

## Update

Close the game and copy the new file over the old one. The file name has no version number in it, so the new file
replaces the old one.

## Uninstall

Close the game, then delete `LE<n>PauseAnytime.asi` from that game's `ASI` folder. The log stays in
`Binaries\Win64\Logs`, and you can delete `PauseAnytime.log` too.

## Build from source

To build the files yourself, see [BUILDING.md](../BUILDING.md).
