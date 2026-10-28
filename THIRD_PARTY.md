# Third-party code in MelePause

MelePause is GPL-3.0, like the rest of this repo (see [`../../LICENSE`](../../LICENSE)). It builds with, or follows, the code below.

| What | Where it comes from | License | How we use it |
|---|---|---|---|
| [LExDialoguePauser](https://github.com/hypermorphicmods/LExDialoguePauser) | v2.1.1 (`DialoguePauser.cpp`), by hypermorphic | none stated in the repo | Not copied. `src/Pause.cpp` follows its approach: Esc read in a hooked `Console.InputKey`, which still runs while the world is paused; the game's own world pause; text drawn after `BioHUD.PostRender`. |
| [ME3Tweaks/LExASIs](https://github.com/ME3Tweaks/LExASIs) | commit `aaad03a` | none stated in the repo | Build tree only: the SDK (LExSDKv2), the ASI loader interface (SPI), `Common` (logger, globals), spdlog and the `asi_target` CMake helper. The SDK, `Common` and spdlog are compiled into `MelePause.asi`. Not part of this repo; `build.sh` clones it. |
| [spdlog](https://github.com/gabime/spdlog) | the copy in LExASIs at `aaad03a` | MIT | The log file, through LExASIs' `Common`. Compiled in. |

If `MelePause.asi` is ever shared, ship the GPL-3.0 text and spdlog's MIT notice with it.
