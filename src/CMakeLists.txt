# Built inside a checkout of ME3Tweaks/LExASIs (see ../build.sh), which provides asi_target, Common and LESDK.

# Dear ImGui (MIT, see ../THIRD_PARTY.md), fetched at a pinned commit by build.sh into the third_party folder next to this
# repo's LExASIs checkout. A library of our own, built with its defaults, not the ASI targets' /W4 /WX; its name differs
# from mele-bridge's MeleImGui, so both can still share one tree.
set (MELE_THIRD_PARTY "${CMAKE_SOURCE_DIR}/../third_party" CACHE PATH "Where build.sh put imgui")
add_library (PauseAnytimeImGui STATIC
  "${MELE_THIRD_PARTY}/imgui/imgui.cpp"
  "${MELE_THIRD_PARTY}/imgui/imgui_draw.cpp"
  "${MELE_THIRD_PARTY}/imgui/imgui_tables.cpp"
  "${MELE_THIRD_PARTY}/imgui/imgui_widgets.cpp"
  "${MELE_THIRD_PARTY}/imgui/backends/imgui_impl_dx11.cpp"
  )
target_include_directories (PauseAnytimeImGui PUBLIC "${MELE_THIRD_PARTY}/imgui")
set_target_properties (PauseAnytimeImGui PROPERTIES FOLDER "External")

# One target per game from the same sources, PauseAnytime-LE<n>, which builds LE<n>PauseAnytime.asi and .pdb: LExASIs'
# default names, so the games' files never collide in Build/<CONFIG>. List a game only once it has game/LE<n>.hpp (the
# names LE1.hpp defines) and its branch in game/Game.hpp: the VisualStudio preset turns every LEASI_BUILD_LE<n> on.
# Listed is not released: a plain ../build.sh builds RELEASED_GAMES (../scripts/games.sh).
asi_target (PauseAnytime
  SHARED
  GAMES "LE1" "LE2" "LE3"
  SOURCES
    "Api.hpp"
    "Bink.cpp"
    "Blur.cpp"
    "Blur.hpp"
    "Engine.cpp"
    "Engine.hpp"
    "Entry.cpp"
    "GameFont.cpp"
    "GameFont.hpp"
    "PauseAnytime.rc"
    "MovieInput.cpp"
    "MovieSound.cpp"
    "Overlay.cpp"
    "Pause.cpp"
    "Pause.hpp"
    "Photo.hpp"
    "Photo.cpp"
    "PhotoGui.hpp"
    "PhotoGuiGFx.cpp"
    "PhotoGuiSF.cpp"
    "game/Game.hpp"
    "game/KismetMovie.hpp"
    "game/LE1.hpp"
    "game/LE2.hpp"
    "game/LE3.hpp"
    "Resources.h"
    "SharedVersion.h"
  LINKS
    "PauseAnytimeImGui"
    "d3d11"
    "dxgi"
    "delayimp"
  VLINKS
    "Common"
    "LESDK"
  )

# No path of the build machine in the ASI, which may name a user's folder (build.sh's default tree is in %LOCALAPPDATA%):
# __FILE__, in the log macros and in fatal errors' "at:" lines, starts below the folder that holds LExASIs and
# third_party, in our code, Common, LESDK and ImGui (spdlog's Release code holds no path). It is /d1trimfile because
# /pathmap needs /experimental:deterministic, and under /WX that makes any __DATE__ an error.
get_filename_component (MELE_BUILD_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
file (TO_NATIVE_PATH "${MELE_BUILD_ROOT}" MELE_BUILD_ROOT)
set (MELE_TRIM_PATHS "/d1trimfile:${MELE_BUILD_ROOT}")
target_compile_options (PauseAnytimeImGui PRIVATE "${MELE_TRIM_PATHS}")

# A game left out of GAMES, or turned off with LEASI_BUILD_LE<n>, has no target.
foreach (game IN ITEMS LE1 LE2 LE3)
  if (TARGET PauseAnytime-${game})
    # imgui_impl_dx11.cpp links d3dcompiler (a pragma) and calls D3DCompile: delay-loaded, so the ASI loads without the
    # DLL and Overlay.cpp binds it to System32's copy first, never to a d3dcompiler_47.dll next to the game.
    # /PDBALTPATH: the ASI names its .pdb by file name only, not by the build folder's path. Not the linker's %_PDB%: the
    # Visual Studio generator escapes its % signs, and the ASI would name "%%%LE1PauseAnytime.pdb%%%".
    target_link_options (PauseAnytime-${game} PRIVATE "/DELAYLOAD:d3dcompiler_47.dll"
      "/PDBALTPATH:$<TARGET_PDB_FILE_NAME:PauseAnytime-${game}>")
    foreach (part IN ITEMS PauseAnytime Common LESDK)
      target_compile_options (${part}-${game} PRIVATE "${MELE_TRIM_PATHS}")
    endforeach ()
  endif ()
endforeach ()
