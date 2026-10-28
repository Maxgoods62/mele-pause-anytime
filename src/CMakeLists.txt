# Built inside a checkout of ME3Tweaks/LExASIs (see ../build.sh), which provides asi_target, Common and LESDK.

asi_target (MelePause
  SHARED
  GAMES "LE1"
  SOURCES
    "Bink.cpp"
    "Entry.cpp"
    "Pause.cpp"
    "Pause.hpp"
    "SharedVersion.h"
  VLINKS
    "Common"
    "LESDK"
  )

# The one file the game-install rule lets this tool copy into the ASI folder is MelePause.asi.
set_target_properties (MelePause-LE1 PROPERTIES OUTPUT_NAME "MelePause")
