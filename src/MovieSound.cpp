#include "PauseAnytime/Api.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

// The sound of an LE2 movie is mostly not in its .bik: a Kismet BioSeqAct_MovieBink posts a Wwise event beside the
// picture, and the two clocks then run free. BinkPause stops only the picture, so that event is paused beside it with
// AK::SoundEngine::ExecuteActionOnEvent, which MassEffect2.exe exports by name (its code is encrypted on disk, its export
// table is not): no address to scan, no game object, no ProcessEvent. The game thread latches the movie action's event at
// its Activated (Pause.cpp); a movie's thread reads the latch and pauses that event (Bink.cpp). Wwise calls are thread-safe
// and queued: they apply at the audio thread's next pass. The game object is AK_INVALID_GAME_OBJECT, which the Wwise
// headers say "may also mean all game objects": the one the native code posted the event on is unknown. Only under
// MoviePolicy::BinkAndEvent: LE1's movies carry Bink audio, and nothing here runs there.
namespace PauseAnytime::MovieSound
{
    namespace
    {
        // AkSoundEngine.h and AkTypes.h (Wwise 2019): the exe's mangled names give the exact types. AKRESULT,
        // AkActionOnEventType and AkCurveInterpolation are ints.
        using t_ExecuteActionOnEvent = int(std::uint32_t eventId, int actionType, std::uint64_t gameObject, std::int32_t fadeMs, int curve,
                                           std::uint32_t playingId);
        using t_GetIDFromString = std::uint32_t(char const* name);

        constexpr char const* ExecuteActionOnEventName =
            "?ExecuteActionOnEvent@SoundEngine@AK@@YA?AW4AKRESULT@@KW4AkActionOnEventType@12@_KJW4AkCurveInterpolation@@K@Z";
        constexpr char const* GetIDFromStringName = "?GetIDFromString@SoundEngine@AK@@YAKPEBD@Z";

        constexpr int AkActionOnEventType_Pause = 1;
        constexpr int AkActionOnEventType_Resume = 2;
        constexpr int AkCurveInterpolation_Linear = 4;
        /// AK_INVALID_GAME_OBJECT: every game object.
        constexpr std::uint64_t AllGameObjects = ~std::uint64_t{ 0 };

        std::atomic<t_ExecuteActionOnEvent*> ExecuteActionOnEvent_fn{ nullptr };
        std::atomic<t_GetIDFromString*> GetIDFromString_fn{ nullptr };

        /// The latch: written on the game thread, read on the movies' threads. Nothing else is done under this lock: it is
        /// taken inside Pause.cpp's fault guards, and a fault they take there could leave it held (/EHsc), so what is done
        /// under it touches only Slot.
        std::mutex LatchLock;
        Latched Slot;
        std::atomic<unsigned> ChangeCount{ 0 };
    }

    void Install()
    {
        auto const exe = GetModuleHandleW(nullptr);
        auto const execute = exe != nullptr ? reinterpret_cast<t_ExecuteActionOnEvent*>(GetProcAddress(exe, ExecuteActionOnEventName)) : nullptr;
        auto const hash = exe != nullptr ? reinterpret_cast<t_GetIDFromString*>(GetProcAddress(exe, GetIDFromStringName)) : nullptr;
        GetIDFromString_fn.store(hash, std::memory_order_release);
        ExecuteActionOnEvent_fn.store(execute, std::memory_order_release);
        if (execute == nullptr)
            LEASI_WARN("movie sound: the exe exports no {}: no movie has a known sound, Esc and Start stay the game's in every movie",
                       ExecuteActionOnEventName);
        else
            LEASI_INFO("movie sound: the exe's AK::SoundEngine::ExecuteActionOnEvent at {}, GetIDFromString at {}: a movie's Wwise event pauses with it",
                       reinterpret_cast<void*>(execute), reinterpret_cast<void*>(hash));
        if (hash == nullptr) LEASI_WARN("movie sound: the exe exports no {}: the latch lines have no hash check", GetIDFromStringName);
    }

    bool Ready() { return ExecuteActionOnEvent_fn.load(std::memory_order_acquire) != nullptr; }

    void Latch(std::string movie, unsigned id, bool beside)
    {
        std::scoped_lock lock{ LatchLock };
        Slot.Movie = std::move(movie);
        Slot.Id = id;
        Slot.Beside = beside;
        ChangeCount.fetch_add(1, std::memory_order_release);
    }

    bool Unlatch(std::string const& movie)
    {
        std::scoped_lock lock{ LatchLock };
        if (Slot.Movie.empty() || Slot.Movie != movie) return false;
        Slot = {};
        ChangeCount.fetch_add(1, std::memory_order_release);
        return true;
    }

    Latched Current()
    {
        std::scoped_lock lock{ LatchLock };
        return Slot;
    }

    unsigned Changes() { return ChangeCount.load(std::memory_order_acquire); }

    unsigned Hash(char const* name)
    {
        auto const hash = GetIDFromString_fn.load(std::memory_order_acquire);
        return hash != nullptr && name != nullptr ? hash(name) : 0;
    }

    int Set(unsigned id, bool pause)
    {
        auto const execute = ExecuteActionOnEvent_fn.load(std::memory_order_acquire);
        if (execute == nullptr) return 0;
        return execute(id, pause ? AkActionOnEventType_Pause : AkActionOnEventType_Resume, AllGameObjects, 0, AkCurveInterpolation_Linear, 0);
    }
}
