#pragma once

// LE2's facts. Include game/Game.hpp, not this file.

/// Photo mode under the pause: three MassEffect2.exe natives get past it.
#define MELEPAUSE_PHOTO 1
/// Photo mode's GUI sounds under the pause: no hook. They are Wwise events, and AudioComponent has no bNonRealtime.
#define MELEPAUSE_PHOTO_GUI_SOUNDS 0
/// Photo mode's panel is a ScaleForm panel of the GUI manager (PhotoGuiSF.cpp), not a GFx movie (PhotoGuiGFx.cpp).
#define MELEPAUSE_PHOTO_GFX 0
/// The Bink movie pause: on, for a movie whose sound we know (Game::Movies). 164 of the 166 movies in BioGame\Movies
/// have no Bink audio track (nor do the DLC's): their sound is a Wwise event that BinkPause does not stop.
#define MELEPAUSE_MOVIES 1
/// The pause screen's texts are ours (Overlay.cpp), in the language the launcher passes.
#define MELEPAUSE_GAME_TEXTS 0

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

    // Photo mode's natives (Photo.cpp) in this build of MassEffect2.exe, with their first bytes, found from LE1's through
    // the classes' vtables in a memory dump of the running game. BioPlayerInput::InputKey is slot 89 of BioPlayerInput's
    // vtable (LE1: 90) and ends in UPlayerInput::InputKey (0x542220), then UInput::InputKey (0x541e00, Pauser at +0x684).
    // UPlayerInput::InputAxis is slot 90, not overridden (it checks the Pauser at 0x54189f). UBioSFManager::Tick: the GUI
    // manager's own Tick (0xb27820) does its part only while the world runs, then jumps there.
    inline constexpr unsigned long long PhotoInputKeyRva = 0xa62e00;
    inline constexpr std::array<unsigned char, 24> PhotoInputKeyStart{ 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89,
                                                                       0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0x01 };
    inline constexpr unsigned long long PhotoInputAxisRva = 0x541790;
    inline constexpr std::array<unsigned char, 16> PhotoInputAxisStart{ 0x48, 0x8B, 0xC4, 0x56, 0x57, 0x41, 0x56, 0x48,
                                                                        0x83, 0xEC, 0x70, 0x48, 0xC7, 0x40, 0xA8, 0xFE };
    inline constexpr unsigned long long PhotoGuiTickRva = 0x1a07c0;
    inline constexpr std::array<unsigned char, 16> PhotoGuiTickStart{ 0x40, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x8B, 0x81,
                                                                      0xB0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF9, 0x83 };

    /// The conversation's panel (the wheel; MassEffectGuiManager has no m_oConversationPanel in LE2), or null.
    inline UBioSFPanel* ConversationPanelOf(UMassEffectGuiManager* gui)
    {
        auto const handler = gui->m_oConversationHandler;
        return handler != nullptr ? handler->oPanel : nullptr;
    }

    /// SFXPlayerCamera.UpdateViewTarget only calls PickCameraMode when TimeSeconds - LastTransitionTime > TransitionTimeout
    /// (0.25 s), and TimeSeconds stands still under the pause: a switch just before it would keep the photo camera from
    /// being picked, or the conversation's from coming back until 0.25 s after the world resumes.
    inline void AllowCameraSwitch(ASFXPlayerCamera* camera, AWorldInfo* world)
    {
        camera->LastTransitionTime = world->TimeSeconds - camera->TransitionTimeout - 1.0f;
    }

    /// A movie's sound is the Wwise event its Kismet BioSeqAct_MovieBink posts beside the picture, paused with
    /// ExecuteActionOnEvent beside BinkPause. Of the two .bik that carry Bink audio, load_f01_a and visionavi, only
    /// visionavi is listed.
    inline constexpr MoviePolicy Movies = MoviePolicy::BinkAndEvent;
    inline constexpr std::array<char const*, 1> BinkAudioMovies{ "visionavi" };

    /// The suffixes of LE2's localized movie files: Opening_Seq ships only as Opening_Seq_INT.bik, _DEU, _FRA, ... (and
    /// DLC_METR_Patch01 adds _FRE, _ITE and _POE copies).
    inline constexpr std::array<char const*, 12> MovieLanguages{ "int", "fra", "fre", "deu", "dee", "esn",
                                                                 "ita", "ite", "pol", "poe", "rus", "jpn" };

    /// The engine's movie input: the Bink movie player's InputKey (MassEffect2.exe, its FViewportClient part's vtable slot
    /// 5), and its first bytes. A press reaches it before Console.InputKey. On a press of Escape, Enter, LeftMouseButton,
    /// XboxTypeS_Back, XboxTypeS_A or XboxTypeS_Start (the last two unless the movie's flags say not) it asks the player to
    /// stop the movie and says it handled the key; on any other it does nothing. The movie stops only if the game lets it
    /// skip (FullScreenMovie.SkippableMovies). Space and the pad's X skip nothing in LE2: their SkipMovie ends in an empty
    /// native. MovieInput.cpp hooks it.
    inline constexpr unsigned long long MovieInputRva = 0x2334d0;
    inline constexpr std::array<unsigned char, 28> MovieInputStart{ 0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x48, 0x20, 0x57, 0x41, 0x54,
                                                                    0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x60,
                                                                    0x48, 0xC7, 0x40, 0x98, 0xFE, 0xFF, 0xFF, 0xFF };
    /// Space and X reach it as Enter, one of its keys: they skip what Esc skipped.
    inline constexpr wchar_t const* MovieSkipKeyAs = L"Enter";
    /// The movie player's fields in the logs: none read in LE2.
    inline constexpr MoviePlayerFields MoviePlayer{};

    /// The op that starts a conversation, for KismetMovie.hpp's MaySound and SameMoment.
    inline UClass* ConversationActionClass() { return UBioSeqAct_StartConversation::StaticClass(); }

    // Of LE2's 117 movie actions with a Wwise event, 71 pass KismetMovie.hpp's MovieActionOf (docs/games/LE2.md): the relay, the
    // hubs' arrivals and departures. Voice lines, conversations, music cues, Matinees and Play_ events run beside the
    // others: in the Prologue (BioD_ProNor ProNorEscape, Opening_End_Seq, ProNor_Pod2), the suicide mission (EndGm1), Tali's
    // and Grunt's arrivals; and visionavi's Matinee posts Play_cineanim_N7Spdr1_flashback_exit with it.

    /// Our own pause screen, as in LE3: only LE1's wears its game's colours so far.
    inline constexpr OverlayLook Look = OverlayLook::Own;

    /// No GUI sound as our world pause starts or ends: only LE1's plays one so far.
    inline constexpr wchar_t const* PauseSound = nullptr;
    inline constexpr wchar_t const* ResumeSound = nullptr;
}

#include "MelePause/game/KismetMovie.hpp"
