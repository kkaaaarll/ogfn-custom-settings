#include "pch.h"
#include "SettingsTab.h"
#include "Engine1910.h"
#include "CustomSettings.h"
#include "Unreal.h"
#include "TabIcon.h"
#include <shlobj.h>
#include <cstdio>
#include <cmath>
#include <cwchar>

using namespace Engine1910;

namespace
{
    constexpr uint64_t RegistryVTableRva = 0x8898158;
    constexpr int OnInitializeIndex = 0x56;
    constexpr uint64_t RegisterSettingRva = 0x1773BB4;
    constexpr uint64_t FindSettingByDevNameRva = 0x1423EFC;
    constexpr uint64_t TransientPackageRva = 0xB44B640;

    constexpr uint64_t StaticConstructObjectRva = 0xB049F0;
    constexpr uint64_t FStringCtorRva = 0xBA100C;
    constexpr uint64_t MakeTextRva = 0xC8D458;
    constexpr uint64_t SetDevNameRva = 0x4DA906C;
    constexpr uint64_t SetDisplayNameRva = 0x4DA90CC;
    constexpr uint64_t SetDescriptionRva = 0x1616C6C;
    constexpr uint64_t InitializeRva = 0x15B7C54;
    constexpr uint64_t AddSettingRva = 0x15B7BE8;
    constexpr uint64_t SetGetterRva = 0x107079C;
    constexpr uint64_t SetSetterRva = 0x1070800;
    constexpr uint64_t AddNumberOptionRva = 0x18AEDEC;
    constexpr uint64_t SetBoolDefaultRva = 0x1070634;
    constexpr uint64_t AssignOptionalStringRva = 0xF17BC8;
    constexpr uint32_t DiscreteDefaultValue = 0x178;

    constexpr uint64_t StringArrayCtorRva = 0x1617920;
    constexpr uint64_t StringArrayDtorRva = 0xB6EDBC;
    constexpr uint64_t MakeDataSourceRva = 0x15B9600;
    constexpr uint64_t DataSourceVTableRva = 0x84CB810;
    constexpr int DataSourceVTableSize = 6;

    constexpr int ShimFilterIndex = 0xA3;
    constexpr uint64_t AddToFilterRva = 0x1423E48;
    constexpr uint32_t ShimRegistry = 0x370;
    constexpr uint32_t MenuTabIds = 0x690;

    struct FBorrowedTab { uint32_t ClassOffset; uint32_t BrushOffset; const char* Name; };
    const FBorrowedTab BorrowedTabs[] = {
        { 0x558, 0x780, "Gameplay" },
        { 0x5A8, 0xAF0, "Audio" },
        { 0x560, 0x830, "HUD" },
        { 0x570, 0x990, "Keyboard and mouse" },
    };

    struct FAddTabsParams
    {
        UObj* WidgetClass;
        FText DisplayName;
        FName NameId;
        uint8_t Pad[8];
        uint8_t Icon[0xB0];
        uint8_t TabType;
        uint8_t Pad2[7];
    };
    static_assert(offsetof(FAddTabsParams, NameId) == 0x20 && offsetof(FAddTabsParams, Icon) == 0x30 && offsetof(FAddTabsParams, TabType) == 0xE0);

    struct FStringRange { FStr* Begin; FStr* End; };
    struct FSharedRef { void* Object; uint8_t* Controller; };

    enum ESetting { CustomFOV, FOV, Crosshair, Style, Size, Thickness, Gap, Outline, Color, BuildSens, EditSens, SettingCount };

    struct FColorPreset { const wchar_t* Name; float R, G, B; };
    const FColorPreset ColorPresets[] = {
        { L"White", 1.f, 1.f, 1.f },
        { L"Cyan", 0.f, 1.f, 0.9f },
        { L"Green", 0.2f, 1.f, 0.2f },
        { L"Yellow", 1.f, 1.f, 0.f },
        { L"Red", 1.f, 0.2f, 0.2f },
        { L"Pink", 1.f, 0.4f, 0.8f },
        { L"Purple", 0.7f, 0.4f, 1.f },
    };
    constexpr int ColorPresetCount = sizeof(ColorPresets) / sizeof(ColorPresets[0]);

    float Snap(float Value, float Min, float Max, float Step)
    {
        Value = Value < Min ? Min : Value > Max ? Max : Value;
        return Min + roundf((Value - Min) / Step) * Step;
    }

    int NearestColor()
    {
        auto& C = Tellurium::CustomSettings::Settings.CrosshairColor;
        int Best = 0;
        float BestDistance = 1e9f;
        for (int i = 0; i < ColorPresetCount; i++)
        {
            float D = fabsf(C[0] - ColorPresets[i].R) + fabsf(C[1] - ColorPresets[i].G) + fabsf(C[2] - ColorPresets[i].B);
            if (D < BestDistance)
                BestDistance = D, Best = i;
        }
        return Best;
    }

    bool IsBool(int Id) { return Id == CustomFOV || Id == Crosshair || Id == Outline; }

