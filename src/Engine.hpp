#pragma once

#include "PauseAnytime/Pause.hpp"
#include "PauseAnytime/game/Game.hpp"

#include <iterator>
#include <string>
#include <string_view>

// Helpers for calling into the game and reading its objects, shared by the world pause (Pause.cpp) and photo mode. Game
// thread only, unless a comment says otherwise.
namespace PauseAnytime
{
    // ! Modes
    // ========================================

    inline constexpr unsigned char NoMode = 0xFF;

    inline char const* ModeName(unsigned char mode)
    {
        return static_cast<size_t>(mode) < std::size(Game::ModeNames) ? Game::ModeNames[mode] : "none";
    }

    inline unsigned char ModeOf(ABioPlayerController* pc)
    {
        auto const manager = Game::ModeManagerOf(pc);
        return manager != nullptr ? manager->CurrentMode : NoMode;
    }

    inline double HeldFor(ULONGLONG since) { return static_cast<double>(GetTickCount64() - since) / 1000.0; }

    // ! Objects and names
    // ========================================

    std::string ToUtf8(std::wstring_view text);

    std::string PathOf(UObject const* object);

    /// Still the object the index pointed at: not destroyed, not replaced by a map change.
    bool Alive(UObject* object, int index);

    std::string NameText(SFXName const& name);

    /// A game object we changed, with its object index: checked again before each write back.
    template <class T>
    struct Kept
    {
        T* Object = nullptr;
        int Index = -1;

        void Set(T* object)
        {
            Object = object;
            Index = object != nullptr ? object->ObjectInternalInteger : -1;
        }

        T* Live() const { return Alive(Object, Index) ? Object : nullptr; }
    };

    // ! Calls
    // ========================================

    /// Set while we call into the game (Pause, Canvas): those calls only pass through our hook.
    extern thread_local bool InOwnCall;

    /// The most derived function of that name: what a console command would run.
    UFunction* FindFunction(UClass* cls, SFXName const& name);

    /// A function of an object's class, looked up again when the class changes.
    struct CachedFunction
    {
        UClass* Class = nullptr;
        UFunction* Function = nullptr;
    };

    UFunction* FunctionOf(UObject* object, wchar_t const* name, CachedFunction& cache);

    /// ProcessEvent refuses a function with an opcode number (iNative): zeroed for the call, it runs the native like any
    /// other function, as LEX Interop and the bridge do. Opcode and flags are put back as they were; the SDK's generated
    /// wrappers instead leave every flag bit set. No C++ object may live in this frame.
    void CallGame(UObject* object, UFunction* function, void* parms);

    /// Calls that function of the object, when its class has one (logged once per class when not).
    template <class Parms>
    bool Call(UObject* object, wchar_t const* name, CachedFunction& cache, Parms& parms)
    {
        auto const function = FunctionOf(object, name, cache);
        if (function == nullptr) return false;
        CallGame(object, function, &parms);
        return true;
    }

    /// An access violation in our code (a pointer the checks missed) fails that call, not the game. No C++ object may
    /// live in this frame: __try cannot unwind one.
    bool Guarded(void (*work)(void*), void* context);
}
