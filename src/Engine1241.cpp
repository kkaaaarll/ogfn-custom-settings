#include "pch.h"
#include "Engine1241.h"
#include "Unreal.h"
#include "Options.h"
#include <shlobj.h>
#include <cstdio>
#include <cstdarg>
#include <cwchar>

namespace
{
    constexpr uint64_t GObjectsRva = 0x8067DF0;
    constexpr uint64_t AppendStringRva = 0x2C1DEA0;
    constexpr uint64_t GWorldRva = 0x8158708;
    constexpr uint32_t WorldOwningGameInstance = 0x188;
    constexpr uint32_t GameInstanceLocalPlayers = 0x38;

    Engine1241::UObj* StringToNameFunc = nullptr;
    Engine1241::UObj* KismetStringDefault = nullptr;
    SRWLOCK LogLock = SRWLOCK_INIT;

    uint8_t* ItemAt(int32_t Index)
    {
        auto Chunks = *(uint8_t***)(Engine1241::Base + GObjectsRva);
        auto Chunk = Chunks ? Chunks[Index / 0x10000] : nullptr;
        return Chunk ? Chunk + (Index % 0x10000) * 24 : nullptr;
    }
}

uint64_t Engine1241::Base = 0;

bool Engine1241::IsSupported()
{
    Base = (uint64_t)GetModuleHandleW(nullptr);
    auto Dos = (IMAGE_DOS_HEADER*)Base;
    auto Nt = (IMAGE_NT_HEADERS64*)(Base + Dos->e_lfanew);
    return Nt->FileHeader.TimeDateStamp == ExeTimeDateStamp;
}

int32_t Engine1241::ObjectCount()
{
    return *(int32_t*)(Base + GObjectsRva + 0x14);
}

Engine1241::UObj* Engine1241::ObjectAt(int32_t Index)
{
    auto Item = ItemAt(Index);
    return Item ? *(UObj**)Item : nullptr;
}

int32_t Engine1241::SerialAt(int32_t Index)
{
    auto Item = ItemAt(Index);
    return Item ? *(int32_t*)(Item + 0x10) : 0;
}

void Engine1241::KeepAlive(UObj* Object)
{
    auto Item = Object ? ItemAt(Object->InternalIndex) : nullptr;
    if (Item)
        InterlockedOr((volatile long*)(Item + 8), 1 << 30);
}

bool Engine1241::NameIs(FName Name, const wchar_t* Text)
{
    FStr Out{};
    ((void(*)(FName*, FStr*))(Base + AppendStringRva))(&Name, &Out);
    bool bSame = Out.Data && wcscmp(Out.Data, Text) == 0;
    if (Out.Data)
        Tellurium::Unreal::FMemory::Free(Out.Data);
    return bSame;
}

void Engine1241::NameToString(FName Name, wchar_t* Out, size_t Count)
{
    FStr Text{};
    ((void(*)(FName*, FStr*))(Base + AppendStringRva))(&Name, &Text);
    wcsncpy_s(Out, Count, Text.Data ? Text.Data : L"", _TRUNCATE);
    if (Text.Data)
        Tellurium::Unreal::FMemory::Free(Text.Data);
}

bool Engine1241::IsA(UObj* Object, UObj* Class)
{
    if (!Object || !Class)
        return false;
    for (auto Struct = Object->Class; Struct; Struct = *(UObj**)((uint8_t*)Struct + StructSuper))
        if (Struct == Class)
            return true;
    return false;
}

