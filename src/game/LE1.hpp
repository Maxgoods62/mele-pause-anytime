#pragma once

// LE1's facts. Include game/Game.hpp, not this file.

/// Photo mode's GUI sounds under the pause need our hooks: ISACT starts only bNonRealtime sounds while the world is paused.
#define MELEPAUSE_PHOTO_GUI_SOUNDS 1
/// Photo mode's panel is a ScaleForm panel of the GUI manager (PhotoGuiSF.cpp), not a GFx movie (PhotoGuiGFx.cpp).
#define MELEPAUSE_PHOTO_GFX 0

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

    // Photo.cpp's spellings of what the SDKs name differently.
    inline FVector const& LocationOf(AActor const* actor) { return actor->Location; }
    inline FVector const& LocationOf(FTPOV const& pov) { return pov.Location; }
    /// PlayerPhotoMode's tether in a vehicle, beside PhotoModeTetherDistance on foot: VehiclePhotoModeTetherDistance.
    inline float* VehicleTetherOf(ABioPlayerController* pc) { return &pc->VehiclePhotoModeTetherDistance; }
    inline USFXGameModeManager_eventDisableMode_Parms DisableModeParms(unsigned char mode)
    {
        USFXGameModeManager_eventDisableMode_Parms p;
        p.Mode = mode;
        return p;
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
    inline constexpr wchar_t const* MovieSkipKeyAs = nullptr;
    inline constexpr MoviePlayerFields MoviePlayer{};

    /// Under MoviePolicy::Bink no Kismet movie action is read: none (LE1's BioSeqAct_MovieBink has no Wwise event).
    inline bool MovieActionOf(UObject*, MovieAction&) { return false; }

    /// The pause screen in the colours of LE1's menus.
    inline constexpr OverlayLook Look = OverlayLook::LE1;

    /// The class that has GetSimpleString, a static native that any such game object runs, and its parameters.
    using StringGame = ABioSPGame;
    using GetSimpleStringParms = ABioSPGame_execGetSimpleString_Parms;

    /// The pause screen's words in the game's string table, in every language it ships: the title (170645, "PAUSED"; the
    /// game's own unused paused box shows it), resume (157151, "Resume") and photo mode (184397, "Photo Mode").
    inline constexpr PauseTextIds PauseTexts{ 170645, 157151, 184397 };

    /// The gamepad's A and Y, which the game's wheel and message boxes show: in Startup_<lang>.pcc, the same in every
    /// language, loaded with the game and kept for good (RootSet).
    inline constexpr PauseIconNames PauseIcons{ L"GUI_SF_SharedAssets", L"XBox_ControllerIcons_I4", L"XBox_ControllerIcons_I9" };

    /// MassEffect1.exe's way from a texture to its view.
    inline constexpr TextureViewFields TextureView{ 0x10247A8, 0x24, 0x1211D20, 0xC, 0x10 };

    /// The GFx movie that holds both fonts: in Startup_<lang>.pcc, the same in every language but for its order in Japanese,
    /// loaded with the game and kept for good (RootSet), its bytes in memory as in the package. It has no export names:
    /// "$AeroLight" and "$BioMass" are aliases in the game's exe, and ids and order differ between languages, so only the
    /// fonts' names can be trusted.
    inline constexpr PauseFontNames PauseFonts{ L"GUI_SF_Fonts", L"Fonts", "Tigensu_jpn", L"Fonts", "Biomass_jpn" };

    /// The GUI manager's sounds (GuiSounds 2DA rows, both snd_gui.c_snd_guiswish) our world pause plays as it starts and as
    /// a key ends it: the game's menus play them as they open and close. Null: none.
    inline constexpr wchar_t const* PauseSound = L"InGameGuiEnter";
    inline constexpr wchar_t const* ResumeSound = L"InGameGuiExit";
    /// Only sounds here: nothing to give back when the pause ends without a key.
    inline constexpr bool PauseSoundHoldsAudio = false;
}
