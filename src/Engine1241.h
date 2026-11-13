#pragma once
#include <cstdint>

namespace Engine1241
{
    constexpr uint32_t ExeTimeDateStamp = 0x5E9B1A8B;
    constexpr int ProcessEventIndex = 0x42;
    constexpr uint32_t StructSuper = 0x40;
    constexpr uint32_t ClassDefaultObject = 0x130;

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
    int32_t SerialAt(int32_t Index);
    void KeepAlive(UObj* Object);
    bool NameIs(FName Name, const wchar_t* Text);
    void NameToString(FName Name, wchar_t* Out, size_t Count);
    bool IsA(UObj* Object, UObj* Class);
    UObj* FindClass(const wchar_t* Name);
    UObj* FindFunction(const wchar_t* Name, const wchar_t* OuterName);
    UObj* FindObject(const wchar_t* Name);

    void ProcessEvent(UObj* Object, UObj* Function, void* Params);
    FStr MakeString(const wchar_t* Text);
    FName MakeName(const wchar_t* Text);
    bool FindEngineObjects();

    UObj* World();
    UObj* LocalPlayer();

    void Log(const char* Format, ...);
}