    void ValueString(int Id, wchar_t (&Out)[32])
    {
        auto& S = Tellurium::CustomSettings::Settings;
        if (IsBool(Id))
        {
            bool b = Id == CustomFOV ? S.bCustomFOV : Id == Crosshair ? S.bCrosshair : S.bCrosshairOutline;
            wcscpy_s(Out, b ? L"true" : L"false");
            return;
        }
        float Value = 0;
        switch (Id)
        {
        case FOV: Value = Snap(S.FOV, 60, 120, 1); break;
        case BuildSens: Value = Snap(S.BuildSensitivity, 10, 500, 5); break;
        case EditSens: Value = Snap(S.EditSensitivity, 10, 500, 5); break;
        case Style: Value = (float)S.CrosshairStyle; break;
        case Size: Value = Snap(S.CrosshairSize, 2, 30, 2); break;
        case Thickness: Value = Snap(S.CrosshairThickness, 1, 6, 1); break;
        case Gap: Value = Snap(S.CrosshairGap, 0, 12, 1); break;
        case Color: Value = (float)NearestColor(); break;
        }
        swprintf_s(Out, L"%f", (double)Value);
    }

    volatile ULONGLONG SaveAfter = 0;

    DWORD WINAPI SaveThread(LPVOID)
    {
        while (true)
        {
            Sleep(250);
            ULONGLONG Due = SaveAfter;
            if (Due && GetTickCount64() >= Due)
            {
                SaveAfter = 0;
                Tellurium::CustomSettings::Save();
            }
        }
        return 0;
    }

    void SetValueFromString(int Id, const wchar_t* Text)
    {
        auto& S = Tellurium::CustomSettings::Settings;
        wchar_t Before[32];
        ValueString(Id, Before);
        bool b = Text && (Text[0] == L't' || Text[0] == L'T');
        float Value = Text ? (float)_wtof(Text) : 0.f;
        switch (Id)
        {
        case CustomFOV: S.bCustomFOV = b; break;
        case Crosshair: S.bCrosshair = b; break;
        case Outline: S.bCrosshairOutline = b; break;
        case FOV: S.FOV = Snap(Value, 60, 120, 1); break;
        case BuildSens: S.BuildSensitivity = Snap(Value, 10, 500, 5); break;
        case EditSens: S.EditSensitivity = Snap(Value, 10, 500, 5); break;
        case Style: S.CrosshairStyle = (int)Snap(Value, 0, 3, 1); break;
        case Size: S.CrosshairSize = Snap(Value, 2, 30, 2); break;
        case Thickness: S.CrosshairThickness = Snap(Value, 1, 6, 1); break;
        case Gap: S.CrosshairGap = Snap(Value, 0, 12, 1); break;
        case Color:
        {
            auto& P = ColorPresets[(int)Snap(Value, 0, ColorPresetCount - 1, 1)];
            S.CrosshairColor[0] = P.R, S.CrosshairColor[1] = P.G, S.CrosshairColor[2] = P.B;
            break;
        }
        }
        wchar_t After[32];
        ValueString(Id, After);
        if (wcscmp(Before, After) == 0)
            return;
        InterlockedIncrement(&Tellurium::CustomSettings::SettingsVersion);
        SaveAfter = GetTickCount64() + 600;
    }

    void* DataSourceVTables[SettingCount][DataSourceVTableSize]{};

    bool __fastcall DataSourceResolve(void*, void*) { return true; }

    template <int Id>
    FStr* __fastcall DataSourceGet(void*, FStr* Out, void*)
    {
        wchar_t Text[32];
        ValueString(Id, Text);
        *Out = MakeString(Text);
        return Out;
    }

    template <int Id>
    void __fastcall DataSourceSet(void*, void*, FStr* Value)
    {
        SetValueFromString(Id, Value ? Value->Data : nullptr);
    }

    template <int Id>
    void FillVTable()
    {
        auto Original = At<void**>(DataSourceVTableRva);
        for (int i = 0; i < DataSourceVTableSize; i++)
            DataSourceVTables[Id][i] = Original[i];
        DataSourceVTables[Id][2] = (void*)&DataSourceResolve;
        DataSourceVTables[Id][3] = (void*)&DataSourceGet<Id>;
        DataSourceVTables[Id][4] = (void*)&DataSourceSet<Id>;
    }

    void FillVTables()
    {
        FillVTable<CustomFOV>(); FillVTable<FOV>(); FillVTable<Crosshair>(); FillVTable<Style>(); FillVTable<Size>();
        FillVTable<Thickness>(); FillVTable<Gap>(); FillVTable<Outline>(); FillVTable<Color>();
        FillVTable<BuildSens>(); FillVTable<EditSens>();
    }

    FSharedRef MakeDataSource(int Id)
    {
        FStr Segment{};
        At<void(*)(FStr*, const wchar_t*)>(FStringCtorRva)(&Segment, L"Refract");
        FStringRange Range{ &Segment, &Segment + 1 };
        uint8_t Array[0x10]{};
        At<void(*)(void*, FStringRange*)>(StringArrayCtorRva)(Array, &Range);
        auto Controller = At<uint8_t*(*)(void*)>(MakeDataSourceRva)(Array);
        At<void(*)(void*)>(StringArrayDtorRva)(Array);
        if (Segment.Data)
            Tellurium::Unreal::FMemory::Free(Segment.Data);
        if (!Controller)
            return {};

        auto Object = Controller + 16;
        if (!*(void**)(Object + 8))
        {
            *(void**)(Object + 8) = Object;
            *(void**)(Object + 16) = Controller;
            InterlockedIncrement((volatile long*)(Controller + 12));
        }
        *(void***)Object = DataSourceVTables[Id];
        return { Object, Controller };
    }

