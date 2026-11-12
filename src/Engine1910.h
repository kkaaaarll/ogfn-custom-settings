#pragma once
#include <cstdint>

namespace Engine1910
{
    constexpr uint32_t ExeTimeDateStamp = 0x61E9A85E;
    constexpr int ProcessEventIndex = 0x4B;
    constexpr uint32_t StructSuper = 0x40;
    constexpr uint32_t ClassDefaultObject = 0x118;
    constexpr int32_t RF_ClassDefaultObject = 0x10;

    struct FName { int32_t Index; int32_t Number; };
    struct FStr { wchar_t* Data; int32_t Num; int32_t Max; };
    struct FText { void* Data; void* Controller; uint32_t Flags; uint32_t Pad; };

    struct UObj
    {
        void** VTable;
        int32_t Flags;
        int32_t InternalIndex;
        UObj* Class;
        FName Name;
        UObj* Outer;
    };

    extern uint64_t Base;

    bool IsSupported();
    template <typename T> T At(uint64_t Rva) { return (T)(Base + Rva); }

    int32_t ObjectCount();
    UObj* ObjectAt(int32_t Index);
    bool NameIs(FName Name, const wchar_t* Text);
    bool IsA(UObj* Object, UObj* Class);
    UObj* FindClass(const wchar_t* Name);
    UObj* FindFunction(const wchar_t* Name, const wchar_t* OuterName);
    UObj* FindObject(const wchar_t* Name);

    void ProcessEvent(UObj* Object, UObj* Function, void* Params);
    FStr MakeString(const wchar_t* Text);
    FName MakeName(const wchar_t* Text);
    bool FindEngineObjects();

    UObj* LocalPlayer();

    void Log(const char* Format, ...);
}
