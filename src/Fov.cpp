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

    constexpr uint32_t ControllerInputYawScale = 0x45C, ControllerInputPitchScale = 0x460;
    constexpr uint32_t ControllerAcknowledgedPawn = 0x2B0;
    constexpr uint32_t PawnCurrentWeapon = 0x5F8;
    UObj* volatile BuildToolClass = nullptr;
    UObj* volatile EditToolClass = nullptr;

    struct
    {
        uint8_t* Controller = nullptr;
        float BaseYaw = 0, BasePitch = 0;
        float WrittenYaw = 0, WrittenPitch = 0;
        bool bApplied = false;
    } Look;

    void UpdateBuildSensitivity(UObj* CameraManager)
    {
        if (!BuildToolClass || !EditToolClass)
            return;
        auto Controller = *(uint8_t**)((uint8_t*)CameraManager + 0x220);
        if (!Controller)
            return;
        if (Controller != Look.Controller)
            Look = {}, Look.Controller = Controller;

        auto& YawScale = *(float*)(Controller + ControllerInputYawScale);
        auto& PitchScale = *(float*)(Controller + ControllerInputPitchScale);
        if (Look.bApplied && (YawScale != Look.WrittenYaw || PitchScale != Look.WrittenPitch))
            Look.bApplied = false;
        if (!Look.bApplied)
            Look.BaseYaw = YawScale, Look.BasePitch = PitchScale;

        float Percent = 100.f;
        auto Pawn = *(uint8_t**)(Controller + ControllerAcknowledgedPawn);
        auto Weapon = Pawn ? *(UObj**)(Pawn + PawnCurrentWeapon) : nullptr;
        if (Weapon && IsA(Weapon, EditToolClass))
            Percent = Tellurium::CustomSettings::Settings.EditSensitivity;
        else if (Weapon && IsA(Weapon, BuildToolClass))
            Percent = Tellurium::CustomSettings::Settings.BuildSensitivity;

        if (Percent != 100.f)
        {
            YawScale = Look.BaseYaw * Percent / 100.f;
            PitchScale = Look.BasePitch * Percent / 100.f;
            Look.WrittenYaw = YawScale, Look.WrittenPitch = PitchScale;
            Look.bApplied = true;
        }
        else if (Look.bApplied)
        {
            YawScale = Look.BaseYaw, PitchScale = Look.BasePitch;
            Look.bApplied = false;
        }
    }

    float __fastcall GetFOVAngleHook(UObj* CameraManager)
    {
        GetFOVAngleFn Original = nullptr;
        for (long i = 0; i < PatchedCount; i++)
            if (Patched[i].VTable == CameraManager->VTable)
                Original = Patched[i].Original;
        if (!Original)
            Original = Patched[0].Original;

        float FOV = Original(CameraManager);
        __try { UpdateBuildSensitivity(CameraManager); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
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
                if (!BuildToolClass) BuildToolClass = FindClass(L"FortWeap_BuildingTool");
                if (!EditToolClass) EditToolClass = FindClass(L"FortWeap_EditingTool");
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