    UObj* CollectionClass = nullptr;
    UObj* BoolClass = nullptr;
    UObj* NumberClass = nullptr;
    UObj* OptionsMenuClass = nullptr;
    UObj* ShimClass = nullptr;
    UObj* AddTabsFunc = nullptr;
    UObj* TabSelectedFunc = nullptr;
    UObj* ActiveWidgetChangedFunc = nullptr;
    volatile bool bClassesReady = false;

    UObj* volatile ShownMenu = nullptr;
    volatile int32_t ShownMenuIndex = 0;
    volatile int32_t ShownMenuSerial = 0;
    volatile bool bRefractTabShown = false;
    constexpr uint32_t ActivatableIsActive = 0x298;
    SRWLOCK ClassLock = SRWLOCK_INIT;

    bool EnsureClasses()
    {
        if (bClassesReady)
            return true;
        AcquireSRWLockExclusive(&ClassLock);
        if (!bClassesReady)
        {
            if (!CollectionClass) CollectionClass = FindClass(L"FortSettingCollection");
            if (!BoolClass) BoolClass = FindClass(L"FortSettingValueDiscreteDynamic_Bool");
            if (!NumberClass) NumberClass = FindClass(L"FortSettingValueDiscreteDynamic_Number");
            if (!OptionsMenuClass) OptionsMenuClass = FindClass(L"FortOptionsMenu");
            if (!ShimClass) ShimClass = FindClass(L"FortOptionsTab_NewSystemShim");
            if (!AddTabsFunc) AddTabsFunc = FindFunction(L"AddTabs", L"FortOptionsMenu");
            if (!TabSelectedFunc) TabSelectedFunc = FindFunction(L"HandleTabSelected", L"FortOptionsMenu");
            if (!ActiveWidgetChangedFunc) ActiveWidgetChangedFunc = FindFunction(L"HandleActiveWidgetChanged", L"FortOptionsMenu");
            bClassesReady = CollectionClass && BoolClass && NumberClass && OptionsMenuClass && ShimClass && AddTabsFunc && FindEngineObjects();
            if (bClassesReady)
                Log("[Settings] found the settings classes");
        }
        ReleaseSRWLockExclusive(&ClassLock);
        return bClassesReady;
    }

    const char* volatile StepWhat = "";
    const wchar_t* volatile StepName = L"";
    void Mark(const char* What, const wchar_t* Name = L"") { StepWhat = What; StepName = Name ? Name : L""; }

