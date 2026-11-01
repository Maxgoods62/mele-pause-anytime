# Third-party code in MelePause

MelePause is GPL-3.0 ([`LICENSE`](LICENSE)). It builds with, or follows, the code below. Only the Exo 2 fonts are in this repo; `build.sh` fetches LExASIs, with its submodules, and
Dear ImGui.

| What | Where it comes from | License | How MelePause uses it |
|---|---|---|---|
| [ME3Tweaks/LExASIs](https://github.com/ME3Tweaks/LExASIs) | commit `aaad03a` | none published | The build tree. `build.sh` clones it. It provides `Common` (the logger and shared globals), the `asi_target` CMake helper, and these submodules: LExSDKv2, spdlog and simdutf. `Common` is compiled into `LE<n>MelePause.asi`. |
| [LExSDKv2](https://github.com/llamathings/LExSDKv2) | the LExASIs submodule at `aaad03a` | none published (its README explains why) | The SDK for the game's classes and functions, compiled into `LE<n>MelePause.asi`. Its `External/SPI.h` is the plugin interface (SPI) of the ASI loader, [LEBinkProxy](https://github.com/ME3Tweaks/LEBinkProxy), through which MelePause is loaded and installs its hooks. |
| [spdlog](https://github.com/gabime/spdlog) | the LExASIs submodule at `aaad03a` | MIT | The log file, through LExASIs' `Common`. Compiled in. |
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.93.0, commit `c1aaac65e896a45f81f47a4d68f9959e0f86c859` | MIT | The pause screen's text and icons (`src/Overlay.cpp`): the core and the DirectX 11 backend, compiled in. `build.sh` fetches it next to the LExASIs checkout. |
| [Exo 2](https://github.com/googlefonts/Exo-2.0) | `Exo2[wght].ttf` from [google/fonts](https://github.com/google/fonts/tree/main/ofl/exo2), instanced at weights 400 and 600 and cut down to Latin, Latin Extended-A and Cyrillic with fontTools (`src/fonts/`) | SIL Open Font License 1.1 ([`src/fonts/OFL.txt`](src/fonts/OFL.txt)); no Reserved Font Name | The pause screen's font, compiled into `LE<n>MelePause.asi` as resources (`src/MelePause.rc`). LE1 sets the screen in the game's own fonts where it can read them. Those are not in this repo or in the ASI. |
| [LExDialoguePauser](https://github.com/hypermorphicmods/LExDialoguePauser) | v2.1.1 (`DialoguePauser.cpp`), by hypermorphic | GPL-3.0 | No code copied. `src/Pause.cpp` follows its approach: Esc read in a hooked `Console.InputKey`, which still runs while the world is paused; the game's own world pause; text drawn after `BioHUD.PostRender`. |
| Dual filter blur (Marius Bjørge, "Bandwidth-Efficient Rendering", SIGGRAPH 2015) and interleaved gradient noise (Jorge Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014) | the talks | algorithms, no code copied | `src/Blur.cpp`: the pause screen's blur passes and the dither of its darkened gradients. |

A shared build of `LE<n>MelePause.asi` must come with the GPL-3.0 text (`LICENSE`), the MIT notices of spdlog and Dear
ImGui, and Exo 2's `OFL.txt`.
