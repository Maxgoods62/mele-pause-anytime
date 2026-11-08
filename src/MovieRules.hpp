#pragma once

#include <cstdint>

// The rules of one movie's pause, as Bink.cpp's threads share them under its Lock: the movie's thread decides a step, makes
// its calls (BinkPause, the movie's Wwise event) outside the lock, and settles what Wwise answered back under it, while the
// game thread asks a pause or a resume (Toggle, RequestResume), and the movie may be closed in between. A game run can't
// choose where those land, so tests/MovieRulesTest.cpp runs every order of them up to a length, on every build
// (../build.sh). No game, SDK or Windows type here: plain functions of the fields they use (State, which Bink.cpp's Movie
// holds), changed only by them. Bink.cpp keeps the lock, the finding of the movie, the calls themselves, its atomics and
// every log line. The calls come in as callables, made in the order a rule gives (Send).
namespace PauseAnytime::MovieRules
{
    // ! Times and answers
    // ========================================

    /// AKRESULT AK_Success. From ExecuteActionOnEvent it means the action was queued, for the audio thread's next pass: so
    /// Wwise 2025.1's docs say, which name AK_IDNotFound for an event not loaded. The Wwise 2019 header we have says nothing
    /// of it, and our logs have only shown 1, each way.
    inline constexpr int AkSuccess = 1;
    /// An owed Wwise resume (its pause queued, its resume refused) is sent again by the movie's thread this many times at
    /// most, this long apart, while the picture plays; then once more at the close.
    inline constexpr unsigned SoundResends = 3;
    inline constexpr std::uint64_t SoundResendMs = 100;

    /// A tick written on another thread may be a little ahead of ours.
    constexpr std::uint64_t Since(std::uint64_t now, std::uint64_t then) { return now > then ? now - then : 0; }

    // ! One movie's pause
    // ========================================

    /// A movie's Wwise event as our ExecuteActionOnEvent calls left it (State::Sound, changed by SetSound only). Pausing and
    /// Resuming last while one Apply has its call out, outside Lock: that same Apply sets what came back.
    enum class SoundState : unsigned char
    {
        /// No pause of ours is owed: none was sent, Wwise refused it, or it queued the resume.
        None,
        /// Our pause is out.
        Pausing,
        /// Wwise queued our pause: a resume is owed, and no other pause is sent.
        Paused,
        /// Our resume is out.
        Resuming,
    };

    /// The fields of one movie these rules read and change. Bink.cpp's Movie is one, under Lock.
    struct State
    {
        /// Esc asked for the pause; BinkPause(1) applied it. They differ until the movie's thread comes by.
        bool Want = false;
        bool Paused = false;
        /// When Paused last changed, for Bink.cpp's report times.
        std::uint64_t ChangedAt = 0;
        /// The thread of its latest BinkWait. A resume is applied there only, as a pause is: BinkPause runs outside the lock,
        /// and two threads applying at once could land BinkPause(1) after BinkPause(0). Closed is the exception, on the closing
        /// thread, which no other call on the movie overlaps.
        std::uint32_t WaitThread = 0;
        /// BinkAndEvent: the Wwise event paused with the picture (0: none, Bink audio), taken when a pause is asked while
        /// Sound is None: a pause of ours owed or out keeps its id. Resends: the owed resume's sends by the retry since the
        /// one that went with the picture's resume; SoundSentAt: the latest send of either way.
        std::uint32_t SoundId = 0;
        SoundState Sound = SoundState::None;
        unsigned Resends = 0;
        std::uint64_t SoundSentAt = 0;
    };

    /// None -> Pausing -> Paused or None, Paused -> Resuming -> None or Paused: the steps of SoundState we take.
    constexpr bool LegalStep(SoundState from, SoundState to)
    {
        if (to == SoundState::Pausing) return from == SoundState::None;
        if (to == SoundState::Resuming) return from == SoundState::Paused;
        return from == SoundState::Pausing || from == SoundState::Resuming;
    }

    /// A step SetSound took that is not one of LegalStep's: a bug, which the caller logs.
    struct BadStep
    {
        bool Any = false;
        SoundState From = SoundState::None;
        SoundState To = SoundState::None;
    };

    /// The one change of State::Sound. Pausing and Resuming stamp the send, from which ResendDue counts. A step LegalStep
    /// doesn't take is set all the same, and returned for the log: the caller knows what Wwise answered, and the state must
    /// say what is owed.
    constexpr BadStep SetSound(State& s, SoundState to, std::uint64_t now)
    {
        BadStep bad;
        if (!LegalStep(s.Sound, to)) bad = { true, s.Sound, to };
        s.Sound = to;
        if (to == SoundState::Pausing || to == SoundState::Resuming) s.SoundSentAt = now;
        return bad;
    }

