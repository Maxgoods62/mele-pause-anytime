#include "PauseAnytime/Pause.hpp"
#include "PauseAnytime/game/Game.hpp"
#include "PauseAnytime/SharedVersion.h"
#include "Common/DefaultLogger.hpp"

#include <format>
#include <string>


SPI_PLUGINSIDE_SUPPORT(SDK_TARGET_NAME_W ASI_NAME_NO_SPACE_W, DEVELOPER_W, L"" VERSION_STRING_W, SPI_GAME_SDK_TARGET, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_ASYNCATTACH;

namespace
{
    /// One per game process, made by the first copy of this file to attach: a later copy (the same file under another
    /// name, which the loader gives the same hook names) counts itself in it and stays off.
    HANDLE Copies = nullptr;
}

SPI_IMPLEMENT_ATTACH
{
    // Before the log: each copy would rewrite the file, and two copies would write it through two handles. A later copy
    // writes nothing and hooks nothing; the first one names it after its "ready" line.
    auto const name = std::format(L"Local\\" SDK_TARGET_NAME_W ASI_NAME_NO_SPACE_W L"-{}", GetCurrentProcessId());
    Copies = CreateSemaphoreW(nullptr, 0, 64, name.c_str());
    auto const copiesError = GetLastError();
    if (Copies != nullptr && copiesError == ERROR_ALREADY_EXISTS)
    {
        ReleaseSemaphore(Copies, 1, nullptr);
        CloseHandle(Copies);
        Copies = nullptr;
        return false;
    }

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
    // A copy that attached before this line; one attaching later goes unnamed, and stays off all the same.
    if (Copies == nullptr) LEASI_WARN("no check for a second copy of this file in the game: CreateSemaphore failed, error {}", copiesError);
    else if (WaitForSingleObject(Copies, 0) == WAIT_OBJECT_0)
        LEASI_WARN("a second copy of " SDK_TARGET_NAME_A ASI_NAME_NO_SPACE_A ".asi is in the ASI folder under another file name: it stays "
                   "off. Keep only one");
    LEASI_FLUSH();
    return true;
}

SPI_IMPLEMENT_DETACH
{
    LEASI_UNUSED(InterfacePtr);
    Common::ShutdownLogger();
    return true;
}
