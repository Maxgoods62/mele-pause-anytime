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

asi_target (MelePause
  SHARED
  GAMES "LE1"
  SOURCES
    "Bink.cpp"
    "Blur.cpp"
    "Blur.hpp"
    "Entry.cpp"
    "Overlay.cpp"
    "Pause.cpp"
    "Pause.hpp"
    "Resources.h"
    "Resources.rc"
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

# The one file the game-install rule lets this tool copy into the ASI folder is MelePause.asi.
set_target_properties (MelePause-LE1 PROPERTIES OUTPUT_NAME "MelePause")
# imgui_impl_dx11.cpp links d3dcompiler (a pragma) and calls D3DCompile: delay-loaded, so the ASI loads without the DLL and
# Overlay.cpp binds it to System32's copy first, never to a d3dcompiler_47.dll next to the game.
target_link_options (MelePause-LE1 PRIVATE "/DELAYLOAD:d3dcompiler_47.dll")
