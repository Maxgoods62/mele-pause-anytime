#pragma once

// Identity the ASI loader (SPI) logs and checks, and PauseAnytime.rc's version info; same macro names as the LExASIs
// projects.

#define DEVELOPER_A "Maxgoods"
#define DEVELOPER_W L"Maxgoods"
#define ASI_NAME_A "Pause Anytime"
#define ASI_NAME_W L"Pause Anytime"
#define ASI_NAME_NO_SPACE_A "PauseAnytime"
#define ASI_NAME_NO_SPACE_W L"PauseAnytime"

// One version for every game: a number and a minor number. 1.0 is the first public release; the test builds before it
// were numbered 2 to 18. The log and the loader print the strings, and package.sh checks that both are
// ASI_VERSION_MAJOR.ASI_VERSION_MINOR.
#define ASI_VERSION_MAJOR 1
#define ASI_VERSION_MINOR 0
#define VERSION_STRING_A "1.0"
#define VERSION_STRING_W L"1.0"

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

// The four-part version of the LExASIs scheme, with the minor number where it has 0: major.minor.game id.group id, for
// PauseAnytime.rc only.
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define APP_VERSION_RC ASI_VERSION_MAJOR, ASI_VERSION_MINOR, ASI_GAME_ID_RC, ASI_GROUP_ID_RC
#define VERSION_STRING_RC TOSTRING(ASI_VERSION_MAJOR) "." TOSTRING(ASI_VERSION_MINOR) "." TOSTRING(ASI_GAME_ID_RC) "." TOSTRING(ASI_GROUP_ID_RC)
