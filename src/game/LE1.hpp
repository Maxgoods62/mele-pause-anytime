#pragma once

// LE1's facts. Include game/Game.hpp, not this file.

/// Photo mode under the pause: five MassEffect1.exe natives get past it.
#define MELEPAUSE_PHOTO 1
/// The Bink movie pause: the galaxy map's travel movie carries its own sound track.
#define MELEPAUSE_MOVIES 1

namespace MelePause::Game
{
    // SFXGameModeManager.EGameModes (SFXGame_classes.hpp).
    inline constexpr unsigned char ModeConversation = 5;
    inline constexpr unsigned char ModeCinematic = 6;
    inline constexpr unsigned char ModeMovie = 8;
    inline constexpr unsigned char ModePhoto = 11;
    inline constexpr char const* ModeNames[] = { "Default", "Vehicle", "PowerWheel", "WeaponWheel", "Command", "Conversation",
                                                 "Cinematic", "GUI", "Movie", "Galaxy", "Minigame", "Photo", "FlyCam" };

    // BioGuiEvents the always-on Escape and Start binds send to Scaleform: Start and Escape, pressed and released
    // (Engine_classes.hpp).
    inline constexpr unsigned char GuiStart = 21;
    inline constexpr unsigned char GuiEscape = 24;
    inline constexpr unsigned char GuiStartRelease = 43;
    inline constexpr unsigned char GuiEscapeRelease = 46;

    /// SFXGameModePhoto's Esc and Start bind, which opens the pause menu.
    inline constexpr wchar_t const* PhotoMenuFunction = L"ShowMenuGui";

    /// The name an older version had in the ASI folder (here v6 and older), or null: Entry.cpp stays off while it is
    /// loaded.
    inline constexpr wchar_t const* LegacyModule = L"MelePause.asi";

    /// The controller's game modes (BioPlayerController.GameModeManager).
    inline USFXGameModeManager* ModeManagerOf(ABioPlayerController* pc)
    {
        return pc != nullptr ? pc->GameModeManager : nullptr;
    }
}
