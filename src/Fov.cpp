#include "pch.h"
#include "Fov.h"
#include "Engine1910.h"
#include "CustomSettings.h"

using namespace Engine1910;

namespace
{
    constexpr uint64_t GWorldRva = 0xB59BD30;
    constexpr int GetFOVAngleIndex = 0x740 / 8;

    constexpr uint32_t WorldOwningGameInstance = 0x190;
    constexpr uint32_t GameInstanceLocalPlayers = 0x38;
    constexpr uint32_t PlayerPlayerController = 0x30;
    constexpr uint32_t ControllerCameraManager = 0x2C8;

    constexpr float GameFOV = 80.f;

    using GetFOVAngleFn = float(__fastcall*)(UObj*);
    struct FPatchedVTable { void** VTable; GetFOVAngleFn Original; };
    FPatchedVTable Patched[16]{};
    volatile long PatchedCount = 0;

    float __fastcall GetFOVAngleHook(UObj* CameraManager)
    {
        GetFOVAngleFn Original = nullptr;
        for (long i = 0; i < PatchedCount; i++)
            if (Patched[i].VTable == CameraManager->VTable)
                Original = Patched[i].Original;
        if (!Original)
            Original = Patched[0].Original;

        float FOV = Original(CameraManager);
        auto& S = Tellurium::CustomSettings::Settings;
        if (S.bCustomFOV && FOV > 1.f)
        {
            FOV *= S.FOV / GameFOV;
            FOV = FOV < 5.f ? 5.f : FOV > 170.f ? 170.f : FOV;
        }
        return FOV;
    }

    void PatchCameraManager(UObj* CameraManager)
    {
        void** VTable = CameraManager->VTable;
        if (VTable[GetFOVAngleIndex] == (void*)&GetFOVAngleHook || PatchedCount >= 16)
            return;

        Patched[PatchedCount] = { VTable, (GetFOVAngleFn)VTable[GetFOVAngleIndex] };
        InterlockedIncrement(&PatchedCount);

        DWORD OldProtect;
        VirtualProtect(&VTable[GetFOVAngleIndex], sizeof(void*), PAGE_READWRITE, &OldProtect);
        VTable[GetFOVAngleIndex] = (void*)&GetFOVAngleHook;
        VirtualProtect(&VTable[GetFOVAngleIndex], sizeof(void*), OldProtect, &OldProtect);
    }

    UObj* LocalCameraManager()
    {
        auto World = *(uint8_t**)(Base + GWorldRva);
        auto GameInstance = World ? *(uint8_t**)(World + WorldOwningGameInstance) : nullptr;
        if (!GameInstance)
            return nullptr;
        auto& LocalPlayers = *(FStr*)(GameInstance + GameInstanceLocalPlayers);
        if (LocalPlayers.Num <= 0 || !LocalPlayers.Data)
            return nullptr;
        auto Player = *(uint8_t**)LocalPlayers.Data;
        auto Controller = Player ? *(uint8_t**)(Player + PlayerPlayerController) : nullptr;
        return Controller ? *(UObj**)(Controller + ControllerCameraManager) : nullptr;
    }

    DWORD WINAPI FovThread(LPVOID)
    {
        while (true)
        {
            __try
            {
                auto CameraManager = LocalCameraManager();
                if (CameraManager && CameraManager->VTable)
                    PatchCameraManager(CameraManager);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            Sleep(500);
        }
        return 0;
    }
}

bool Tellurium::Fov::Start()
{
    if (!IsSupported())
        return false;
    Tellurium::CustomSettings::Load();
    CreateThread(0, 0, FovThread, 0, 0, 0);
    return true;
}
