# Third-party code in MelePause

MelePause is GPL-3.0, like the rest of this repo (see [`../../LICENSE`](../../LICENSE)). It builds with, or follows, the code below.

| What | Where it comes from | License | How we use it |
|---|---|---|---|
| [LExDialoguePauser](https://github.com/hypermorphicmods/LExDialoguePauser) | v2.1.1 (`DialoguePauser.cpp`), by hypermorphic | none stated in the repo | Not copied. `src/Pause.cpp` follows its approach: Esc read in a hooked `Console.InputKey`, which still runs while the world is paused; the game's own world pause; text drawn after `BioHUD.PostRender`. |
| [ME3Tweaks/LExASIs](https://github.com/ME3Tweaks/LExASIs) | commit `aaad03a` | none stated in the repo | Build tree only: the SDK (LExSDKv2), the ASI loader interface (SPI), `Common` (logger, globals), spdlog and the `asi_target` CMake helper. The SDK, `Common` and spdlog are compiled into `MelePause.asi`. Not part of this repo; `build.sh` clones it. |
| [spdlog](https://github.com/gabime/spdlog) | the copy in LExASIs at `aaad03a` | MIT | The log file, through LExASIs' `Common`. Compiled in. |
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.93.0, commit `c1aaac65` (the same checkout as mele-bridge's, `D:\MELE\asi-build\third_party\imgui`) | MIT | The pause screen's text and icons (`src/Overlay.cpp`): the core and the DirectX 11 backend, compiled in. Not part of this repo; `build.sh` fetches it. |
| [Exo 2](https://github.com/googlefonts/Exo-2.0) | `Exo2[wght].ttf` from [google/fonts](https://github.com/google/fonts/tree/main/ofl/exo2), instanced at weights 300, 400 and 600 and cut down to Latin, Latin Extended-A and Cyrillic with fontTools (`src/fonts/`) | SIL Open Font License 1.1 ([`src/fonts/OFL.txt`](src/fonts/OFL.txt)); no Reserved Font Name | The pause screen's font, compiled into `MelePause.asi` as resources (`src/Resources.rc`). |
| Dual filter blur (Marius Bjørge, "Bandwidth-Efficient Rendering", SIGGRAPH 2015) and interleaved gradient noise (Jorge Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014) | the talks | algorithms, no code copied | `src/Blur.cpp`: the pause screen's blur passes and the dither of its darkened gradients. |

If `MelePause.asi` is ever shared, ship the GPL-3.0 text, spdlog's and Dear ImGui's MIT notices, and Exo 2's `OFL.txt` with it.
