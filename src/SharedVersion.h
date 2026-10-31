#pragma once

// Identity the ASI loader (SPI) logs and checks, and MelePause.rc's version info; same macro names as the LExASIs
// projects.

#define DEVELOPER_A "Maxgoods"
#define DEVELOPER_W L"Maxgoods"
#define ASI_NAME_A "Mele Pause"
#define ASI_NAME_W L"Mele Pause"
#define ASI_NAME_NO_SPACE_A "MelePause"
#define ASI_NAME_NO_SPACE_W L"MelePause"

// One version for every game. The log and the loader print it as a plain number, and package.sh checks that the two
// strings are ASI_VERSION.
#define ASI_VERSION 17
#define VERSION_STRING_A "17"
#define VERSION_STRING_W L"17"

// The game this build is for: its file name prefix, and the game and group ids of ME3Tweaks' tooling. Group 0 until
// ME3Tweaks assigns one.
#if defined(SDK_TARGET_LE1)
#define GAME_PREFIX_RC "LE1"
#define ASI_GAME_ID_RC 4
#define ASI_GROUP_ID_RC 0
#define ASI_DESCRIPTION "Pauses conversations, cutscenes and movies in Mass Effect 1 (LE1)"
#endif

#if defined(SDK_TARGET_LE2)
#define GAME_PREFIX_RC "LE2"
#define ASI_GAME_ID_RC 5
#define ASI_GROUP_ID_RC 0
#define ASI_DESCRIPTION "Pauses conversations, cutscenes and movies in Mass Effect 2 (LE2)"
#endif

#if defined(SDK_TARGET_LE3)
#define GAME_PREFIX_RC "LE3"
#define ASI_GAME_ID_RC 6
#define ASI_GROUP_ID_RC 0
#define ASI_DESCRIPTION "Pauses conversations, cutscenes and movies in Mass Effect 3 (LE3)"
#endif

// The four-part version of the LExASIs scheme, version.0.game id.group id, for MelePause.rc only.
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define APP_VERSION_RC ASI_VERSION, 0, ASI_GAME_ID_RC, ASI_GROUP_ID_RC
#define VERSION_STRING_RC TOSTRING(ASI_VERSION) ".0." TOSTRING(ASI_GAME_ID_RC) "." TOSTRING(ASI_GROUP_ID_RC)
