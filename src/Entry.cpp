#include "MelePause/Pause.hpp"
#include "MelePause/game/Game.hpp"
#include "MelePause/SharedVersion.h"
#include "Common/DefaultLogger.hpp"


SPI_PLUGINSIDE_SUPPORT(SDK_TARGET_NAME_W ASI_NAME_NO_SPACE_W, DEVELOPER_W, L"" VERSION_STRING_W, SPI_GAME_SDK_TARGET, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_ASYNCATTACH;


SPI_IMPLEMENT_ATTACH
{
    // Logs\MelePause.log next to the game's executable, rewritten at each start.
    Common::SetupDefaultLogger(SDK_TARGET_NAME_A, ASI_NAME_NO_SPACE_A);
    ::LESDK::Initializer Init{ InterfacePtr, SDK_TARGET_NAME_A ASI_NAME_NO_SPACE_A };

    ::MelePause::InitializeGlobals(Init);
    ::MelePause::InstallHooks(Init);
    // if constexpr, not #if: without game/Game.hpp the switch is a compile error, not a silent 0.
    if constexpr (MELEPAUSE_MOVIES) ::MelePause::Bink::Install(Init);
    else LEASI_INFO("movie pause: not in the {} build", SDK_TARGET_NAME_A);
    ::MelePause::Overlay::Install(Init);

    LEASI_INFO("MelePause {} ready", VERSION_STRING_A);
    LEASI_FLUSH();
    return true;
}

SPI_IMPLEMENT_DETACH
{
    LEASI_UNUSED(InterfacePtr);
    Common::ShutdownLogger();
    return true;
}
