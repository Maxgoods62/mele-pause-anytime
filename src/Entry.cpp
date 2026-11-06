#include "PauseAnytime/Pause.hpp"
#include "PauseAnytime/game/Game.hpp"
#include "PauseAnytime/SharedVersion.h"
#include "Common/DefaultLogger.hpp"


SPI_PLUGINSIDE_SUPPORT(SDK_TARGET_NAME_W ASI_NAME_NO_SPACE_W, DEVELOPER_W, L"" VERSION_STRING_W, SPI_GAME_SDK_TARGET, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_ASYNCATTACH;


SPI_IMPLEMENT_ATTACH
{
    // Logs\PauseAnytime.log next to the game's executable, rewritten at each start.
    Common::SetupDefaultLogger(SDK_TARGET_NAME_A, ASI_NAME_NO_SPACE_A);
    ::LESDK::Initializer Init{ InterfacePtr, SDK_TARGET_NAME_A ASI_NAME_NO_SPACE_A };

    ::PauseAnytime::InitializeGlobals(Init);
    if (!::PauseAnytime::InstallHooks(Init)) return false;
    ::PauseAnytime::Bink::Install(Init, ::PauseAnytime::Game::Movies, ::PauseAnytime::Game::BinkAudioMovies, ::PauseAnytime::Game::MovieLanguages,
                               ::PauseAnytime::Game::MoviePlayer);
    ::PauseAnytime::MovieInput::Install(Init, ::PauseAnytime::Game::MovieInputRva, ::PauseAnytime::Game::MovieInputStart,
                                     ::PauseAnytime::Game::MovieSkipKeyAs);
    ::PauseAnytime::Overlay::Install(Init, ::PauseAnytime::Game::Look);

    LEASI_INFO("{} {} ready", SDK_TARGET_NAME_A ASI_NAME_NO_SPACE_A, VERSION_STRING_A);
    LEASI_FLUSH();
    return true;
}

SPI_IMPLEMENT_DETACH
{
    LEASI_UNUSED(InterfacePtr);
    Common::ShutdownLogger();
    return true;
}
