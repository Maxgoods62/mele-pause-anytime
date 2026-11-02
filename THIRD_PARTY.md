# Third-party code in Pause Anytime

Pause Anytime is GPL-3.0 ([`LICENSE`](LICENSE)). It is built with the code below. From LExDialoguePauser and the two
SIGGRAPH talks, Pause Anytime takes only ideas and copies no code. Only the Exo 2 fonts are stored in this repo.
`build.sh` downloads LExASIs, with its submodules, and Dear ImGui.

| What | Version | License | In the ASI |
|---|---|---|---|
| [ME3Tweaks/LExASIs](https://github.com/ME3Tweaks/LExASIs) | commit `aaad03a` | none published | `Common` only |
| [LExSDKv2](https://github.com/llamathings/LExSDKv2) | the LExASIs submodule at `aaad03a` | none published (its README explains why) | yes |
| [spdlog](https://github.com/gabime/spdlog) | the LExASIs submodule at `aaad03a` | MIT | yes |
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.93.0, commit `c1aaac65e896a45f81f47a4d68f9959e0f86c859` | MIT | the core and the DirectX 11 backend |
| ProggyClean and ProggyForever, Dear ImGui's built-in fonts | the ones in Dear ImGui at `c1aaac65` | MIT | yes |
| [Exo 2](https://github.com/googlefonts/Exo-2.0) | `Exo2[wght].ttf` from [google/fonts](https://github.com/google/fonts/tree/main/ofl/exo2) | SIL Open Font License 1.1, no Reserved Font Name | yes |
| [LExDialoguePauser](https://github.com/hypermorphicmods/LExDialoguePauser) | v2.1.1 (`DialoguePauser.cpp`), by hypermorphic | GPL-3.0 | no |
| Dual filter blur and interleaved gradient noise | two SIGGRAPH talks | algorithms, no code copied | no |

## How Pause Anytime uses them

- LExASIs is the build tree, and `build.sh` clones it. It provides `Common` (the logger and shared globals), the
  `asi_target` CMake helper, and three submodules: LExSDKv2, spdlog and simdutf. `Common` is compiled into
  `LE<n>PauseAnytime.asi`. simdutf is built in the tree but not linked into the ASI.
- LExSDKv2 is the SDK for the game's classes and functions, compiled into `LE<n>PauseAnytime.asi`. Its `External/SPI.h`
  is the plugin interface (SPI) of the ASI loader, [LEBinkProxy](https://github.com/ME3Tweaks/LEBinkProxy). The loader
  loads Pause Anytime through this interface, and Pause Anytime installs its hooks through it.
- spdlog writes the log file, through LExASIs' `Common`.
- Dear ImGui draws the pause screen's text and icons (`src/Overlay.cpp`). Its core and its DirectX 11 backend are
  compiled in. `build.sh` downloads it next to the LExASIs checkout.
- ProggyClean and ProggyForever come with Dear ImGui and are compiled in with it. Pause Anytime uses one of them only
  when Exo 2 fails to load. Their notices are in Dear ImGui's `imgui_draw.cpp`:
  - ProggyClean: MIT License, Copyright (c) 2004, 2005 Tristan Grimmer.
  - ProggyForever: MIT license, Copyright (c) 2026 Disco Hello, Copyright (c) 2019,2023 Tristan Grimmer.
- Exo 2 is the pause screen's font when the game's own fonts can't be used. It is compiled into `LE<n>PauseAnytime.asi`
  as resources (`src/PauseAnytime.rc`). The two files in `src/fonts/` were made from `Exo2[wght].ttf` with fontTools:
  instanced at weights 400 and 600, and cut down to Latin, Latin Extended-A and Cyrillic. The license is
  `src/fonts/OFL.txt` in this repo and `LICENSE\OFL.txt` in the release zip.
- In each game, the pause screen uses the game's own fonts when Pause Anytime can read them from memory. Those fonts are
  not in this repo or in the ASI.
- No code of LExDialoguePauser is copied. `src/Pause.cpp` uses three of its ideas. It reads Esc in a hooked
  `Console.InputKey`, which still runs while the world is paused. It uses the game's own world pause. It draws text
  after `BioHUD.PostRender`.
- `src/Blur.cpp` uses two algorithms from the two SIGGRAPH talks, and copies no code from either:
  - The dual filter blur, for the pause screen's blur passes: Marius Bjørge, "Bandwidth-Efficient Rendering",
    SIGGRAPH 2015.
  - Interleaved gradient noise, to dither the darkened gradients: Jorge Jimenez, "Next Generation Post Processing in
    Call of Duty: Advanced Warfare", SIGGRAPH 2014.

If you share a build of `LE<n>PauseAnytime.asi`, include the GPL-3.0 text (`LICENSE`), the MIT notices of spdlog and
Dear ImGui, and Exo 2's `OFL.txt`. The release zip's `LICENSE` folder has all of them.
