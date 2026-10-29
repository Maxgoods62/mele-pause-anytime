# Built inside a checkout of ME3Tweaks/LExASIs (see ../build.sh), which provides asi_target, Common and LESDK.

# Dear ImGui (MIT, see ../THIRD_PARTY.md), fetched at a pinned commit by build.sh into the third_party folder next to this
# repo's LExASIs checkout. A library of our own, built with its defaults, not the ASI targets' /W4 /WX; its name differs
# from mele-bridge's MeleImGui, so both can still share one tree.
set (MELE_THIRD_PARTY "${CMAKE_SOURCE_DIR}/../third_party" CACHE PATH "Where build.sh put imgui")
add_library (MelePauseImGui STATIC
  "${MELE_THIRD_PARTY}/imgui/imgui.cpp"
  "${MELE_THIRD_PARTY}/imgui/imgui_draw.cpp"
  "${MELE_THIRD_PARTY}/imgui/imgui_tables.cpp"
  "${MELE_THIRD_PARTY}/imgui/imgui_widgets.cpp"
  "${MELE_THIRD_PARTY}/imgui/backends/imgui_impl_dx11.cpp"
  )
target_include_directories (MelePauseImGui PUBLIC "${MELE_THIRD_PARTY}/imgui")
set_target_properties (MelePauseImGui PROPERTIES FOLDER "External")

# One target per game from the same sources, MelePause-LE<n>, which builds LE<n>MelePause.asi and .pdb: LExASIs' default
# names, so the games' files never collide in Build/<CONFIG>. List a game only once it has game/LE<n>.hpp (the names
# LE1.hpp defines, LegacyModule included) and its branch in game/Game.hpp: the VisualStudio preset turns every
# LEASI_BUILD_LE<n> on. Listed is not released: a plain ../build.sh builds RELEASED_GAMES (../scripts/games.sh).
asi_target (MelePause
  SHARED
  GAMES "LE1" "LE2"
  SOURCES
    "Api.hpp"
    "Bink.cpp"
    "Blur.cpp"
    "Blur.hpp"
    "Engine.cpp"
    "Engine.hpp"
    "Entry.cpp"
    "MelePause.rc"
    "MovieInput.cpp"
    "MovieSound.cpp"
    "Overlay.cpp"
    "Pause.cpp"
    "Pause.hpp"
    "Photo.hpp"
    "PhotoLE1.cpp"
    "PhotoOff.cpp"
    "game/Game.hpp"
    "game/LE1.hpp"
    "game/LE2.hpp"
    "Resources.h"
    "SharedVersion.h"
  LINKS
    "MelePauseImGui"
    "d3d11"
    "dxgi"
    "delayimp"
  VLINKS
    "Common"
    "LESDK"
  )

# A game left out of GAMES, or turned off with LEASI_BUILD_LE<n>, has no target.
foreach (game IN ITEMS LE1 LE2 LE3)
  if (TARGET MelePause-${game})
    # imgui_impl_dx11.cpp links d3dcompiler (a pragma) and calls D3DCompile: delay-loaded, so the ASI loads without the
    # DLL and Overlay.cpp binds it to System32's copy first, never to a d3dcompiler_47.dll next to the game.
    target_link_options (MelePause-${game} PRIVATE "/DELAYLOAD:d3dcompiler_47.dll")
  endif ()
endforeach ()
