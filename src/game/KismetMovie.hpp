#pragma once

// What LE2 and LE3 read of a Kismet BioSeqAct_MovieBink (MoviePolicy::BinkAndEvent): its movie, the Wwise event it posts
// beside the picture, and whether anything else may make a sound while the movie plays. game/LE2.hpp and game/LE3.hpp
// include it last, after ConversationActionClass (the op that starts a conversation, named differently in each game). Not
// LE1: its movie action has no Wwise event, and its movies carry their sound in the .bik.

#include <algorithm>
#include <iterator>
#include <string_view>
#include <vector>

namespace PauseAnytime::Game
{
    /// An op that may make a sound when it runs: a voice line, a conversation, a vocalization, a remote event (the levels'
    /// music cues are remote events), a sound, music or FaceFX cue, a Wwise trigger, state, switch or audio object, music
    /// turned up, a Matinee, a nested sequence, and a Wwise event that does not silence something (Mute_, Stop_,
    /// VO_Kill_: Mute_All_World_Sounds and its _2secs fade, Mute_amb_..., Stop_amb_...) or bring the world's ambience
    /// back (UnMute: LE2's Illium arrival posts UnMute_All_World_Sounds on Out and Mute_All_World_Sounds on HasStarted,
    /// and the hubs' other movies leave the world unmuted). Every other op is taken as silent, and the ops it starts are
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
        UClass* const sounding[]{ UBioSeqAct_FaceOnlyVO::StaticClass(), ConversationActionClass(),
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
            || op->IsA(ConversationActionClass()) || op->IsA(UBioSeqAct_ShowChoiceGUI::StaticClass()))
            return false;
        if (op->IsA(USeqAct_Delay::StaticClass())) return static_cast<USeqAct_Delay*>(op)->Duration <= SameMomentSeconds;
        if (op->IsA(USeqAct_Interp::StaticClass()))
        {
            auto const data = static_cast<USeqAct_Interp*>(op)->InterpData;
            return data != nullptr && data->InterpLength <= SameMomentSeconds;
        }
        return true;
    }

    /// A Matinee's outputs that fire once it has ended (Default__SeqAct_Interp in LE2's and LE3's Engine.pcc); its other
    /// outputs are event tracks, which fire while it plays.
    inline bool MatineeEnded(std::wstring_view output)
    {
        return output == L"Completed" || output == L"Reversed" || output == L"Cancelled" || output == L"Stopped";
    }

    inline std::wstring_view LinkName(FSeqOpOutputLink const& output)
    {
        return { output.LinkDesc.Chars(), static_cast<size_t>(output.LinkDesc.Length()) };
    }

    /// The movie's outputs that don't fire as it starts: Complete and Failed at its end, and LE3's Primed once its file is
    /// preloaded (the Prime input), before the movie is played.
    inline bool AtMovieStart(std::wstring_view output)
    {
        return output != L"Complete" && output != L"Failed" && output != L"Primed";
    }

    /// The link starts the op it leads to: not LE3's Prime and Unprime inputs (of the movie action and the Matinee), which
    /// preload or drop it without playing it.
    inline bool Starts(FSeqOpOutputInputLink const& link)
    {
        auto const op = link.LinkedOp;
        if (op == nullptr || link.InputLinkIdx < 0 || static_cast<unsigned>(link.InputLinkIdx) >= op->InputLinks.Count()) return op != nullptr;
        auto const& input = op->InputLinks.GetData()[link.InputLinkIdx].LinkDesc;
        std::wstring_view const name{ input.Chars(), static_cast<size_t>(input.Length()) };
        return name != L"Prime" && name != L"Unprime";
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

    /// A Kismet BioSeqAct_MovieBink (LE3's SFXSeqAct_PlatformMovieBink is one too): its m_sMovieName and m_wwiseEvent, and
    /// that event's Id. Beside is the first op that may make a sound while the movie plays (MaySound), and Via says how it
    /// was found:
    /// - "Out" or "HasStarted": that output of the movie starts it (both fire as the movie starts; Complete and Failed
    ///   at its end), or a silent op after it does.
    /// - "Beside": an output that starts the movie starts it too, or an output that starts the op that starts the movie,
    ///   when that op fires at about the moment it is started (SameMoment): two levels up, in the movie's sequence.
    /// - "Matinee": a Matinee's event track starts the movie, or the op that starts it, with the Matinee's clock running.
    /// - "Conversation": a conversation's event (a line starting) starts it, and the line plays on.
    /// False for any other object. The counts per game are in game/LE<n>.hpp.
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
            if (output.bDisabled || !AtMovieStart(name)) continue;
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
                        if (Holds(started, link.LinkedOp))
                        {
                            if (Starts(link)) starts = true;
                        }
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