Engine1241::UObj* Engine1241::FindObject(const wchar_t* Name)
{
    __try
    {
        int32_t Num = ObjectCount();
        for (int32_t i = 0; i < Num; i++)
        {
            auto Object = ObjectAt(i);
            if (Object && NameIs(Object->Name, Name))
                return Object;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

Engine1241::UObj* Engine1241::FindClass(const wchar_t* Name)
{
    __try
    {
        int32_t Num = ObjectCount();
        for (int32_t i = 0; i < Num; i++)
        {
            auto Object = ObjectAt(i);
            if (Object && Object->Class && NameIs(Object->Name, Name) && NameIs(Object->Class->Name, L"Class"))
                return Object;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

Engine1241::UObj* Engine1241::FindFunction(const wchar_t* Name, const wchar_t* OuterName)
{
    __try
    {
        int32_t Num = ObjectCount();
        for (int32_t i = 0; i < Num; i++)
        {
            auto Object = ObjectAt(i);
            if (Object && Object->Outer && NameIs(Object->Name, Name) && NameIs(Object->Outer->Name, OuterName))
                return Object;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

void Engine1241::ProcessEvent(UObj* Object, UObj* Function, void* Params)
{
    ((void(*)(UObj*, UObj*, void*))Object->VTable[ProcessEventIndex])(Object, Function, Params);
}

Engine1241::FStr Engine1241::MakeString(const wchar_t* Text)
{
    FStr Out{};
    auto Length = (int32_t)wcslen(Text) + 1;
    Out.Data = Tellurium::Unreal::FMemory::Malloc<wchar_t>(Length * sizeof(wchar_t), alignof(wchar_t));
    if (Out.Data)
    {
        memcpy(Out.Data, Text, Length * sizeof(wchar_t));
        Out.Num = Out.Max = Length;
    }
    return Out;
}

bool Engine1241::FindEngineObjects()
{
    if (!StringToNameFunc)
        StringToNameFunc = FindFunction(L"Conv_StringToName", L"KismetStringLibrary");
    if (!KismetStringDefault)
        KismetStringDefault = FindObject(L"Default__KismetStringLibrary");
    return StringToNameFunc && KismetStringDefault;
}

Engine1241::FName Engine1241::MakeName(const wchar_t* Text)
{
    struct { FStr InString; FName ReturnValue; } Params{};
    Params.InString = MakeString(Text);
    ProcessEvent(KismetStringDefault, StringToNameFunc, &Params);
    if (Params.InString.Data)
        Tellurium::Unreal::FMemory::Free(Params.InString.Data);
    return Params.ReturnValue;
}

Engine1241::UObj* Engine1241::World()
{
    return *(UObj**)(Base + GWorldRva);
}

Engine1241::UObj* Engine1241::LocalPlayer()
{
    __try
    {
        auto World = (uint8_t*)Engine1241::World();
        auto GameInstance = World ? *(uint8_t**)(World + WorldOwningGameInstance) : nullptr;
        if (!GameInstance)
            return nullptr;
        auto& LocalPlayers = *(FStr*)(GameInstance + GameInstanceLocalPlayers);
        return LocalPlayers.Num > 0 && LocalPlayers.Data ? *(UObj**)LocalPlayers.Data : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

void Engine1241::Log(const char* Format, ...)
{
    char Line[1024];
    SYSTEMTIME Time;
    GetLocalTime(&Time);
    int Length = snprintf(Line, sizeof(Line), "[%02d:%02d:%02d] ", Time.wHour, Time.wMinute, Time.wSecond);
    va_list Args;
    va_start(Args, Format);
    vsnprintf(Line + Length, sizeof(Line) - Length, Format, Args);
    va_end(Args);

    if (Console)
        printf("%s\n", Line);

    wchar_t* Local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &Local)))
        return;
    wchar_t Path[MAX_PATH];
    swprintf_s(Path, L"%s\\Shatter", Local);
    CoTaskMemFree(Local);
    CreateDirectoryW(Path, nullptr);
    wcscat_s(Path, L"\\client.log");

    AcquireSRWLockExclusive(&LogLock);
    FILE* File = nullptr;
    if (_wfopen_s(&File, Path, L"a") == 0 && File)
    {
        fprintf(File, "%s\n", Line);
        fclose(File);
    }
    ReleaseSRWLockExclusive(&LogLock);
}
