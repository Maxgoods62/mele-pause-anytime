#include "MelePause/Photo.hpp"
#include "Common/Base.hpp"

// A game without our photo mode (MELEPAUSE_PHOTO 0 in game/LE<n>.hpp): Y and F7 stay the game's, the pause screen offers no
// photo key.
#if !MELEPAUSE_PHOTO

namespace MelePause::PhotoMode
{
    void InstallHooks(::LESDK::Initializer& Init)
    {
        LEASI_UNUSED(Init);
        LEASI_INFO("photo mode under the pause: not in the {} build", SDK_TARGET_NAME_A);
    }

    bool Answers() { return false; }

    bool On() { return false; }

    bool IsController(UObject* object)
    {
        LEASI_UNUSED(object);
        return false;
    }

    char const* Enter(ABioPlayerController* pc, unsigned char mode, bool gamepad)
    {
        LEASI_UNUSED_3(pc, mode, gamepad);
        return nullptr;
    }

    void Drop(char const* why, bool worldGone) { LEASI_UNUSED_2(why, worldGone); }

    void Check(bool worldPaused, AWorldInfo* pausedWorld) { LEASI_UNUSED_2(worldPaused, pausedWorld); }

    void Tick(ABioPlayerController* pc, float dt) { LEASI_UNUSED_2(pc, dt); }

    void Frame() {}
}

#endif
