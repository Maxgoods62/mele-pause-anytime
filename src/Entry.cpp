#include "PauseAnytime/Pause.hpp"
#include "PauseAnytime/game/Game.hpp"
#include "PauseAnytime/SharedVersion.h"
#include "Common/DefaultLogger.hpp"

#include <string>


SPI_PLUGINSIDE_SUPPORT(SDK_TARGET_NAME_W ASI_NAME_NO_SPACE_W, DEVELOPER_W, L"" VERSION_STRING_W, SPI_GAME_SDK_TARGET, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_ASYNCATTACH;


extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
    /// The ASI loader loads every file of the ASI folder before it attaches any, so an older copy left there under an
    /// old name (Game::LegacyModules) is already loaded here, whichever of the two attaches first. Not this file
    /// itself, renamed to that name by hand. Returns the first old name loaded, or null.
    wchar_t const* LegacyLoaded()
    {
        for (wchar_t const* const name : ::PauseAnytime::Game::LegacyModules)
        {
            HMODULE const legacy = GetModuleHandleW(name);
            if (legacy != nullptr && legacy != reinterpret_cast<HMODULE>(&__ImageBase)) return name;
        }
        return nullptr;
    }
}

SPI_IMPLEMENT_ATTACH
{
    // The loader names each hook after its owner (LESDK's Initializer: "<owner>_<hook>"), and an older copy's owner is its
    // old name, so the loader would take both copies' hooks and each would toggle the pause on the same key press. With an
    // old one loaded, this one installs nothing and leaves the pause to it (the old one writes Logs\MelePause.log). With no
    // log of its own, it says so to a debugger (DebugView); the loader logs the false it returns as an attach error in
    // Logs\bink2w64_proxy.log (an async attach's result is dropped, but only the first ASI attaches async).
    if (wchar_t const* const legacy = LegacyLoaded())
    {
        std::wstring const why = std::wstring{ SDK_TARGET_NAME_W ASI_NAME_NO_SPACE_W L" " VERSION_STRING_W L" stays off: " }
            + legacy + L", an older version, is loaded too. Delete it from the ASI folder.\n";
        OutputDebugStringW(why.c_str());
        return false;
    }

    // Logs\PauseAnytime.log next to the game's executable, rewritten at each start.
    Common::SetupDefaultLogger(SDK_TARGET_NAME_A, ASI_NAME_NO_SPACE_A);
    ::LESDK::Initializer Init{ InterfacePtr, SDK_TARGET_NAME_A ASI_NAME_NO_SPACE_A };

    ::PauseAnytime::InitializeGlobals(Init);
    ::PauseAnytime::InstallHooks(Init);
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
