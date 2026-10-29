#include <LESDK/Headers.hpp>
#include "MelePause/Api.hpp"

#include <atomic>
#include <cstring>
#include <span>

// The engine's movie input: the Bink movie player's InputKey (native, an FViewportClient's), which a key event reaches before
// Console.InputKey while a fullscreen movie plays. On a press of one of its keys (in LE2 Escape, Enter, the left mouse
// button, and the pad's Start, Back and A) it asks the player to stop the movie and says it handled the key: Console.InputKey
// never sees that press. The movie stops only if the game lets it skip (the relay jump took the key and played on). Space
// and the pad's X aren't among those keys: SFXGameModeMovie binds them to SkipMovie, which does nothing in LE2. So on a
// movie we can pause, Esc and Start skipped it before our pause could hold. The hook:
// - Esc and Start while the pause keys are the movies' (Bink::OwnsKeys): not given to it. Their press goes on to
//   Console.InputKey: the movie's thread toggles the pause on Esc, Pause.cpp on Start (only Start's repeats and release got
//   there before).
// - Every press while our movie pause is on: not given to it, so no key skips a paused movie. Space and A resume it instead.
// - Space and X, the movie playing: given to it as Enter, so they skip what Esc skipped, and only that.
// Any other event goes to it as it came. Game thread only; it reads no game object.
namespace MelePause::MovieInput
{
    namespace
    {
        // UBOOL InputKey(FViewport*, INT ControllerId, FName Key, EInputEvent Event, FLOAT AmountDepressed, UBOOL bGamepad), on
        // the player's FViewportClient part. SFXName is 8 bytes and trivially copyable: passed in a register, as FName is.
        using t_InputKey = unsigned long(void* self, void* viewport, int controllerId, SFXName key, int event, float amount, unsigned long gamepad);

        std::atomic<t_InputKey*> InputKey_orig{ nullptr };

        /// EInputEvent's IE_Pressed: the only event the movie input acts on.
        constexpr int Pressed = 0;

        /// The loader makes the hook live before InstallHook hands back the original: a call arriving first waits for it.
        t_InputKey* Original()
        {
            auto f = InputKey_orig.load(std::memory_order_acquire);
            while (f == nullptr)
            {
                YieldProcessor();
                f = InputKey_orig.load(std::memory_order_acquire);
            }
            return f;
        }

        unsigned long InputKey_hook(void* self, void* viewport, int controllerId, SFXName key, int event, float amount, unsigned long gamepad)
        {
            auto const orig = Original();
            if (event != Pressed) return orig(self, viewport, controllerId, key, event, amount, gamepad);

            static SFXName const Escape{ L"Escape", 0 };
            static SFXName const Start{ L"XboxTypeS_Start", 0 };
            static SFXName const Space{ L"SpaceBar", 0 };
            static SFXName const PadX{ L"XboxTypeS_X", 0 };
            static SFXName const Enter{ L"Enter", 0 };
            auto const pauseKey = key == Escape || key == Start;
            auto const skipKey = key == Space || key == PadX;
            if (!pauseKey && !skipKey && !Bink::PauseWanted()) return orig(self, viewport, controllerId, key, event, amount, gamepad);

            auto const name = key == Escape ? "Esc" : key == Start ? "Start" : key == Space ? "Space" : key == PadX ? "X" : "a key";
            if (Bink::PauseWanted() && Bink::OwnsKeys())
            {
                LEASI_INFO("{} pressed under our movie pause: kept from the engine's movie input, which would skip the movie", name);
                return 0;
            }
            if (pauseKey)
            {
                if (!Bink::OwnsKeys()) return orig(self, viewport, controllerId, key, event, amount, gamepad);
                LEASI_INFO("{} pressed during a fullscreen movie: kept from the engine's movie input, which would skip it; Console.InputKey gets it",
                           name);
                return 0;
            }
            // With no movie open there is nothing to skip, and nothing to log.
            if (!skipKey || Bink::OpenMovies() == 0) return orig(self, viewport, controllerId, key, event, amount, gamepad);
            auto const handled = orig(self, viewport, controllerId, Enter, event, amount, gamepad);
            LEASI_INFO("{} pressed during a movie: given to the engine's movie input as Enter: {}", name,
                       handled != 0 ? "taken, the movie stops if the game lets it skip" : "not taken");
            return handled;
        }
    }

    void Install(::LESDK::Initializer& Init, unsigned long long rva, std::span<unsigned char const> start)
    {
        if (rva == 0) return;
        auto const target = Init.ResolveTyped<t_InputKey>(::LESDK::Address::FromOffset(rva));
        if (target == nullptr || std::memcmp(reinterpret_cast<void const*>(target), start.data(), start.size()) != 0)
        {
            LEASI_WARN("the engine's movie input not found at RVA {:#x} (another build of the game?): Esc and Start skip a movie the game lets skip, Space and X don't",
                       rva);
            return;
        }
        InputKey_orig.store(static_cast<t_InputKey*>(Init.InstallHook("FullScreenMovie::InputKey", reinterpret_cast<void*>(target),
                                                                       reinterpret_cast<void*>(InputKey_hook))),
                            std::memory_order_release);
        if (InputKey_orig.load() == nullptr)
        {
            LEASI_WARN("hooking the engine's movie input failed: Esc and Start skip a movie the game lets skip, Space and X don't");
            return;
        }
        LEASI_INFO("movie input hooked: on a movie we can pause, Esc and Start pause it; Space and X skip a movie the game lets skip");
    }
}
