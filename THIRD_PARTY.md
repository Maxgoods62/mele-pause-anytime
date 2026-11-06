# Third-party code in Pause Anytime

Pause Anytime is GPL-3.0 ([LICENSE](LICENSE)). The only third-party files in this repo are the Exo 2 fonts in
`src/fonts/`. `build.sh` downloads LExASIs, with its submodules, and Dear ImGui, each at a pinned commit
([BUILDING.md](BUILDING.md)).

## In the ASI

| Component | Version | License | What Pause Anytime uses it for |
|---|---|---|---|
| [LExASIs](https://github.com/ME3Tweaks/LExASIs) | commit `aaad03ab6e5112e27ff5528f7b08c3bcf3870c31` | none published | The build tree and its CMake helper `asi_target`. Only its `Common` part (the logger and shared globals) goes into the ASI. |
| [LExSDKv2](https://github.com/llamathings/LExSDKv2) | LExASIs submodule, commit `3885be14113816ff1d2a0d941aa96bba6df9dc58` | none published (its README says why) | The game's classes and functions. Its `External/SPI.h` is the plugin interface of the ASI loader: the loader loads Pause Anytime through it, and Pause Anytime installs its hooks through it. |
| [spdlog](https://github.com/gabime/spdlog) | LExASIs submodule, commit `27cb4c76708608465c413f6d0e6b8d99a4d84302` (1.14.1) | MIT | Writes the log file, through `Common`. |
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.93.0, commit `c1aaac65e896a45f81f47a4d68f9959e0f86c859` | MIT | Draws the pause screen's text and icons. Its core and its DirectX 11 backend are compiled in. |
| stb_truetype 1.26, stb_rect_pack 1.01, stb_textedit 1.14, by Sean Barrett | the copies in Dear ImGui (`imstb_*.h`) | MIT or public domain, your choice | Compiled in with Dear ImGui. stb_truetype and stb_rect_pack load the fonts into its atlas. stb_textedit comes with its widgets. |
| ProggyClean and ProggyForever, Dear ImGui's built-in fonts | the ones in Dear ImGui | MIT | Used only if Exo 2 fails to load. |
| [Exo 2](https://github.com/googlefonts/Exo-2.0) | `src/fonts/Exo2-Regular.ttf` and `Exo2-SemiBold.ttf` | SIL Open Font License 1.1 | The pause screen's font when the game's own fonts can't be used. Compiled in as resources. |

The two Exo 2 files were made with fontTools from `Exo2[wght].ttf` in
[google/fonts](https://github.com/google/fonts/tree/main/ofl/exo2). They are instances at weights 400 and 600, cut down
to Latin, Latin Extended-A and Cyrillic. The font has no Reserved Font Name.

## Used but not in the ASI

- simdutf, a LExASIs submodule at commit `cd79a258af781b5fd92da3c671a2d679b97be227`, Apache-2.0 or MIT. It is built in
  the tree, but nothing that Pause Anytime links uses it.
- [LEBinkProxy](https://github.com/ME3Tweaks/LEBinkProxy), the ASI loader. Players install it: ME3Tweaks Mod Manager
  adds it with any mod. Pause Anytime uses only its plugin interface, `SPI.h` in LExSDKv2.
- The Visual C++ runtime, Direct3D 11, DXGI and `d3dcompiler_47.dll` come from Microsoft, with Windows or the Visual C++
  Redistributable. Pause Anytime ships none of them.
- In each game, the pause screen uses the game's own fonts when Pause Anytime can read them from the game's memory.
  Nothing of those fonts is in this repo, the ASI or the zip.

## Ideas, no code copied

- [LExDialoguePauser](https://github.com/hypermorphicmods/LExDialoguePauser) v2.1.1 by hypermorphic, GPL-3.0.
  `src/Pause.cpp` takes three ideas from it: read Esc in a hooked `Console.InputKey`, which still runs while the world
  is paused; use the game's own world pause; draw text after `BioHUD.PostRender`.
- `src/Blur.cpp` uses the dual filter blur for its blur passes (Marius Bjørge, "Bandwidth-Efficient Rendering",
  SIGGRAPH 2015). It dithers the darkened gradients with interleaved gradient noise (Jorge Jimenez, "Next Generation
  Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014).

## Notices

- spdlog: MIT License, Copyright (c) 2016 Gabi Melman.
- Dear ImGui: MIT License, Copyright (c) 2014-2026 Omar Cornut.
- stb_truetype, stb_rect_pack and stb_textedit: MIT License, Copyright (c) 2017 Sean Barrett, or public domain
  (www.unlicense.org).
- ProggyClean: MIT License, Copyright (c) 2004, 2005 Tristan Grimmer.
- ProggyForever: MIT License, Copyright (c) 2026 Disco Hello, Copyright (c) 2019, 2023 Tristan Grimmer.
- Exo 2: Copyright 2013 The Exo 2 Project Authors (https://github.com/googlefonts/Exo-2.0), SIL Open Font License 1.1.

## In the release zip

`package.sh` puts these files in the zip's `LICENSE` folder. If you share your own build, include them too.

| File | What it is |
|---|---|
| `LICENSE` | GPL-3.0, the license of Pause Anytime |
| `THIRD_PARTY.md` | this page, with the notices above |
| `OFL.txt` | the Exo 2 license, from `src/fonts/OFL.txt` |
| `LICENSE-spdlog.txt` | spdlog's MIT license, from the build tree |
| `LICENSE-imgui.txt` | Dear ImGui's MIT license, from the build tree |

The stb and Proggy notices have no file of their own: they are on this page and in Dear ImGui's sources. LExASIs and
LExSDKv2 publish no license, so the zip has no text for them.
