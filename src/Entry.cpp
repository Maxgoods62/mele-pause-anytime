#include "MelePause/Pause.hpp"
#include "MelePause/game/Game.hpp"
#include "MelePause/SharedVersion.h"
#include "Common/DefaultLogger.hpp"

#include <string>


SPI_PLUGINSIDE_SUPPORT(SDK_TARGET_NAME_W ASI_NAME_NO_SPACE_W, DEVELOPER_W, L"" VERSION_STRING_W, SPI_GAME_SDK_TARGET, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_ASYNCATTACH;


extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
    /// The ASI loader loads every file of the ASI folder before it attaches any, so an older copy left there under its
    /// old name (Game::LegacyModule) is already loaded here, whichever of the two attaches first. Not this file itself,
    /// renamed to that name by hand.
    bool LegacyLoaded()
    {
        if (::MelePause::Game::LegacyModule == nullptr) return false;
        HMODULE const legacy = GetModuleHandleW(::MelePause::Game::LegacyModule);
        return legacy != nullptr && legacy != reinterpret_cast<HMODULE>(&__ImageBase);
    }
}

SPI_IMPLEMENT_ATTACH
{
    // Both copies install their hooks under the same name, so the loader refuses the second copy's, but each copy would
    // rewrite Logs\MelePause.log at its start: with the old one loaded, this one installs nothing and leaves the log to
    // it. With no log of its own, it says so to a debugger (DebugView); the loader logs the false it returns as an attach
    // error in Logs\bink2w64_proxy.log (an async attach's result is dropped, but only the first ASI attaches async).
    if (LegacyLoaded())
    {
        std::wstring const why = std::wstring{ SDK_TARGET_NAME_W ASI_NAME_NO_SPACE_W L" " VERSION_STRING_W L" stays off: " }
            + ::MelePause::Game::LegacyModule + L", an older version, is loaded too. Delete it from the ASI folder.\n";
        OutputDebugStringW(why.c_str());
        return false;
    }

    // Logs\MelePause.log next to the game's executable, rewritten at each start.
    Common::SetupDefaultLogger(SDK_TARGET_NAME_A, ASI_NAME_NO_SPACE_A);
    ::LESDK::Initializer Init{ InterfacePtr, SDK_TARGET_NAME_A ASI_NAME_NO_SPACE_A };

    ::MelePause::InitializeGlobals(Init);
    ::MelePause::InstallHooks(Init);
    // if constexpr, not #if: without game/Game.hpp the switch is a compile error, not a silent 0.
    if constexpr (MELEPAUSE_MOVIES) ::MelePause::Bink::Install(Init);
    else LEASI_INFO("movie pause: not in the {} build", SDK_TARGET_NAME_A);
    ::MelePause::Overlay::Install(Init);

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
