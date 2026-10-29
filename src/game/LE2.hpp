#pragma once

// LE2's facts. Include game/Game.hpp, not this file.

/// Photo mode under the pause: off. PhotoLE1.cpp hooks MassEffect1.exe natives by address, and LE2 has no
/// WorldInfo.GameTimeSeconds, AudioComponent.bNonRealtime or MassEffectGuiManager.m_oConversationPanel.
#define MELEPAUSE_PHOTO 0
/// The Bink movie pause: off. 164 of the 166 movies in BioGame\Movies have no Bink audio track (nor do the DLC's):
/// their sound is a Wwise event that BinkPause does not stop.
#define MELEPAUSE_MOVIES 0

namespace MelePause::Game
{
    // SFXGameModeManager.EGameModes (SFXGame_classes.hpp:4054-4071). Same numbers as LE1 for these four; other names.
    inline constexpr unsigned char ModeConversation = 5;
    inline constexpr unsigned char ModeCinematic = 6;
    inline constexpr unsigned char ModeMovie = 8;
    inline constexpr unsigned char ModePhoto = 11;
    inline constexpr char const* ModeNames[] = { "Default", "PowerWheel", "WeaponWheel", "Command", "Vehicle", "Conversation",
                                                 "Cinematic", "GUI", "Movie", "Galaxy", "Orbital", "Photo", "CheatMenu" };

    // BioGuiEvents the always-on Escape and Start binds send to Scaleform, pressed and released: BioPlayerInput's
    // StaticPCBinds and StaticConsoleBinds (SFXGame_classes.hpp:13169-13170, defaults in SFXGame.pcc). In LE2 Escape
    // sends BUTTON_B, not KEY_ESCAPE (Engine_classes.hpp:1085, 1106); Start sends BUTTON_START (:1093, :1114).
    inline constexpr unsigned char GuiStart = 21;
    inline constexpr unsigned char GuiEscape = 13;
    inline constexpr unsigned char GuiStartRelease = 42;
    inline constexpr unsigned char GuiEscapeRelease = 34;

    /// SFXGameModePhoto's Esc and Start bind, which opens the pause menu (SFXGame_classes.hpp:27326).
    inline constexpr wchar_t const* PhotoMenuFunction = L"ShowMenu";

    /// The name an older version had in the ASI folder, or null: LE2 never had one.
    inline constexpr wchar_t const* LegacyModule = nullptr;

    /// The controller's game modes (BioPlayerController.GameModeManager2, SFXGame_classes.hpp:5535).
    inline USFXGameModeManager* ModeManagerOf(ABioPlayerController* pc)
    {
        return pc != nullptr ? pc->GameModeManager2 : nullptr;
    }
}
