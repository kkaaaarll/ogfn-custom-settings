#include "pch.h"
#include "Engine1910.h"
#include "Unreal.h"
#include <shlobj.h>
#include <cstdio>
#include <cstdarg>
#include <cwchar>

namespace
{
    constexpr uint64_t GObjectsRva = 0xB3C9A78;
    constexpr uint64_t AppendStringRva = 0xEC0900;

    Engine1910::UObj* StringToNameFunc = nullptr;
    Engine1910::UObj* KismetStringDefault = nullptr;
    SRWLOCK LogLock = SRWLOCK_INIT;
}

uint64_t Engine1910::Base = 0;

bool Engine1910::IsSupported()
{
    Base = (uint64_t)GetModuleHandleW(nullptr);
    auto Dos = (IMAGE_DOS_HEADER*)Base;
    auto Nt = (IMAGE_NT_HEADERS64*)(Base + Dos->e_lfanew);
    return Nt->FileHeader.TimeDateStamp == ExeTimeDateStamp;
}

int32_t Engine1910::ObjectCount()
{
    return *(int32_t*)(Base + GObjectsRva + 0x14);
}

Engine1910::UObj* Engine1910::ObjectAt(int32_t Index)
{
    auto Chunks = *(uint8_t***)(Base + GObjectsRva);
    auto Item = Chunks[Index / 0x10000];
    return Item ? *(UObj**)(Item + (Index % 0x10000) * 24) : nullptr;
}

bool Engine1910::NameIs(FName Name, const wchar_t* Text)
{
    FStr Out{};
    ((void(*)(FName*, FStr*))(Base + AppendStringRva))(&Name, &Out);
    bool bSame = Out.Data && wcscmp(Out.Data, Text) == 0;
    if (Out.Data)
        Tellurium::Unreal::FMemory::Free(Out.Data);
    return bSame;
}

bool Engine1910::IsA(UObj* Object, UObj* Class)
{
    if (!Object || !Class)
        return false;
    for (auto Struct = Object->Class; Struct; Struct = *(UObj**)((uint8_t*)Struct + StructSuper))
        if (Struct == Class)
            return true;
    return false;
}

Engine1910::UObj* Engine1910::FindObject(const wchar_t* Name)
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

Engine1910::UObj* Engine1910::FindClass(const wchar_t* Name)
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

Engine1910::UObj* Engine1910::FindFunction(const wchar_t* Name, const wchar_t* OuterName)
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

void Engine1910::ProcessEvent(UObj* Object, UObj* Function, void* Params)
{
    ((void(*)(UObj*, UObj*, void*))Object->VTable[ProcessEventIndex])(Object, Function, Params);
}

Engine1910::FStr Engine1910::MakeString(const wchar_t* Text)
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

bool Engine1910::FindEngineObjects()
{
    if (!StringToNameFunc)
        StringToNameFunc = FindFunction(L"Conv_StringToName", L"KismetStringLibrary");
    if (!KismetStringDefault)
        KismetStringDefault = FindObject(L"Default__KismetStringLibrary");
    return StringToNameFunc && KismetStringDefault;
}

Engine1910::FName Engine1910::MakeName(const wchar_t* Text)
{
    struct { FStr InString; FName ReturnValue; } Params{};
    Params.InString = MakeString(Text);
    ProcessEvent(KismetStringDefault, StringToNameFunc, &Params);
    if (Params.InString.Data)
        Tellurium::Unreal::FMemory::Free(Params.InString.Data);
    return Params.ReturnValue;
}

Engine1910::UObj* Engine1910::LocalPlayer()
{
    __try
    {
        auto World = *(uint8_t**)(Base + 0xB59BD30);
        auto GameInstance = World ? *(uint8_t**)(World + 0x190) : nullptr;
        if (!GameInstance)
            return nullptr;
        auto& LocalPlayers = *(FStr*)(GameInstance + 0x38);
        return LocalPlayers.Num > 0 && LocalPlayers.Data ? *(UObj**)LocalPlayers.Data : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

void Engine1910::Log(const char* Format, ...)
{
    char Line[1024];
    SYSTEMTIME Time;
    GetLocalTime(&Time);
    int Length = snprintf(Line, sizeof(Line), "[%02d:%02d:%02d] ", Time.wHour, Time.wMinute, Time.wSecond);
    va_list Args;
    va_start(Args, Format);
    vsnprintf(Line + Length, sizeof(Line) - Length, Format, Args);
    va_end(Args);

    wchar_t* Local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &Local)))
        return;
    wchar_t Path[MAX_PATH];
    swprintf_s(Path, L"%s\\Refract", Local);
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
