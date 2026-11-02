#include "PauseAnytime/Engine.hpp"
#include "Common/Objects.hpp"

namespace PauseAnytime
{
    thread_local bool InOwnCall = false;

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
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            return false;
        }
    }
}