    /// An owed Wwise resume is sent again while the picture plays (no pause asked, none applied): on the thread of the
    /// movie's BinkWait, as any resume, SoundResends times at most, SoundResendMs after the last send. Then only a press (its
    /// pause and resume) or the close sends one.
    constexpr bool ResendDue(State const& s, std::uint32_t thread, std::uint64_t now)
    {
        return s.Sound == SoundState::Paused && !s.Want && !s.Paused && s.WaitThread == thread && s.Resends < SoundResends &&
               Since(now, s.SoundSentAt) >= SoundResendMs;
    }

    /// What the latch knows of the movie's sound now (Bink.cpp's SoundOf): its Wwise event, 0 for Bink audio; Known false
    /// for a sound we don't know.
    struct Latch
    {
        bool Known = false;
        std::uint32_t Event = 0;
    };

    // ! The game thread's asks (ToggleLocked, ResumeLocked)
    // ========================================

    /// ToggleLocked's pause of the movie it picked. The Wwise event to pause with it, as the latch knows it now (the latch may
    /// change before the movie closes), is taken only while no pause of ours is owed or out on it: an owed one keeps its id
    /// (that event is still paused, and Decide sends no second pause), and one out keeps it until Settle has its answer. A
    /// resume out takes the latch when it comes back (Settle). <paramref name="latched"/> is called only when it is taken.
    template <class Latched>
    void AskPause(State& s, Latched latched)
    {
        s.Want = true;
        if (s.Sound == SoundState::None) s.SoundId = latched().Event;
    }

    /// ResumeLocked's change of one movie: a movie we paused, or asked to, resumes at its next call. False for one with
    /// nothing of ours on it (a movie the game paused itself stays as it is). DropStale drops a stale pause through it too.
    constexpr bool AskResume(State& s)
    {
        if (!s.Want && !s.Paused) return false;
        s.Want = false;
        return true;
    }

    // ! The movie's thread (Apply)
    // ========================================

    /// What one Apply sends: the pause (in BinkWait only), the resume, or an owed Wwise resume again (ResendDue).
    enum class Step : unsigned char { None, Pause, Resume, Resend };

    struct Decision
    {
        Step Do = Step::None;
        /// The Wwise event to pause or resume with it; 0: no Wwise call (Bink audio, or no pause of ours to pair it with).
        std::uint32_t Sound = 0;
        /// Resend: which send of the retry this is, 1 to SoundResends.
        unsigned Resend = 0;
        BadStep Bad;
    };

    /// Apply's part under Lock, at a call of the movie's on <paramref name="thread"/> (<paramref name="inWait"/>: a BinkWait,
    /// which notes its thread). A pause is applied in BinkWait only, a resume in any call on the thread of the movie's
    /// BinkWait; <paramref name="canPause"/>: BinkPause is there. Paused takes Want's value here, before the calls: another
    /// Apply of the movie then finds nothing left to apply. The caller recounts when Do is Pause or Resume (Paused is one of
    /// MayPause's inputs).
    constexpr Decision Decide(State& s, bool inWait, std::uint32_t thread, std::uint64_t now, bool canPause)
    {
        Decision d;
        if (inWait) s.WaitThread = thread;
        // Another Apply of this movie has its Wwise call out (two threads call Bink on it at once): that one sets what came
        // back first, so a pause and a resume of the event are never out together.
        if (s.Sound == SoundState::Pausing || s.Sound == SoundState::Resuming) return d;
        if (s.Want == s.Paused)
        {
            if (!ResendDue(s, thread, now)) return d;
            d.Do = Step::Resend;
            d.Resend = ++s.Resends;
            d.Bad = SetSound(s, SoundState::Resuming, now);
        }
        else
        {
            if (s.Want && !inWait) return d;
            // A resume waits for the thread of the movie's BinkWait, which applied the pause: a movie that calls Bink on two
            // threads then never has two BinkPause calls in flight at once.
            if (s.WaitThread != thread) return d;
            if (!canPause) return d;
            d.Do = s.Want ? Step::Pause : Step::Resume;
            s.Paused = s.Want;
            s.ChangedAt = now;
            // A pause is sent only while none of ours is owed or out, a resume only for a pause Wwise queued: a plain Resume
            // takes back one Pause (MovieSound.cpp). A pause asked while an owed resume waits sends none: the event is still
            // paused, and the picture stops with it again.
            if (d.Do == Step::Pause && s.SoundId != 0 && s.Sound == SoundState::None) d.Bad = SetSound(s, SoundState::Pausing, now);
            else if (d.Do == Step::Resume && s.Sound == SoundState::Paused)
            {
                // A resume asked by a press: the retry counts its sends again from here.
                s.Resends = 0;
                d.Bad = SetSound(s, SoundState::Resuming, now);
            }
        }
        // Set just above, and only there: Pausing and Resuming imply a SoundId, which changes only under None.
        if (s.Sound == SoundState::Pausing || s.Sound == SoundState::Resuming) d.Sound = s.SoundId;
        return d;
    }

