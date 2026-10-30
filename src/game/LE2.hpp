#pragma once

// LE2's facts. Include game/Game.hpp, not this file.

#include <algorithm>
#include <iterator>
#include <string_view>
#include <vector>

/// Photo mode under the pause: three MassEffect2.exe natives get past it.
#define MELEPAUSE_PHOTO 1
/// Photo mode's GUI sounds under the pause: no hook. They are Wwise events, and AudioComponent has no bNonRealtime.
#define MELEPAUSE_PHOTO_GUI_SOUNDS 0
/// Photo mode's panel is a ScaleForm panel of the GUI manager (PhotoGuiSF.cpp), not a GFx movie (PhotoGuiGFx.cpp).
#define MELEPAUSE_PHOTO_GFX 0
/// The Bink movie pause: on, for a movie whose sound we know (Game::Movies). 164 of the 166 movies in BioGame\Movies
/// have no Bink audio track (nor do the DLC's): their sound is a Wwise event that BinkPause does not stop.
#define MELEPAUSE_MOVIES 1

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

    /// An op that may make a sound when it runs: a voice line, a conversation, a vocalization, a remote event (the levels'
    /// music cues are remote events), a sound, music or FaceFX cue, a Wwise trigger, state, switch or audio object, music
    /// turned up, a Matinee, a nested sequence, and a Wwise event that does not silence something (Mute_, Stop_,
    /// VO_Kill_: Mute_All_World_Sounds and its _2secs fade, Mute_amb_..., Stop_amb_...) or bring the world's ambience
    /// back (UnMute: Illium's arrival posts UnMute_All_World_Sounds on Out and Mute_All_World_Sounds on HasStarted, and
    /// the hubs' other movies leave the world unmuted). Every other op is taken as silent, and the ops it starts are
    /// walked in turn (FirstSound).
    inline bool MaySound(USequenceOp* op)
    {
        if (op->IsA(USeqAct_WwisePostEvent::StaticClass()))
        {
            auto const event = static_cast<USeqAct_WwisePostEvent*>(op)->WwiseObject;
            if (event == nullptr) return false;
            std::string_view const name{ event->Name.GetName() };
            return !name.starts_with("Mute_") && !name.starts_with("UnMute") && !name.starts_with("Stop_") && !name.starts_with("VO_Kill_");
        }
        UClass* const sounding[]{ UBioSeqAct_FaceOnlyVO::StaticClass(), UBioSeqAct_StartConversation::StaticClass(),
                                  USFXSeqAct_GenerateVocEvent::StaticClass(), USeqAct_ActivateRemoteEvent::StaticClass(),
                                  USeqAct_PlaySound::StaticClass(), USeqAct_PlayMusicTrack::StaticClass(),
                                  USeqAct_PlayFaceFXAnim::StaticClass(), USeqAct_ApplySoundNode::StaticClass(),
                                  USeqAct_WwisePostTrigger::StaticClass(), USeqAct_WwiseSetState::StaticClass(),
                                  USeqAct_WwiseSetSwitch::StaticClass(), USeqAct_WwiseStartAudioObjects::StaticClass(),
                                  USeqAct_WwiseMusicVolumeEnable::StaticClass(), USeqAct_Interp::StaticClass(),
                                  USequence::StaticClass() };
        return std::any_of(std::begin(sounding), std::end(sounding), [op](UClass* cls) { return op->IsA(cls); });
    }

    /// A Delay or a Matinee this short starts what follows it at about the moment it was started.
    inline constexpr float SameMomentSeconds = 5.0f;

    /// The op fires its outputs at about the moment it is started: an instant op, or a Delay or Matinee of
    /// SameMomentSeconds or less. Not an event, a movie, a nested sequence, a conversation or a choice: they wait on
    /// something else.
    inline bool SameMoment(USequenceOp* op)
    {
        if (op->IsA(USequenceEvent::StaticClass()) || op->IsA(UBioSeqAct_MovieBink::StaticClass()) || op->IsA(USequence::StaticClass())
            || op->IsA(UBioSeqAct_StartConversation::StaticClass()) || op->IsA(UBioSeqAct_ShowChoiceGUI::StaticClass()))
            return false;
        if (op->IsA(USeqAct_Delay::StaticClass())) return static_cast<USeqAct_Delay*>(op)->Duration <= SameMomentSeconds;
        if (op->IsA(USeqAct_Interp::StaticClass()))
        {
            auto const data = static_cast<USeqAct_Interp*>(op)->InterpData;
            return data != nullptr && data->InterpLength <= SameMomentSeconds;
        }
        return true;
    }

    /// A Matinee's outputs that fire once it has ended (Default__SeqAct_Interp in LE2's Engine.pcc); its other outputs
    /// are event tracks, which fire while it plays.
    inline bool MatineeEnded(std::wstring_view output)
    {
        return output == L"Completed" || output == L"Reversed" || output == L"Cancelled" || output == L"Stopped";
    }

    inline std::wstring_view LinkName(FSeqOpOutputLink const& output)
    {
        return { output.LinkDesc.Chars(), static_cast<size_t>(output.LinkDesc.Length()) };
    }

    inline bool Holds(std::vector<USequenceOp*> const& ops, USequenceOp* op)
    {
        return std::find(ops.begin(), ops.end(), op) != ops.end();
    }

    /// Walks <paramref name="next"/> and, while each op is silent (not MaySound), the ops its outputs that are not
    /// disabled start: the first op that may make a sound, or the 33rd op walked; null when all are silent.
    /// <paramref name="walked"/> carries the silent ops over from one call to the next. The movie action is not walked.
    inline USequenceOp* FirstSound(std::vector<USequenceOp*> next, USequenceOp* action, std::vector<USequenceOp*>& walked)
    {
        constexpr size_t MaxWalk = 32;
        while (!next.empty())
        {
            auto const op = next.back();
            next.pop_back();
            if (op == action || Holds(walked, op)) continue;
            if (walked.size() == MaxWalk || MaySound(op)) return op;
            walked.push_back(op);
            for (auto const& output : op->OutputLinks)
            {
                if (output.bDisabled) continue;
                for (auto const& link : output.Links)
                {
                    if (link.LinkedOp != nullptr) next.push_back(link.LinkedOp);
                }
            }
        }
        return nullptr;
    }

    /// A Kismet BioSeqAct_MovieBink: its m_sMovieName and m_wwiseEvent (SFXGame_classes.hpp:19319), and that event's Id
    /// (UWwiseEvent, WwiseAudio_classes.hpp:518). Beside is the first op that may make a sound while the movie plays
    /// (MaySound), and Via says how it was found:
    /// - "Out" or "HasStarted": that output of the movie starts it (both fire as the movie starts; Complete and Failed
    ///   at its end), or a silent op after it does.
    /// - "Beside": an output that starts the movie starts it too, or an output that starts the op that starts the movie,
    ///   when that op fires at about the moment it is started (SameMoment): two levels up, in the movie's sequence.
    /// - "Matinee": a Matinee's event track starts the movie, or the op that starts it, with the Matinee's clock running.
    /// - "Conversation": a conversation's event (a line starting) starts it, and the line plays on.
    /// Of LE2's 117 movie actions with a Wwise event, 71 have none (ai-docs/15): the relay, the hubs' arrivals and
    /// departures. Voice lines, conversations, music cues, Matinees and Play_ events run beside the others: in the
    /// Prologue (BioD_ProNor ProNorEscape, Opening_End_Seq, ProNor_Pod2), the suicide mission (EndGm1), Tali's and
    /// Grunt's arrivals; and visionavi's Matinee posts Play_cineanim_N7Spdr1_flashback_exit with it. False for any
    /// other object.
    inline bool MovieActionOf(UObject* object, MovieAction& out)
    {
        if (object == nullptr || !object->IsA(UBioSeqAct_MovieBink::StaticClass())) return false;
        auto const action = static_cast<UBioSeqAct_MovieBink*>(object);
        auto const event = action->m_wwiseEvent;
        out.Movie = &action->m_sMovieName;
        out.Event = event;
        out.EventId = event != nullptr ? static_cast<unsigned>(event->Id) : 0;

        std::vector<USequenceOp*> walked;
        for (auto const& output : action->OutputLinks)
        {
            auto const name = LinkName(output);
            if (output.bDisabled || name == L"Complete" || name == L"Failed") continue;
            std::vector<USequenceOp*> next;
            for (auto const& link : output.Links)
            {
                if (link.LinkedOp != nullptr) next.push_back(link.LinkedOp);
            }
            if (auto const op = FirstSound(std::move(next), action, walked); op != nullptr)
            {
                out.Beside = op;
                out.Via = name;
                return true;
            }
        }

        auto const parent = action->ParentSequence;
        if (parent == nullptr) return true;
        constexpr int Levels = 2;
        std::vector<USequenceOp*> started{ action };
        std::vector<USequenceOp*> climbed{ action };
        for (auto level = 0; level < Levels && !started.empty(); ++level)
        {
            std::vector<USequenceOp*> starters;
            for (auto const member : parent->SequenceObjects)
            {
                if (member == nullptr || !member->IsA(USequenceOp::StaticClass())) continue;
                auto const source = static_cast<USequenceOp*>(member);
                for (auto const& output : source->OutputLinks)
                {
                    if (output.bDisabled) continue;
                    auto starts = false;
                    std::vector<USequenceOp*> next;
                    for (auto const& link : output.Links)
                    {
                        if (link.LinkedOp == nullptr) continue;
                        if (Holds(started, link.LinkedOp)) starts = true;
                        else next.push_back(link.LinkedOp);
                    }
                    if (!starts) continue;
                    if (source->IsA(USeqAct_Interp::StaticClass()) && !MatineeEnded(LinkName(output)))
                    {
                        out.Beside = source;
                        out.Via = L"Matinee";
                        return true;
                    }
                    if (source->IsA(UBioSeqEvt_Conversation::StaticClass()) || source->IsA(UBioSeqEvt_ConvNode::StaticClass()))
                    {
                        out.Beside = source;
                        out.Via = L"Conversation";
                        return true;
                    }
                    if (auto const op = FirstSound(std::move(next), action, walked); op != nullptr)
                    {
                        out.Beside = op;
                        out.Via = L"Beside";
                        return true;
                    }
                    if (!Holds(climbed, source) && SameMoment(source))
                    {
                        climbed.push_back(source);
                        starters.push_back(source);
                    }
                }
            }
            started = std::move(starters);
        }
        return true;
    }
}
