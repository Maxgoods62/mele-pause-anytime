#pragma once

// LE1's facts. Include game/Game.hpp, not this file.

/// Photo mode under the pause: five MassEffect1.exe natives get past it.
#define MELEPAUSE_PHOTO 1
/// Photo mode's GUI sounds under the pause need our hooks: ISACT starts only bNonRealtime sounds while the world is paused.
#define MELEPAUSE_PHOTO_GUI_SOUNDS 1
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

    // Photo mode's natives (Photo.cpp) in this build of MassEffect1.exe, with their first bytes: BioPlayerInput::InputKey,
    // UPlayerInput::InputAxis (BioPlayerInput::InputAxis ends in a jump to it), UBioSFManager::Tick, and for the GUI
    // sounds UBioSFManager::PlayGuiSound and UAudioDevice::CreateComponent. Read from a memory dump of the running game.
    inline constexpr unsigned long long PhotoInputKeyRva = 0xc6de70;
    inline constexpr std::array<unsigned char, 13> PhotoInputKeyStart{ 0x4C, 0x89, 0x44, 0x24, 0x18, 0x53, 0x55, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57 };
    inline constexpr unsigned long long PhotoInputAxisRva = 0x68bc40;
    inline constexpr std::array<unsigned char, 11> PhotoInputAxisStart{ 0x48, 0x8B, 0xC4, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x70 };
    inline constexpr unsigned long long PhotoGuiTickRva = 0x193bc0;
    inline constexpr std::array<unsigned char, 9> PhotoGuiTickStart{ 0x40, 0x53, 0x57, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x50 };
    inline constexpr unsigned long long PhotoPlayGuiSoundRva = 0x191ee0;
    inline constexpr std::array<unsigned char, 15> PhotoPlayGuiSoundStart{ 0x40, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0xC7,
                                                                           0x44, 0x24, 0x30, 0xFE, 0xFF, 0xFF, 0xFF };
    inline constexpr unsigned long long PhotoCreateAudioComponentRva = 0x2ff470;
    inline constexpr std::array<unsigned char, 16> PhotoCreateAudioComponentStart{ 0x48, 0x8B, 0xC4, 0x57, 0x41, 0x54, 0x41, 0x55,
                                                                                   0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xA0 };

    /// The conversation's panel (the wheel and the subtitle), or null.
    inline UBioSFPanel* ConversationPanelOf(UMassEffectGuiManager* gui) { return gui->m_oConversationPanel; }

    /// PickCameraMode only switches modes when GameTimeSeconds - LastTransitionTime > 0.1 s, and GameTimeSeconds stands
    /// still under the pause: a switch just before it would keep the photo camera from ever being picked.
    inline void AllowCameraSwitch(ASFXPlayerCamera* camera, AWorldInfo* world) { camera->LastTransitionTime = world->GameTimeSeconds - 1.0f; }

    /// Every movie carries its sound in its .bik, which BinkPause stops: no Wwise event to pause, no list of Bink-audio
    /// movies to keep.
    inline constexpr MoviePolicy Movies = MoviePolicy::Bink;
    inline constexpr std::array<char const*, 0> BinkAudioMovies{};
    inline constexpr std::array<char const*, 0> MovieLanguages{};

    /// The engine's movie input is not hooked (MovieInput::Install does nothing for 0): the travel movie's pause works
    /// without.
    inline constexpr unsigned long long MovieInputRva = 0;
    inline constexpr std::array<unsigned char, 0> MovieInputStart{};

    /// Under MoviePolicy::Bink no Kismet movie action is read: none (LE1's BioSeqAct_MovieBink has no Wwise event).
    inline bool MovieActionOf(UObject*, MovieAction&) { return false; }
}