    /// What the calls of one Decision came back with: Wwise's AKRESULT (AkSuccess when none was made) and BinkPause's answer.
    struct Answer
    {
        int Ak = AkSuccess;
        int Picture = 0;
    };

    /// Apply's calls, outside Lock: <paramref name="picture"/>(1 or 0) is BinkPause, <paramref name="wwise"/>(event, pause)
    /// the event's ExecuteActionOnEvent. A pause sends the event first, and the picture only once Wwise has queued it: a
    /// refusal leaves both playing. A resume starts the picture first. Wwise applies either at its next audio frame, so both
    /// ways the sound follows the picture alike, and the two stay in step.
    template <class Picture, class Wwise>
    Answer Send(Decision const& d, Picture picture, Wwise wwise)
    {
        Answer a;
        switch (d.Do)
        {
        case Step::Pause:
            if (d.Sound != 0) a.Ak = wwise(d.Sound, true);
            if (a.Ak == AkSuccess) a.Picture = picture(1);
            break;
        case Step::Resume:
            a.Picture = picture(0);
            if (d.Sound != 0) a.Ak = wwise(d.Sound, false);
            break;
        case Step::Resend:
            a.Ak = wwise(d.Sound, false);
            break;
        case Step::None: break;
        }
        return a;
    }

    /// The movie is paused once its calls are back: a pause whose sound Wwise refused paused nothing.
    constexpr bool PausedAfter(Decision const& d, Answer const& a) { return d.Do == Step::Pause && a.Ak == AkSuccess; }

    struct Settled
    {
        /// The movie was closed while the calls were out, and a resume of ours is owed: its close left the event to us (Closed
        /// saw Pausing or Resuming and sent nothing), and the caller sends it now.
        bool OwedGone = false;
        /// Want and Paused changed: the caller recounts.
        bool Recount = false;
        /// The pause asked while this resume was out takes this event, from the latch.
        bool Retook = false;
        std::uint32_t RetookEvent = 0;
        BadStep Bad;
    };

    /// Apply's answer brought back under Lock, to <paramref name="s"/>, the movie found again by its handle and serial (null:
    /// closed meanwhile), for a Decision with a Wwise call (none otherwise: nothing to settle). Wwise's answer decides: a
    /// pause it refused pauses nothing, a resume it refused stays owed (ResendDue). <paramref name="latched"/> is called only
    /// for a resume Wwise queued while a pause was asked: that pause took no event (AskPause under Resuming), and takes the
    /// latch's now, as AskPause would have. A latch that no longer knows the movie's sound leaves it the event it had.
    template <class Latched>
    Settled Settle(State* s, Decision const& d, Answer const& a, std::uint64_t now, Latched latched)
    {
        Settled r;
        if (d.Sound == 0) return r;
        if (s == nullptr)
        {
            // Only a game that overlaps BinkClose with another call on the movie gets here.
            r.OwedGone = d.Do == Step::Pause ? a.Ak == AkSuccess : a.Ak != AkSuccess;
            return r;
        }
        if (d.Do == Step::Pause && a.Ak != AkSuccess)
        {
            // The sound did not pause, so the picture was not paused either: the movie plays on, as one whose sound can't be
            // paused must (Bink.cpp's NotPausable). It stays pausable: the next press tries again, one try a press.
            r.Bad = SetSound(*s, SoundState::None, now);
            s->Want = false;
            s->Paused = false;
            s->ChangedAt = now;
            r.Recount = true;
        }
        else if (d.Do == Step::Pause || a.Ak != AkSuccess)
            r.Bad = SetSound(*s, SoundState::Paused, now);
        else
        {
            r.Bad = SetSound(*s, SoundState::None, now);
            if (s->Want)
            {
                auto const l = latched();
                if (l.Known && l.Event != s->SoundId)
                {
                    r.Retook = true;
                    r.RetookEvent = l.Event;
                    s->SoundId = l.Event;
                }
            }
        }
        return r;
    }

    // ! The close (Closed)
    // ========================================

    struct Closing
    {
        /// The owed Wwise resume the close sends, once (0: none owed): nothing else would.
        std::uint32_t Resume = 0;
        /// A pause or resume of ours is out: a Bink call on the movie overlaps its close. That call's Settle finds the movie
        /// gone and sends a resume owed itself.
        bool Out = false;
        /// BinkPause(1) holds the picture: BinkPause(0) right before the close, since this Bink hangs a player that pauses
        /// and then stops calling BinkWait, and a close may be such a stop.
        bool Picture = false;
    };

    /// Closed's part under Lock, before the movie is dropped.
    constexpr Closing Close(State const& s)
    {
        Closing c;
        if (s.Sound == SoundState::Paused) c.Resume = s.SoundId;
        c.Out = s.Sound == SoundState::Pausing || s.Sound == SoundState::Resuming;
        c.Picture = s.Paused;
        return c;
    }
}