    int LogCrash(EXCEPTION_POINTERS* E, const char* What)
    {
        auto Record = E->ExceptionRecord;
        auto Address = (uint64_t)Record->ExceptionAddress;
        HMODULE Module = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)Address, &Module);
        char Path[MAX_PATH] = "?";
        if (Module)
            GetModuleFileNameA(Module, Path, MAX_PATH);
        auto Slash = strrchr(Path, '\\');
        auto C = E->ContextRecord;
        Log("[Settings] %s crashed (0x%08X) at %s+0x%llX while %s %ls (address 0x%llX)", What, Record->ExceptionCode,
            Slash ? Slash + 1 : Path, Address - (uint64_t)Module, StepWhat, StepName,
            Record->NumberParameters > 1 ? (unsigned long long)Record->ExceptionInformation[1] : 0ull);
        Log("[Settings]   rcx=%llX rdx=%llX r8=%llX r9=%llX rax=%llX rbx=%llX", C->Rcx, C->Rdx, C->R8, C->R9, C->Rax, C->Rbx);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    void KeepAlive(UObj* Object)
    {
        auto Chunks = *(uint8_t***)(Base + 0xB3C9A78);
        auto Item = Chunks[Object->InternalIndex / 0x10000];
        if (Item)
            InterlockedOr((volatile long*)(Item + (Object->InternalIndex % 0x10000) * 24 + 8), 1 << 30);
    }

    UObj* NewSetting(UObj* Class)
    {
        uint64_t Params[64]{};
        Params[0] = (uint64_t)Class;
        Params[1] = (uint64_t)*At<UObj**>(TransientPackageRva);
        auto Object = At<UObj*(*)(void*)>(StaticConstructObjectRva)(Params);
        if (Object)
            KeepAlive(Object);
        return Object;
    }

    FText Text(const wchar_t* Value, const wchar_t* Key)
    {
        FText Out{};
        At<void(*)(FText*, const wchar_t*, const wchar_t*, const wchar_t*)>(MakeTextRva)(&Out, Value, L"Refract", Key);
        return Out;
    }

    void Name(UObj* Setting, const wchar_t* DevName, const wchar_t* DisplayName, const wchar_t* Description)
    {
        Mark("naming", DevName);
        FStr Dev{};
        At<void(*)(FStr*, const wchar_t*)>(FStringCtorRva)(&Dev, DevName);
        At<void(*)(UObj*, FStr*)>(SetDevNameRva)(Setting, &Dev);
        if (Dev.Data)
            Tellurium::Unreal::FMemory::Free(Dev.Data);

        wchar_t Key[96];
        swprintf_s(Key, L"%s_Name", DevName);
        auto Display = Text(DisplayName, _wcsdup(Key));
        At<void(*)(UObj*, FText*)>(SetDisplayNameRva)(Setting, &Display);
        if (Description)
        {
            swprintf_s(Key, L"%s_Description", DevName);
            auto Desc = Text(Description, _wcsdup(Key));
            At<void(*)(UObj*, FText*)>(SetDescriptionRva)(Setting, &Desc);
        }
    }

    void Bind(UObj* Setting, int Id)
    {
        Mark("making the data source", nullptr);
        auto Source = MakeDataSource(Id);
        if (!Source.Object)
            return;
        Mark("setting the getter and setter", nullptr);
        At<void(*)(UObj*, FSharedRef*)>(SetGetterRva)(Setting, &Source);
        At<void(*)(UObj*, FSharedRef*)>(SetSetterRva)(Setting, &Source);
    }

    UObj* NewCollection(const wchar_t* DevName, const wchar_t* DisplayName)
    {
        Mark("creating collection", DevName);
        auto Collection = NewSetting(CollectionClass);
        if (Collection)
            Name(Collection, DevName, DisplayName, nullptr);
        return Collection;
    }

    void AddBool(UObj* Parent, int Id, const wchar_t* DevName, const wchar_t* DisplayName, const wchar_t* Description, bool bDefault)
    {
        Mark("creating bool", DevName);
        auto Setting = NewSetting(BoolClass);
        if (!Setting)
            return;
        Name(Setting, DevName, DisplayName, Description);
        Bind(Setting, Id);
        Mark("setting the default of", DevName);
        At<void(*)(UObj*, bool)>(SetBoolDefaultRva)(Setting, bDefault);
        Mark("adding and initializing", DevName);
        At<void(*)(UObj*, UObj*)>(AddSettingRva)(Parent, Setting);
    }

    void AddNumber(UObj* Parent, int Id, const wchar_t* DevName, const wchar_t* DisplayName, const wchar_t* Description,
        float Min, float Max, float Step, float Default, const wchar_t* const* Labels = nullptr)
    {
        Mark("creating number", DevName);
        auto Setting = NewSetting(NumberClass);
        if (!Setting)
            return;
        Name(Setting, DevName, DisplayName, Description);
        Bind(Setting, Id);
        Mark("adding the options of", DevName);
        int Count = (int)roundf((Max - Min) / Step) + 1;
        for (int Index = 0; Index < Count; Index++)
        {
            float Value = Min + Index * Step;
            wchar_t Label[32], Key[96];
            if (Labels)
                wcscpy_s(Label, Labels[Index]);
            else
                swprintf_s(Label, L"%g", Value);
            swprintf_s(Key, L"%s_Option%d", DevName, Index);
            auto OptionText = Text(_wcsdup(Label), _wcsdup(Key));
            At<void(*)(UObj*, float, FText*)>(AddNumberOptionRva)(Setting, Value, &OptionText);
        }
        Mark("setting the default of", DevName);
        wchar_t DefaultText[32];
        swprintf_s(DefaultText, L"%f", (double)Default);
        FStr DefaultValue{};
        At<void(*)(FStr*, const wchar_t*)>(FStringCtorRva)(&DefaultValue, DefaultText);
        At<void(*)(void*, FStr*)>(AssignOptionalStringRva)((uint8_t*)Setting + DiscreteDefaultValue, &DefaultValue);
        if (DefaultValue.Data)
            Tellurium::Unreal::FMemory::Free(DefaultValue.Data);
        Mark("adding and initializing", DevName);
        At<void(*)(UObj*, UObj*)>(AddSettingRva)(Parent, Setting);
    }

    constexpr uint64_t NewScalarRva = 0x17769F0;
    constexpr uint64_t ScalarSetDefaultRva = 0x1776A90;
    constexpr uint64_t ScalarSetDisplayFormatRva = 0x1776AE4;
    constexpr uint64_t ScalarSetRangeAndStepRva = 0x1774A78;
    constexpr uint64_t CopyFormatRva = 0xE389F0;
    constexpr uint64_t GameScalarFormatRva = 0xB0FF0B0;

    struct FDoubleRange { uint8_t LowerType; double Lower; uint8_t UpperType; double Upper; };

    FText NumberTexts[201]{};
    FText PercentTexts[99]{};

    void MakeScalarTexts()
    {
        if (NumberTexts[0].Data)
            return;
        for (int i = 0; i <= 200; i++)
        {
            wchar_t Label[8], Key[32];
            swprintf_s(Label, L"%d", i);
            swprintf_s(Key, L"RefractNumber%d", i);
            NumberTexts[i] = Text(_wcsdup(Label), _wcsdup(Key));
        }
        for (int i = 0; i < 99; i++)
        {
            wchar_t Label[8], Key[32];
            swprintf_s(Label, L"%d%%", 10 + i * 5);
            swprintf_s(Key, L"RefractPercent%d", 10 + i * 5);
            PercentTexts[i] = Text(_wcsdup(Label), _wcsdup(Key));
        }
    }

    FText* ShareText(FText* Out, const FText& From)
    {
        *Out = From;
        if (Out->Controller)
            InterlockedIncrement((volatile long*)((uint8_t*)Out->Controller + 8));
        return Out;
    }

    FText* __fastcall FormatWholeNumber(FText* Out, void*, double* Source, double*)
    {
        int Value = (int)lround(Source ? *Source : 0.0);
        Value = Value < 0 ? 0 : Value > 200 ? 200 : Value;
        return ShareText(Out, NumberTexts[Value]);
    }

    FText* __fastcall FormatPercent(FText* Out, void*, double* Source, double*)
    {
        int Index = (int)lround(((Source ? *Source : 100.0) - 10.0) / 5.0);
        Index = Index < 0 ? 0 : Index > 98 ? 98 : Index;
        return ShareText(Out, PercentTexts[Index]);
    }

    bool AddScalar(UObj* Parent, int Id, const wchar_t* DevName, const wchar_t* DisplayName, const wchar_t* Description,
        double Min, double Max, double Step, double Default, bool bPercent)
    {
        MakeScalarTexts();
        Mark("creating slider", DevName);
        auto Setting = At<UObj*(*)(UObj*)>(NewScalarRva)(*At<UObj**>(TransientPackageRva));
        if (!Setting)
            return false;
        KeepAlive(Setting);
        Name(Setting, DevName, DisplayName, Description);
        Bind(Setting, Id);
        Mark("setting the default of", DevName);
        At<void(*)(UObj*, double)>(ScalarSetDefaultRva)(Setting, Default);
        Mark("setting the format of", DevName);
        alignas(16) uint8_t Format[0x40]{};
        At<void(*)(void*, void*)>(CopyFormatRva)(Format, At<void*>(GameScalarFormatRva));
        *(void**)Format = bPercent ? (void*)&FormatPercent : (void*)&FormatWholeNumber;
        At<void(*)(UObj*, void*)>(ScalarSetDisplayFormatRva)(Setting, Format);
        Mark("setting the range of", DevName);
        FDoubleRange Range{ 1, Min, 1, Max };
        At<void(*)(UObj*, FDoubleRange*, double)>(ScalarSetRangeAndStepRva)(Setting, &Range, Step);
        Mark("adding and initializing", DevName);
        At<void(*)(UObj*, UObj*)>(AddSettingRva)(Parent, Setting);
        return true;
    }

    UObj* FindRefractCollection(UObj* Registry)
    {
        FStr Dev{};
        At<void(*)(FStr*, const wchar_t*)>(FStringCtorRva)(&Dev, L"RefractCollection");
        auto Found = At<UObj*(*)(UObj*, FStr*)>(FindSettingByDevNameRva)(Registry, &Dev);
        if (Dev.Data)
            Tellurium::Unreal::FMemory::Free(Dev.Data);
        return Found;
    }

    void BuildCollection(UObj* Registry, UObj* LocalPlayer)
    {
        Mark("looking for", L"RefractCollection");
        if (!EnsureClasses() || FindRefractCollection(Registry))
            return;

        auto Top = NewCollection(L"RefractCollection", L"Refract");
        if (!Top)
            return;
        Mark("initializing", L"RefractCollection");
        Mark("initializing", L"RefractCollection");
        At<void(*)(UObj*, UObj*)>(InitializeRva)(Top, LocalPlayer);

        auto Camera = NewCollection(L"RefractCameraCollection", L"Camera");
        At<void(*)(UObj*, UObj*)>(AddSettingRva)(Top, Camera);
        AddBool(Camera, CustomFOV, L"RefractCustomFOV", L"Custom FOV",
            L"Use your own field of view instead of the game's. Aiming down sights still zooms in.", false);
        static const wchar_t* FovDescription = L"How wide the camera sees when Custom FOV is on. The game's normal view is 80.";
        if (!AddScalar(Camera, FOV, L"RefractFOV", L"Field of View", FovDescription, 60, 120, 1, 90, false))
            AddNumber(Camera, FOV, L"RefractFOV", L"Field of View", FovDescription, 60, 120, 5, 90);

        auto Building = NewCollection(L"RefractBuildingCollection", L"Building");
        At<void(*)(UObj*, UObj*)>(AddSettingRva)(Top, Building);
        static wchar_t PercentText[99][8];
        static const wchar_t* Percents[99];
        for (int i = 0; i < 99; i++)
            swprintf_s(PercentText[i], L"%d%%", 10 + i * 5), Percents[i] = PercentText[i];
        static const wchar_t* BuildDescription = L"Mouse look speed while in build mode, as a percentage of your normal sensitivity. 100% is no change.";
        static const wchar_t* EditDescription = L"Mouse look speed while editing a build, as a percentage of your normal sensitivity. 100% is no change.";
        if (!AddScalar(Building, BuildSens, L"RefractBuildSensitivity", L"BUILDING SENSITIVITY", BuildDescription, 10, 500, 5, 100, true))
            AddNumber(Building, BuildSens, L"RefractBuildSensitivity", L"BUILDING SENSITIVITY", BuildDescription, 10, 500, 5, 100, Percents);
        if (!AddScalar(Building, EditSens, L"RefractEditSensitivity", L"EDITING SENSITIVITY", EditDescription, 10, 500, 5, 100, true))
            AddNumber(Building, EditSens, L"RefractEditSensitivity", L"EDITING SENSITIVITY", EditDescription, 10, 500, 5, 100, Percents);

        auto Cross = NewCollection(L"RefractCrosshairCollection", L"Crosshair");
        At<void(*)(UObj*, UObj*)>(AddSettingRva)(Top, Cross);
        AddBool(Cross, Crosshair, L"RefractCrosshair", L"Custom Crosshair",
            L"Draws your own crosshair in the middle of the screen during matches.", false);
        static const wchar_t* Styles[] = { L"Cross", L"Dot", L"Circle", L"Cross and Dot" };
        AddNumber(Cross, Style, L"RefractCrosshairStyle", L"Style", nullptr, 0, 3, 1, 0, Styles);
        AddNumber(Cross, Size, L"RefractCrosshairSize", L"Size", nullptr, 2, 30, 2, 8);
        AddNumber(Cross, Thickness, L"RefractCrosshairThickness", L"Thickness", nullptr, 1, 6, 1, 2);
        AddNumber(Cross, Gap, L"RefractCrosshairGap", L"Gap", L"Space between the middle and the lines.", 0, 12, 1, 3);
        AddBool(Cross, Outline, L"RefractCrosshairOutline", L"Outline", L"A dark edge so the crosshair shows on bright backgrounds.", true);
        static const wchar_t* Colors[ColorPresetCount];
        for (int i = 0; i < ColorPresetCount; i++)
            Colors[i] = ColorPresets[i].Name;
        AddNumber(Cross, Color, L"RefractCrosshairColor", L"Color", nullptr, 0, ColorPresetCount - 1, 1, 1, Colors);

        Mark("registering", L"RefractCollection");
        At<void(*)(UObj*, UObj*)>(RegisterSettingRva)(Registry, Top);
        Log("[Settings] added the Refract settings to a registry");
    }

    using OnInitializeFn = void(__fastcall*)(UObj*, UObj*);
    OnInitializeFn OriginalOnInitialize = nullptr;

    volatile bool bBuildFailed = false;

    void SafeBuild(UObj* Registry, UObj* LocalPlayer)
    {
        if (bBuildFailed)
            return;
        Log("[Settings] building the Refract settings (registry %p, local player %p)", Registry, LocalPlayer);
        __try { BuildCollection(Registry, LocalPlayer); }
        __except (LogCrash(GetExceptionInformation(), "building the Refract settings")) { bBuildFailed = true; }
    }

    void __fastcall OnInitializeHook(UObj* Registry, UObj* LocalPlayer)
    {
        OriginalOnInitialize(Registry, LocalPlayer);
        SafeBuild(Registry, LocalPlayer);
    }

    using FilterFn = void(__fastcall*)(UObj*, void*);
    FilterFn OriginalFilter = nullptr;
    void** PatchedShimVTable = nullptr;
    volatile bool bAddingTab = false;
    FName TabName{};
    UObj* BorrowedTabClass = nullptr;

    constexpr uint32_t TabWidgetType = 0x360;
    constexpr uint8_t OurTabType = 0;

    bool IsOurWidget(UObj* Tab)
    {
        return bAddingTab || (Tab->Class == BorrowedTabClass && *((uint8_t*)Tab + TabWidgetType) == OurTabType);
    }

    void __fastcall FilterHook(UObj* Tab, void* FilterState)
    {
        if (!IsOurWidget(Tab))
            return OriginalFilter(Tab, FilterState);

        __try
        {
            auto Registry = *(UObj**)((uint8_t*)Tab + ShimRegistry);
            auto Collection = Registry ? FindRefractCollection(Registry) : nullptr;
            if (!Collection && Registry)
            {
                SafeBuild(Registry, LocalPlayer());
                Collection = FindRefractCollection(Registry);
            }
            if (Collection)
                At<void(*)(void*, UObj*)>(AddToFilterRva)(FilterState, Collection);
            else
                Log("[Settings] the Refract tab opened but its settings weren't in the registry");
        }
        __except (LogCrash(GetExceptionInformation(), "showing the Refract tab")) {}
    }

    void PatchShim(UObj* TabClass)
    {
        auto Default = *(UObj**)((uint8_t*)TabClass + ClassDefaultObject);
        if (!Default || PatchedShimVTable)
            return;
        void** VTable = Default->VTable;
        if (VTable[ShimFilterIndex] == (void*)&FilterHook)
            return;
        OriginalFilter = (FilterFn)VTable[ShimFilterIndex];
        DWORD OldProtect;
        VirtualProtect(&VTable[ShimFilterIndex], sizeof(void*), PAGE_READWRITE, &OldProtect);
        VTable[ShimFilterIndex] = (void*)&FilterHook;
        VirtualProtect(&VTable[ShimFilterIndex], sizeof(void*), OldProtect, &OldProtect);
        PatchedShimVTable = VTable;
    }

    struct FTArray { void* Data; int32_t Num; int32_t Max; };

    UObj* IconTexture = nullptr;
    bool bIconFailed = false;

    bool WriteIconFile(wchar_t (&Path)[MAX_PATH])
    {
        wchar_t* Local = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &Local)))
            return false;
        swprintf_s(Path, L"%s\\Refract", Local);
        CoTaskMemFree(Local);
        CreateDirectoryW(Path, nullptr);
        wcscat_s(Path, L"\\tab-icon.png");
        HANDLE File = CreateFileW(Path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (File == INVALID_HANDLE_VALUE)
            return GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES;
        DWORD Written = 0;
        WriteFile(File, TabIconPng, sizeof(TabIconPng), &Written, nullptr);
        CloseHandle(File);
        return Written == sizeof(TabIconPng);
    }

    UObj* LoadIconTexture(UObj* WorldContext)
    {
        if (IconTexture || bIconFailed)
            return IconTexture;
        bIconFailed = true;
        auto Import = FindFunction(L"ImportFileAsTexture2D", L"KismetRenderingLibrary");
        auto Library = FindObject(L"Default__KismetRenderingLibrary");
        wchar_t Path[MAX_PATH];
        if (!Import || !Library || !WriteIconFile(Path))
        {
            Log("[Settings] couldn't load the Refract icon (import %p, library %p)", Import, Library);
            return nullptr;
        }
        struct { UObj* WorldContextObject; FStr Filename; UObj* ReturnValue; } Params{};
        Params.WorldContextObject = WorldContext;
        Params.Filename = MakeString(Path);
        ProcessEvent(Library, Import, &Params);
        if (Params.Filename.Data)
            Tellurium::Unreal::FMemory::Free(Params.Filename.Data);
        if (!Params.ReturnValue)
        {
            Log("[Settings] the game couldn't read the Refract icon");
            return nullptr;
        }
        KeepAlive(Params.ReturnValue);
        IconTexture = Params.ReturnValue;
        bIconFailed = false;
        return IconTexture;
    }

    int32_t PropertyOffset(const wchar_t* StructName, const wchar_t* PropertyName)
    {
        __try
        {
            auto Struct = FindObject(StructName);
            for (auto Field = Struct ? *(uint8_t**)((uint8_t*)Struct + 0x50) : nullptr; Field; Field = *(uint8_t**)(Field + 0x20))
                if (NameIs(*(FName*)(Field + 0x28), PropertyName))
                    return *(int32_t*)(Field + 0x4C);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return -1;
    }

    bool MakeIconBrush(UObj* Menu, const uint8_t* Borrowed, uint8_t (&Icon)[0xB0])
    {
        auto Texture = LoadIconTexture(Menu);
        static UObj* MakeBrush = FindFunction(L"MakeBrushFromTexture", L"WidgetBlueprintLibrary");
        static UObj* Library = FindObject(L"Default__WidgetBlueprintLibrary");
        if (!Texture || !MakeBrush || !Library)
            return false;

        static int32_t SizeOffset = PropertyOffset(L"SlateBrush", L"ImageSize");
        int32_t Width = 48, Height = 48;
        if (SizeOffset > 0 && SizeOffset + 8 <= 0xB0)
        {
            float W = *(float*)(Borrowed + SizeOffset), H = *(float*)(Borrowed + SizeOffset + 4);
            if (W >= 8 && W <= 512 && H >= 8 && H <= 512)
                Width = (int32_t)W, Height = (int32_t)H;
        }

        struct FParams { UObj* Texture; int32_t Width; int32_t Height; uint8_t ReturnValue[0xB0]; };
        static_assert(offsetof(FParams, ReturnValue) == 0x10);
        FParams Params{};
        Params.Texture = Texture;
        Params.Width = Width;
        Params.Height = Height;
        *(void**)Params.ReturnValue = *(void* const*)Borrowed;
        ProcessEvent(Library, MakeBrush, &Params);
        memcpy(Icon, Params.ReturnValue, sizeof(Icon));
        Log("[Settings] made the Refract tab icon (%dx%d, size offset %d)", Width, Height, SizeOffset);
        return true;
    }

    bool SafeMakeIconBrush(UObj* Menu, const uint8_t* Borrowed, uint8_t (&Icon)[0xB0])
    {
        __try { return MakeIconBrush(Menu, Borrowed, Icon); }
        __except (LogCrash(GetExceptionInformation(), "making the Refract tab icon")) { bIconFailed = true; return false; }
    }

    bool HasOurTab(UObj* Menu)
    {
        auto& Ids = *(FTArray*)((uint8_t*)Menu + MenuTabIds);
        for (int32_t i = 0; i < Ids.Num; i++)
            if (((FName*)Ids.Data)[i].Index == TabName.Index && ((FName*)Ids.Data)[i].Number == TabName.Number)
                return true;
        return false;
    }

    UObj* HandledMenu = nullptr;
    UObj* FailedMenu = nullptr;
    int32_t FailedMenuSerial = 0;

    int32_t SerialAt(int32_t Index)
    {
        auto Chunks = *(uint8_t***)(Base + 0xB3C9A78);
        auto Item = Chunks[Index / 0x10000];
        return Item ? *(int32_t*)(Item + (Index % 0x10000) * 24 + 0x10) : 0;
    }

    void AddTab(UObj* Menu)
    {
        auto& Ids = *(FTArray*)((uint8_t*)Menu + MenuTabIds);
        if (HasOurTab(Menu))
        {
            HandledMenu = Menu;
            return;
        }
        if (Ids.Num == 0)
            return;

        const FBorrowedTab* Borrowed = nullptr;
        UObj* TabClass = nullptr;
        for (auto& Candidate : BorrowedTabs)
        {
            auto Class = *(UObj**)((uint8_t*)Menu + Candidate.ClassOffset);
            for (auto Super = Class; Super; Super = *(UObj**)((uint8_t*)Super + StructSuper))
                if (Super == ShimClass)
                {
                    Borrowed = &Candidate, TabClass = Class;
                    break;
                }
            if (Borrowed)
                break;
        }
        if (!Borrowed)
        {
            Log("[Settings] no settings tab to borrow, can't add the Refract tab");
            HandledMenu = FailedMenu = Menu;
            return;
        }
        PatchShim(TabClass);
        BorrowedTabClass = TabClass;

        FAddTabsParams Params{};
        Params.WidgetClass = TabClass;
        Params.DisplayName = Text(L"Refract", L"RefractTab_Name");
        Params.NameId = TabName;
        if (!SafeMakeIconBrush(Menu, (uint8_t*)Menu + Borrowed->BrushOffset, Params.Icon))
            memcpy(Params.Icon, (uint8_t*)Menu + Borrowed->BrushOffset, sizeof(Params.Icon));
        Params.TabType = OurTabType;

        bAddingTab = true;
        ProcessEvent(Menu, AddTabsFunc, &Params);
        bAddingTab = false;

        HandledMenu = Menu;
        Log("[Settings] added the Refract tab (borrowing the %s tab's layout)", Borrowed->Name);
    }

    void SafeAddTab(UObj* Menu)
    {
        __try { AddTab(Menu); }
        __except (LogCrash(GetExceptionInformation(), "adding the Refract tab"))
        {
            bAddingTab = false;
            HandledMenu = FailedMenu = Menu;
        }
    }

    using ProcessEventFn = void(__fastcall*)(UObj*, UObj*, void*);
    ProcessEventFn OriginalMenuProcessEvent = nullptr;
    bool bInMenuHook = false;

    void __fastcall MenuProcessEventHook(UObj* Object, UObj* Function, void* Params)
    {
        OriginalMenuProcessEvent(Object, Function, Params);
        if (bAddingTab || bInMenuHook || !IsA(Object, OptionsMenuClass))
            return;

        if (Params && Function && (Function == TabSelectedFunc || Function == ActiveWidgetChangedFunc))
        {
            bool bOurs = false;
            if (Function == TabSelectedFunc)
            {
                auto Id = *(FName*)Params;
                bOurs = TabName.Index != 0 && Id.Index == TabName.Index && Id.Number == TabName.Number;
            }
            else
            {
                auto Widget = *(UObj**)Params;
                bOurs = Widget && BorrowedTabClass && Widget->Class == BorrowedTabClass && *((uint8_t*)Widget + TabWidgetType) == OurTabType;
            }
            ShownMenuIndex = Object->InternalIndex;
            ShownMenuSerial = SerialAt(Object->InternalIndex);
            ShownMenu = Object;
            bRefractTabShown = bOurs;
        }
        if (Object == FailedMenu && SerialAt(Object->InternalIndex) == FailedMenuSerial)
            return;

        bInMenuHook = true;
        if (TabName.Index == 0)
            TabName = MakeName(L"RefractTab");
        SafeAddTab(Object);
        if (FailedMenu == Object)
            FailedMenuSerial = SerialAt(Object->InternalIndex);
        bInMenuHook = false;
    }

    DWORD WINAPI SettingsThread(LPVOID)
    {
        while (!EnsureClasses())
            Sleep(1000);

        UObj* Default = nullptr;
        for (int Try = 0; Try < 120 && !(Default = *(UObj**)((uint8_t*)OptionsMenuClass + ClassDefaultObject)); Try++)
            Sleep(1000);
        if (!Default)
        {
            Log("[Settings] the settings menu has no default object, no Refract tab");
            return 0;
        }
        void** VTable = Default->VTable;
        OriginalMenuProcessEvent = (ProcessEventFn)VTable[ProcessEventIndex];
        DWORD OldProtect;
        VirtualProtect(&VTable[ProcessEventIndex], sizeof(void*), PAGE_READWRITE, &OldProtect);
        VTable[ProcessEventIndex] = (void*)&MenuProcessEventHook;
        VirtualProtect(&VTable[ProcessEventIndex], sizeof(void*), OldProtect, &OldProtect);
        Log("[Settings] watching the settings menu");
        return 0;
    }
}

