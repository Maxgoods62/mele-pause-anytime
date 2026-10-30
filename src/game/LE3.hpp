#pragma once

// LE3's facts. Include game/Game.hpp, not this file.

/// Photo mode under the pause: off. LE3's photo panel is a GFx movie (SFXSFHandler_PhotoMode), and the
/// MassEffect3.exe natives that would get it past the pause are not known yet.
#define MELEPAUSE_PHOTO 0
#define MELEPAUSE_PHOTO_GUI_SOUNDS 0
/// The Bink movie pause: off. 125 of the 126 movies in BioGame\Movies have no Bink audio track: their sound is a Wwise
/// event that BinkPause does not stop.
#define MELEPAUSE_MOVIES 0

namespace MelePause::Game
{
    // SFXGameModeManager.EGameModes (SFXGame_classes.hpp:3210-3239).
    inline constexpr unsigned char ModeConversation = 7;
    inline constexpr unsigned char ModeCinematic = 8;
    inline constexpr unsigned char ModeMovie = 10;
    inline constexpr unsigned char ModePhoto = 14;
    inline constexpr char const* ModeNames[] = { "Default", "Vehicle", "Atlas", "PowerWheel", "WeaponWheel", "Command",
                                                 "InjuredShepard", "Conversation", "Cinematic", "GUI", "Movie", "Galaxy",
                                                 "Orbital", "MultiLand", "Photo", "CheatMenu", "AIDebug", "Prototyping",
                                                 "DreamSequence", "IllusiveManConflict", "Spectator", "Dying", "FlyCam",
                                                 "ReplicationDebug", "Lobby" };

    // BioGuiEvents the always-on Escape and Start binds send to Scaleform, pressed and released: BioPlayerInput's
    // StaticPCBinds and StaticConsoleBinds (defaults in SFXGame.pcc). As in LE2, Escape sends BUTTON_B, not KEY_ESCAPE;
    // Start sends BUTTON_START (SFXGame_classes.hpp:1126, BioGuiEvents is in SFXGame here).
    inline constexpr unsigned char GuiStart = 24;
    inline constexpr unsigned char GuiEscape = 16;
    inline constexpr unsigned char GuiStartRelease = 45;
    inline constexpr unsigned char GuiEscapeRelease = 37;

    /// SFXGameModePhoto's Esc and Start bind, which opens the pause menu.
    inline constexpr wchar_t const* PhotoMenuFunction = L"ShowMenu";

    /// The name an older version had in the ASI folder, or null: LE3 never had one.
    inline constexpr wchar_t const* LegacyModule = nullptr;

    /// The controller's game modes (BioPlayerController.GameModeManager2, SFXGame_classes.hpp:7610).
    inline USFXGameModeManager* ModeManagerOf(ABioPlayerController* pc)
    {
        return pc != nullptr ? pc->GameModeManager2 : nullptr;
    }

    // No movie pause (MELEPAUSE_MOVIES 0): Entry.cpp installs neither Bink nor MovieInput, and no Kismet movie action is
    // read. The names stay for the shared code that still compiles them.
    inline constexpr MoviePolicy Movies = MoviePolicy::Bink;
    inline constexpr std::array<char const*, 0> BinkAudioMovies{};
    inline constexpr std::array<char const*, 0> MovieLanguages{};
    inline constexpr unsigned long long MovieInputRva = 0;
    inline constexpr std::array<unsigned char, 0> MovieInputStart{};
    inline bool MovieActionOf(UObject*, MovieAction&) { return false; }
}
