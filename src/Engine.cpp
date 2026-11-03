#include "PauseAnytime/Engine.hpp"
#include "Common/Objects.hpp"

#include <cstdint>

namespace PauseAnytime
{
    thread_local bool InOwnCall = false;

    namespace Fault
    {
        namespace
        {
            /// The latest fault Filter took on this thread, until Log reads it.
            struct Taken
            {
                void const* At = nullptr;
                ULONG_PTR Access = 0;
                ULONG_PTR Address = 0;
                bool Have = false;
            };
            thread_local Taken Last;

            /// In this ASI's image: GetModuleHandleEx finds the ASI from it.
            char const Anchor = 0;
        }

        int Filter(EXCEPTION_POINTERS const* info)
        {
            auto const record = info != nullptr ? info->ExceptionRecord : nullptr;
            if (record == nullptr || record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
            Last.At = record->ExceptionAddress;
            // An access violation's two parameters: 0 read, 1 write, 8 execute (DEP); then the address it touched.
            Last.Access = record->NumberParameters >= 2 ? record->ExceptionInformation[0] : ~ULONG_PTR{ 0 };
            Last.Address = record->NumberParameters >= 2 ? record->ExceptionInformation[1] : 0;
            Last.Have = true;
            return EXCEPTION_EXECUTE_HANDLER;
        }

        void Log(char const* what)
        {
            if (!Last.Have)
            {
                LEASI_ERROR("{}: access violation, where not known", what);
                return;
            }
            Last.Have = false;
            auto const access = Last.Access == 0 ? "reading" : Last.Access == 1 ? "writing" : Last.Access == 8 ? "running" : "touching";
            constexpr DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
            HMODULE module = nullptr;
            HMODULE self = nullptr;
            wchar_t path[MAX_PATH]{};
            if (!GetModuleHandleExW(flags, static_cast<LPCWSTR>(Last.At), &module) || module == nullptr || GetModuleFileNameW(module, path, MAX_PATH) == 0)
            {
                LEASI_ERROR("{}: access violation {} 0x{:X}, raised at {} (in no module)", what, access, Last.Address, Last.At);
                return;
            }
            GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&Anchor), &self);
            std::wstring_view name{ path };
            if (auto const slash = name.find_last_of(L"\\/"); slash != std::wstring_view::npos) name.remove_prefix(slash + 1);
            auto const whose = module == self ? "Pause Anytime's code" : module == GetModuleHandleW(nullptr) ? "the game's code" : "another module's code";
            LEASI_ERROR("{}: access violation {} 0x{:X}, raised at {}+0x{:X} ({})", what, access, Last.Address, ToUtf8(name),
                        reinterpret_cast<std::uintptr_t>(Last.At) - reinterpret_cast<std::uintptr_t>(module), whose);
        }
    }

    std::string ToUtf8(std::wstring_view text)
    {
        if (text.empty()) return {};
        auto const size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
        return out;
    }

    std::string PathOf(UObject const* object)
    {
        if (object == nullptr) return "None";
        auto const path = object->GetFullPath();
        return ToUtf8(std::wstring_view(path.Chars(), path.Length()));
    }

    bool Alive(UObject* object, int index)
    {
        auto& objects = *UObject::GObjObjects;
        return object != nullptr && index >= 0 && static_cast<UINT>(index) < objects.Count() && objects.GetData()[index] == object
            && !Common::IsDestroyed(object);
    }

    std::string NameText(SFXName const& name)
    {
        auto const text = name.Instanced();
        return ToUtf8(std::wstring_view(text.Chars(), text.Length()));
    }

    UFunction* FindFunction(UClass* cls, SFXName const& name)
    {
        for (UStruct* s = cls; s != nullptr; s = static_cast<UStruct*>(s->SuperField))
        {
            for (UField* f = s->Children; f != nullptr; f = f->Next)
            {
                if (f->Name == name && f->IsA(UFunction::StaticClass())) return static_cast<UFunction*>(f);
            }
        }
        return nullptr;
    }

    UFunction* FunctionOf(UObject* object, wchar_t const* name, CachedFunction& cache)
    {
        if (object->Class != cache.Class)
        {
            cache.Class = object->Class;
            cache.Function = FindFunction(object->Class, SFXName{ name, 0 });
            if (cache.Function == nullptr) LEASI_ERROR("no {} function in {}", ToUtf8(name), PathOf(object->Class));
        }
        return cache.Function;
    }

    void CallGame(UObject* object, UFunction* function, void* parms)
    {
        auto const native = function->iNative;
        auto const flags = function->FunctionFlags;
        auto const outer = InOwnCall;
        function->iNative = 0;
        InOwnCall = true;
        __try
        {
            object->ProcessEvent(function, parms, nullptr);
        }
        __finally
        {
            InOwnCall = outer;
            function->iNative = native;
            function->FunctionFlags = flags;
        }
    }

    bool Guarded(void (*work)(void*), void* context)
    {
        __try
        {
            work(context);
            return true;
        }
        __except (Fault::Filter(GetExceptionInformation()))
        {
            // Where it was raised: in the game's code, the fault may be a stale pointer of ours it was handed.
            Fault::Log("guarded call");
            return false;
        }
    }
}