bool Tellurium::SettingsTab::IsRefractTabShown()
{
    if (!bRefractTabShown)
        return false;
    __try
    {
        auto Menu = ShownMenu;
        if (!Menu || ObjectAt(ShownMenuIndex) != Menu || SerialAt(ShownMenuIndex) != ShownMenuSerial)
            return false;
        return *((uint8_t*)Menu + ActivatableIsActive) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Tellurium::SettingsTab::Start()
{
    if (!Engine1910::IsSupported())
        return;
    Tellurium::CustomSettings::Load();
    FillVTables();
    CreateThread(0, 0, SaveThread, 0, 0, 0);

    auto VTable = At<void**>(RegistryVTableRva);
    if (VTable[OnInitializeIndex] != (void*)(Base + 0x1773A64))
    {
        Log("[Settings] the settings registry isn't where it should be, no Refract tab");
        return;
    }
    OriginalOnInitialize = (OnInitializeFn)VTable[OnInitializeIndex];
    DWORD OldProtect;
    VirtualProtect(&VTable[OnInitializeIndex], sizeof(void*), PAGE_READWRITE, &OldProtect);
    VTable[OnInitializeIndex] = (void*)&OnInitializeHook;
    VirtualProtect(&VTable[OnInitializeIndex], sizeof(void*), OldProtect, &OldProtect);

    CreateThread(0, 0, SettingsThread, 0, 0, 0);
}
