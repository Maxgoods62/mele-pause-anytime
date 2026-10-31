#pragma once

// LE3's facts. Include game/Game.hpp, not this file.

/// Photo mode under the pause: on.
#define MELEPAUSE_PHOTO 1
/// Its GUI sounds: the game's, not hooked (LE3's GFx movies play them through SFXGUIInteraction).
#define MELEPAUSE_PHOTO_GUI_SOUNDS 0
/// Photo mode's panel is a GFx movie of SFXGUIInteraction (PhotoGuiGFx.cpp).
#define MELEPAUSE_PHOTO_GFX 1
/// The Bink movie pause: on, for a Kismet movie whose sound we know (Game::Movies). Of the 334 .bik in BioGame\Movies and
/// the DLC, only StartupLoop_a carries a Bink audio track: every other movie's sound is a Wwise event.
#define MELEPAUSE_MOVIES 1
/// The pause screen's texts are ours (Overlay.cpp), in the language the launcher passes.
#define MELEPAUSE_GAME_TEXTS 0
#define MELEPAUSE_GAME_ICONS 0

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

    // Photo.cpp's spellings of what the SDKs name differently.
    inline FVector const& LocationOf(AActor const* actor) { return actor->location; }
    inline FVector const& LocationOf(FTPOV const& pov) { return pov.location; }
    /// PlayerPhotoMode's tether in a vehicle, beside PhotoModeTetherDistance on foot: none in LE3.
    inline float* VehicleTetherOf(ABioPlayerController*) { return nullptr; }
    inline USFXGameModeManager_eventDisableMode_Parms DisableModeParms(unsigned char mode)
    {
        USFXGameModeManager_eventDisableMode_Parms p;
        p.mode = mode;
        return p;
    }

    // Photo mode's natives (Photo.cpp) in this build of MassEffect3.exe, with their first bytes, found through the classes'
    // vtables in a memory dump of the running game. BioPlayerInput::InputKey is slot 88 of BioPlayerInput's vtable (LE2: 89):
    // it hands the key to SFXGUIInteraction first (0xbc9be0), then to UPlayerInput::InputKey (0x54b710), which ends in
    // UInput::InputKey (0x54b2f0, Pauser at +0x670). UPlayerInput::InputAxis is slot 89, not overridden (it checks the
    // Pauser at 0x54ad9e). The GUI tick is the GFx movies' Tick (slot 18 of vtable 0x12c7e48, called by the viewport
    // client's tick at 0xc25695): it checks no Pauser, but advances each movie by the time since its last advance on the
    // movie's clock, and most movies run on the world's TimeSeconds (0x8c2570), which stands still under the pause.
    inline constexpr unsigned long long PhotoInputKeyRva = 0xa5a040;
    inline constexpr std::array<unsigned char, 24> PhotoInputKeyStart{ 0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x41, 0x56, 0x48, 0x83, 0xEC,
                                                                       0x40, 0x48, 0x8B, 0xF1, 0x41, 0x8B, 0xE9, 0x48, 0x8D, 0x4C, 0x24, 0x60 };
    inline constexpr unsigned long long PhotoInputAxisRva = 0x54ac90;
    inline constexpr std::array<unsigned char, 16> PhotoInputAxisStart{ 0x48, 0x8B, 0xC4, 0x56, 0x57, 0x41, 0x56, 0x48,
                                                                        0x83, 0xEC, 0x70, 0x48, 0xC7, 0x40, 0xA8, 0xFE };
    inline constexpr unsigned long long PhotoGuiTickRva = 0x92d7a0;
    inline constexpr std::array<unsigned char, 16> PhotoGuiTickStart{ 0x48, 0x89, 0x5C, 0x24, 0x18, 0x57, 0x48, 0x83,
                                                                      0xEC, 0x40, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x49 };

    /// What the GFx movies' Tick walks (Photo.cpp GfxClock): two arrays (data pointer, count) at these offsets of its this,
    /// of pointers to the movies' entries, and in each entry the clock's time of the movie's last advance (+0x44; +0x58 is
    /// 1 for a movie on real time, +0x5c the movie).
    struct GfxMovieList
    {
        unsigned Data;
        unsigned Count;
    };
    inline constexpr std::array<GfxMovieList, 2> GfxMovieLists{ { { 0x4c, 0x54 }, { 0xbc, 0xc4 } } };
    inline constexpr unsigned GfxMovieStamp = 0x44;

    /// SFXPlayerCamera.UpdateViewTarget only calls PickCameraMode when TimeSeconds - LastTransitionTime > TransitionTimeout,
    /// as in LE2, and TimeSeconds stands still under the pause: a switch just before it would keep the photo camera from
    /// being picked, or the conversation's from coming back until after the world resumes.
    inline void AllowCameraSwitch(ASFXPlayerCamera* camera, AWorldInfo* world)
    {
        camera->LastTransitionTime = world->TimeSeconds - camera->TransitionTimeout - 1.0f;
    }

    /// A movie's sound is the Wwise event its Kismet BioSeqAct_MovieBink names (the movie player posts it once the movie
    /// starts), paused with ExecuteActionOnEvent beside BinkPause, as in LE2. Kismet plays only some of LE3's movies: most
    /// are Matinee tracks (SFXInterpTrackMovieBink, inside cutscenes and conversations) or loading movies, which nothing
    /// latches, so Esc and Start stay the game's there (docs/games/LE3.md). StartupLoop_a, the one movie with Bink audio, is the
    /// startup movie, not listed: the game's own screens play over it.
    inline constexpr MoviePolicy Movies = MoviePolicy::BinkAndEvent;
    inline constexpr std::array<char const*, 0> BinkAudioMovies{};

    /// The suffixes of LE3's localized movie files: only Genesis2Title has them (DLC_CON_DH1: _INT, _DEU, _ESN, _FRA, _ITA,
    /// _JPN, _POL, _RUS; DLC_METR_Patch01: _DEE, _FRE, _ITE).
    inline constexpr std::array<char const*, 11> MovieLanguages{ "int", "deu", "esn", "fra", "ita", "jpn",
                                                                 "pol", "rus", "dee", "fre", "ite" };

    /// The engine's movie input: the Bink movie player's InputKey (MassEffect3.exe, its FViewportClient part's vtable slot
    /// 5), found in a memory dump of the running game, with LE2's first bytes. On a press of Escape, SpaceBar,
    /// XboxTypeS_Back, XboxTypeS_X or XboxTypeS_Start (X and Start unless the movie's flags say not) it asks the player to
    /// stop the movie and says it handled the key; Enter, A and the left mouse button are not its keys in LE3. The movie
    /// stops only if FullScreenMovie.SkippableMovies lists it (four movies). A movie played with flag 0x400 (a Matinee
    /// track's, and Kismet's while a cutscene runs) ignores every key: they go on to SFXGameModeMovie.SkipMovie, a script
    /// that skips the cutscene and nothing else (LE3 has no native SkipMovie). MovieInput.cpp hooks it.
    inline constexpr unsigned long long MovieInputRva = 0x24afc0;
    inline constexpr std::array<unsigned char, 28> MovieInputStart{ 0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x48, 0x20, 0x57, 0x41, 0x54,
                                                                    0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x60,
                                                                    0x48, 0xC7, 0x40, 0x98, 0xFE, 0xFF, 0xFF, 0xFF };
    /// Space and X are its keys already: they reach it as they are.
    inline constexpr wchar_t const* MovieSkipKeyAs = nullptr;

    /// The movie player's fields that the movie lines show (Bink.cpp), found in the dump: GFullScreenMovie, its flags (+0x58:
    /// 0x400, keys ignored, for the relay jump and the Matinee tracks; 0x80, the movie input off, for the movies opened by
    /// file handle) and its skippable flag (+0x84, from SkippableMovies). For the logs only.
    inline constexpr MoviePlayerFields MoviePlayer{ 0x1cb9cd0, 0x58, 0x84 };

    /// The op that starts a conversation, for KismetMovie.hpp's MaySound and SameMoment: LE3 has no
    /// BioSeqAct_StartConversation; SFXSeqAct_StartConversation is an SFXSeqAct_StartAmbientConv, and ambient lines speak
    /// too.
    inline UClass* ConversationActionClass() { return USFXSeqAct_StartAmbientConv::StaticClass(); }

    // Of LE3's 188 live movie actions (71 movies), 122 post a Wwise event and 114 of those pass KismetMovie.hpp's
    // MovieActionOf (docs/games/LE3.md): the relay jump, the first Citadel arrival, Omega's intros, the Leviathan arrivals, the
    // ending's memorial flashbacks. A Matinee, music, a conversation, a sound loop or a nested sequence runs beside the 8 others: the Mars
    // arrival, the Citadel DLC's two Normandy movies, Device_Reveal, the Kro001 animatic, Hackett's speech intro,
    // Lev002_Asteroid_Depart and Lev004_WW_ArrivalCrash4.

    /// Our own pause screen, as in LE2: only LE1's wears its game's colours so far.
    inline constexpr OverlayLook Look = OverlayLook::Own;

    /// No GUI sound as our world pause starts or ends: only LE1's plays one so far.
    inline constexpr wchar_t const* PauseSound = nullptr;
    inline constexpr wchar_t const* ResumeSound = nullptr;
}

#include "MelePause/game/KismetMovie.hpp"
