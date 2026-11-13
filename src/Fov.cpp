#include "pch.h"
#include "Fov.h"
#include "Engine1241.h"
#include "CustomSettings.h"
#include "SettingsTab.h"

using namespace Engine1241;

namespace
{
    constexpr int GetFOVAngleIndex = 0x6C0 / 8;
    constexpr uint32_t PlayerPlayerController = 0x30;
    constexpr uint32_t ControllerCameraManager = 0x2B0;
    constexpr float GameFOV = 80.f;

    constexpr uint32_t ControllerInputYawScale = 0x424, ControllerInputPitchScale = 0x428;
    constexpr uint32_t ControllerAcknowledgedPawn = 0x298;
    constexpr uint32_t PawnCurrentWeapon = 0x588;
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
        auto Controller = *(uint8_t**)((uint8_t*)CameraManager + 0x218);
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

    using GetFOVAngleFn = float(__fastcall*)(UObj*);
    struct FPatchedVTable { void** VTable; GetFOVAngleFn Original; };
    FPatchedVTable Patched[16]{};
    volatile long PatchedCount = 0;
    volatile bool bInMatch = false;

    float __fastcall GetFOVAngleHook(UObj* CameraManager)
    {
        GetFOVAngleFn Original = nullptr;
        for (long i = 0; i < PatchedCount; i++)
            if (Patched[i].VTable == CameraManager->VTable)
                Original = Patched[i].Original;
        if (!Original)
            Original = Patched[0].Original;

        Tellurium::SettingsTab::GameThreadTick();
        __try { UpdateBuildSensitivity(CameraManager); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        float GameValue = Original(CameraManager);
        float FOV = GameValue;
        auto& S = Tellurium::CustomSettings::Settings;
        if (S.bCustomFOV && FOV > 1.f)
        {
            FOV *= S.FOV / GameFOV;
            FOV = FOV < 5.f ? 5.f : FOV > 170.f ? 170.f : FOV;
        }

        static UObj* LoggedFor = nullptr;
        if (bInMatch && CameraManager != LoggedFor)
        {
            LoggedFor = CameraManager;
            Log("[Fov] in a match: Custom FOV %s, slider %.0f, game FOV %.1f -> %.1f",
                S.bCustomFOV ? "on" : "off", S.FOV, GameValue, FOV);
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
        Log("[Fov] hooked GetFOVAngle on a camera manager vtable");
    }

    void Tick()
    {
        __try
        {
            auto World = Engine1241::World();
            bInMatch = World && !NameIs(World->Name, L"Frontend");

            auto Player = (uint8_t*)LocalPlayer();
            auto Controller = Player ? *(uint8_t**)(Player + PlayerPlayerController) : nullptr;
            auto CameraManager = Controller ? *(UObj**)(Controller + ControllerCameraManager) : nullptr;
            if (CameraManager && CameraManager->VTable)
                PatchCameraManager(CameraManager);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    DWORD WINAPI FovThread(LPVOID)
    {
        while (true)
        {
            if (!BuildToolClass) BuildToolClass = FindClass(L"FortWeap_BuildingTool");
            if (!EditToolClass) EditToolClass = FindClass(L"FortWeap_EditingTool");
            Tick();
            Sleep(500);
        }
        return 0;
    }
}

bool Tellurium::Fov::InMatch()
{
    return bInMatch;
}

bool Tellurium::Fov::Start()
{
    if (!IsSupported())
        return false;
    Log("[Shatter] 12.41 DLL built " __DATE__ " " __TIME__);
    Tellurium::CustomSettings::Load();
    CreateThread(0, 0, FovThread, 0, 0, 0);
    return true;
}
