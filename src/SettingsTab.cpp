#include "pch.h"
#include "SettingsTab.h"
#include "Engine1241.h"
#include "CustomSettings.h"
#include "Unreal.h"
#include "TabIcon.h"
#include <shlobj.h>
#include <cstdio>
#include <cmath>
#include <cwchar>

using namespace Engine1241;

namespace
{
    constexpr int OnInitializeIndex = 616 / 8;
    constexpr uint64_t OnInitializeRva = 0x4F70B80;
    constexpr uint64_t RegisterSettingRva = 0x4FB0560;
    constexpr uint64_t FindSettingByDevNameRva = 0x4F93490;
    constexpr uint64_t GetTransientPackageRva = 0x2DC47B0;

    constexpr uint64_t StaticConstructObjectRva = 0x2E1B370;
    constexpr uint64_t MakeTextRva = 0x2B314D0;
    constexpr uint64_t AssignTextRva = 0x774120;
    constexpr uint64_t InitializeRva = 0x4F30690;
    constexpr uint64_t AddSettingRva = 0x4F227F0;
    constexpr uint64_t SetGetterRva = 0x4F73EA0;
    constexpr uint64_t SetSetterRva = 0x4F73F40;
    constexpr uint64_t AddOptionRva = 0x4F22320;
    constexpr uint64_t SetBoolDefaultRva = 0x4F735B0;
    constexpr uint64_t SetDefaultStringRva = 0x4F736E0;
    constexpr uint32_t SettingDevName = 0x68;
    constexpr uint32_t SettingDisplayName = 0x78;
    constexpr uint32_t SettingDescription = 0x90;
    constexpr uint32_t SettingHasDescription = 0xC8;

    constexpr uint64_t StringArrayCtorRva = 0xAF75F0;
    constexpr uint64_t MakeDataSourceRva = 0x4D83170;
    constexpr uint64_t DataSourceVTableRva = 0x6C8F1D8;
    constexpr int DataSourceVTableSize = 6;

    constexpr int ShimFilterIndex = 0x9A;
    constexpr int ShimCheckIndex = 0x99;
    constexpr uint64_t ShimCheckRva = 0x4DB2160;
    constexpr uint64_t AddToFilterRva = 0x4F22870;
    constexpr uint32_t ShimRegistry = 0x318;
    constexpr uint32_t MenuTabList = 0x4A0;
    constexpr uint32_t MenuSwitcher = 0x4A8;
    constexpr uint32_t TabListPreregistered = 0x350;
    constexpr uint32_t TabInfoSize = 0xC8;
    constexpr uint32_t TabInfoLabel = 0x10;
    constexpr uint32_t TabInfoButtonType = 0xB0;
    constexpr uint32_t TabInfoCreatedContent = 0xC0;
    constexpr uint32_t LabelInfoSize = 0xA0;
    constexpr uint32_t LabelIcon = 0x18;
    constexpr uint32_t BrushSize = 0x88;
    constexpr uint32_t ActivatableIsActive = 0x288;
    constexpr uint32_t PanelSlots = 0x108;
    constexpr uint32_t SwitcherActiveIndex = 0x120;
    constexpr uint32_t SlotContent = 0x30;

    struct FTArray { void* Data; int32_t Num; int32_t Max; };
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
        FStr Segment = MakeString(L"Shatter");
        FStringRange Range{ &Segment, &Segment + 1 };
        FTArray Array{};
        At<void*(*)(FTArray*, FStringRange*)>(StringArrayCtorRva)(&Array, &Range);
        auto Controller = At<uint8_t*(*)(FTArray*)>(MakeDataSourceRva)(&Array);
        for (int32_t i = 0; i < Array.Num; i++)
            if (((FStr*)Array.Data)[i].Data)
                Tellurium::Unreal::FMemory::Free(((FStr*)Array.Data)[i].Data);
        if (Array.Data)
            Tellurium::Unreal::FMemory::Free(Array.Data);
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
    UObj* RegistryClass = nullptr;
    UObj* OptionsMenuClass = nullptr;
    UObj* ShimClass = nullptr;
    UObj* CreateWidgetFunc = nullptr;
    UObj* WidgetLibrary = nullptr;
    UObj* AddChildFunc = nullptr;
    UObj* HasChildFunc = nullptr;
    UObj* RegisterFortTabFunc = nullptr;
    UObj* GetTabButtonFunc = nullptr;
    UObj* ScalarClass = nullptr;
    UObj* SetBrushFunc = nullptr;
    UObj* ImageSetBrushFunc = nullptr;
    UObj* BorderClass = nullptr;
    UObj* ImageClass = nullptr;
    UObj* PanelClass = nullptr;
    UObj* UserWidgetClass = nullptr;
    volatile bool bClassesReady = false;
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
            if (!RegistryClass) RegistryClass = FindClass(L"FortGameSettingRegistry");
            if (!OptionsMenuClass) OptionsMenuClass = FindClass(L"FortOptionsMenu");
            if (!ShimClass) ShimClass = FindClass(L"FortOptionsTab_NewSystemShim");
            if (!CreateWidgetFunc) CreateWidgetFunc = FindFunction(L"Create", L"WidgetBlueprintLibrary");
            if (!WidgetLibrary) WidgetLibrary = FindObject(L"Default__WidgetBlueprintLibrary");
            if (!AddChildFunc) AddChildFunc = FindFunction(L"AddChild", L"PanelWidget");
            if (!HasChildFunc) HasChildFunc = FindFunction(L"HasChild", L"PanelWidget");
            if (!RegisterFortTabFunc) RegisterFortTabFunc = FindFunction(L"RegisterFortTab", L"FortTabListWidgetBase");
            if (!GetTabButtonFunc) GetTabButtonFunc = FindFunction(L"GetTabButtonByID", L"CommonTabListWidget");
            if (!ScalarClass) ScalarClass = FindClass(L"FortSettingValueScalarDynamic");
            if (!SetBrushFunc) SetBrushFunc = FindFunction(L"SetBrush", L"Border");
            if (!ImageSetBrushFunc) ImageSetBrushFunc = FindFunction(L"SetBrush", L"Image");
            if (!BorderClass) BorderClass = FindClass(L"Border");
            if (!ImageClass) ImageClass = FindClass(L"Image");
            if (!PanelClass) PanelClass = FindClass(L"PanelWidget");
            if (!UserWidgetClass) UserWidgetClass = FindClass(L"UserWidget");
            bClassesReady = CollectionClass && BoolClass && NumberClass && RegistryClass && OptionsMenuClass && ShimClass &&
                CreateWidgetFunc && WidgetLibrary && AddChildFunc && HasChildFunc &&
                RegisterFortTabFunc && GetTabButtonFunc && FindEngineObjects();
            static int Tries = 0;
            if (bClassesReady)
                Log("[Settings] found the settings classes (12.41)");
            else if (++Tries == 60)
                Log("[Settings] still looking for: %s%s%s%s%s%s%s%s%s%s%s%s",
                    CollectionClass ? "" : "collection ", BoolClass ? "" : "bool ", NumberClass ? "" : "number ",
                    RegistryClass ? "" : "registry ", OptionsMenuClass ? "" : "menu ", ShimClass ? "" : "shim ",
                    CreateWidgetFunc ? "" : "create ", WidgetLibrary ? "" : "widgetlibrary ", AddChildFunc ? "" : "addchild ",
                    HasChildFunc ? "" : "haschild ", RegisterFortTabFunc ? "" : "registertab ", GetTabButtonFunc ? "" : "gettabbutton ");
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

    UObj* NewSetting(UObj* Class)
    {
        using StaticConstructObjectFn = UObj*(*)(UObj*, UObj*, FName, uint32_t, uint32_t, UObj*, bool, void*, bool);
        auto Outer = At<UObj*(*)()>(GetTransientPackageRva)();
        auto Object = At<StaticConstructObjectFn>(StaticConstructObjectRva)(Class, Outer, FName{}, 0, 0, nullptr, false, nullptr, false);
        if (Object)
            KeepAlive(Object);
        return Object;
    }

    FText Text(const wchar_t* Value, const wchar_t* Key)
    {
        FText Out{};
        At<void(*)(FText*, const wchar_t*, const wchar_t*, const wchar_t*)>(MakeTextRva)(&Out, Value, L"Shatter", Key);
        return Out;
    }

    void Name(UObj* Setting, const wchar_t* DevName, const wchar_t* DisplayName, const wchar_t* Description)
    {
        Mark("naming", DevName);
        auto& Dev = *(FStr*)((uint8_t*)Setting + SettingDevName);
        if (Dev.Data)
            Tellurium::Unreal::FMemory::Free(Dev.Data);
        Dev = MakeString(DevName);

        wchar_t Key[96];
        swprintf_s(Key, L"%s_Name", DevName);
        auto Display = Text(DisplayName, _wcsdup(Key));
        At<void(*)(void*, FText*)>(AssignTextRva)((uint8_t*)Setting + SettingDisplayName, &Display);
        if (Description)
        {
            swprintf_s(Key, L"%s_Description", DevName);
            auto Desc = Text(Description, _wcsdup(Key));
            At<void(*)(void*, FText*)>(AssignTextRva)((uint8_t*)Setting + SettingDescription, &Desc);
            *((uint8_t*)Setting + SettingHasDescription) = 1;
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
            wchar_t Label[32], Key[96], ValueText[32];
            if (Labels)
                wcscpy_s(Label, Labels[Index]);
            else
                swprintf_s(Label, L"%g", Value);
            swprintf_s(Key, L"%s_Option%d", DevName, Index);
            swprintf_s(ValueText, L"%f", (double)Value);
            auto OptionValue = MakeString(ValueText);
            auto OptionText = Text(_wcsdup(Label), _wcsdup(Key));
            At<void(*)(UObj*, FStr*, FText*)>(AddOptionRva)(Setting, &OptionValue, &OptionText);
        }
        Mark("setting the default of", DevName);
        wchar_t DefaultText[32];
        swprintf_s(DefaultText, L"%f", (double)Default);
        auto DefaultValue = MakeString(DefaultText);
        At<void(*)(UObj*, FStr*)>(SetDefaultStringRva)(Setting, &DefaultValue);
        Mark("adding and initializing", DevName);
        At<void(*)(UObj*, UObj*)>(AddSettingRva)(Parent, Setting);
    }

    constexpr uint32_t ScalarGetter = 0x110, ScalarSetter = 0x120;
    constexpr uint32_t ScalarDefault = 0x130;
    constexpr uint32_t ScalarRange = 0x148;
    constexpr uint32_t ScalarStep = 0x168;
    constexpr uint64_t SetDisplayFormatRva = 0x4F73DA0;
    constexpr uint64_t SourceAsIntegerRva = 0x7C25470;

    FText NumberTexts[201]{};

    void MakeNumberTexts()
    {
        if (NumberTexts[0].Data)
            return;
        for (int i = 0; i <= 200; i++)
        {
            wchar_t Label[8], Key[32];
            swprintf_s(Label, L"%d", i);
            swprintf_s(Key, L"ShatterNumber%d", i);
            NumberTexts[i] = Text(_wcsdup(Label), _wcsdup(Key));
        }
    }

    FText* __fastcall FormatWholeNumber(FText* Out, void*, double* Source, double*)
    {
        int Value = (int)lround(Source ? *Source : 0.0);
        Value = Value < 0 ? 0 : Value > 200 ? 200 : Value;
        *Out = NumberTexts[Value];
        if (Out->Controller)
            InterlockedIncrement((volatile long*)((uint8_t*)Out->Controller + 8));
        return Out;
    }

    bool AddScalar(UObj* Parent, int Id, const wchar_t* DevName, const wchar_t* DisplayName, const wchar_t* Description,
        double Min, double Max, double Step, double Default, bool bPercent = false)
    {
        if (!ScalarClass)
            return false;
        MakeNumberTexts();
        Mark("creating slider", DevName);
        auto Setting = NewSetting(ScalarClass);
        if (!Setting)
            return false;
        auto S = (uint8_t*)Setting;
        Name(Setting, DevName, DisplayName, Description);
        Mark("making the data source", DevName);
        auto Source = MakeDataSource(Id);
        if (!Source.Object)
            return false;
        for (uint32_t Offset : { ScalarGetter, ScalarSetter })
        {
            *(void**)(S + Offset) = Source.Object;
            *(void**)(S + Offset + 8) = Source.Controller;
            InterlockedIncrement((volatile long*)(Source.Controller + 8));
        }
        *(double*)(S + ScalarDefault) = Default;
        *(S + ScalarDefault + 8) = 1;
        *(S + ScalarRange) = 1;
        *(double*)(S + ScalarRange + 8) = Min;
        *(S + ScalarRange + 0x10) = 1;
        *(double*)(S + ScalarRange + 0x18) = Max;
        *(double*)(S + ScalarStep) = Step;
        Mark("setting the format of", DevName);
        alignas(16) uint8_t Format[0x40];
        memcpy(Format, At<void*>(SourceAsIntegerRva), sizeof(Format));
        if (!bPercent)
            *(void**)Format = (void*)&FormatWholeNumber;
        At<void(*)(UObj*, void*)>(SetDisplayFormatRva)(Setting, Format);
        Mark("adding and initializing", DevName);
        At<void(*)(UObj*, UObj*)>(AddSettingRva)(Parent, Setting);
        return true;
    }

    UObj* FindShatterCollection(UObj* Registry)
    {
        auto Dev = MakeString(L"ShatterCollection");
        auto Found = At<UObj*(*)(UObj*, FStr*)>(FindSettingByDevNameRva)(Registry, &Dev);
        if (Dev.Data)
            Tellurium::Unreal::FMemory::Free(Dev.Data);
        return Found;
    }

    void BuildCollection(UObj* Registry, UObj* LocalPlayer)
    {
        Mark("looking for", L"ShatterCollection");
        if (!EnsureClasses() || FindShatterCollection(Registry))
            return;

        auto Top = NewCollection(L"ShatterCollection", L"Shatter");
        if (!Top)
            return;
        Mark("initializing", L"ShatterCollection");
        At<void(*)(UObj*, UObj*)>(InitializeRva)(Top, LocalPlayer);

        auto Camera = NewCollection(L"ShatterCameraCollection", L"Camera");
        At<void(*)(UObj*, UObj*)>(AddSettingRva)(Top, Camera);
        AddBool(Camera, CustomFOV, L"ShatterCustomFOV", L"Custom FOV",
            L"Use your own field of view instead of the game's. Aiming down sights still zooms in.", false);
        const wchar_t* FovDescription = L"How wide the camera sees when Custom FOV is on. The game's normal view is 80.";
        if (!AddScalar(Camera, FOV, L"ShatterFOV", L"Field of View", FovDescription, 60, 120, 1, 90))
            AddNumber(Camera, FOV, L"ShatterFOV", L"Field of View", FovDescription, 60, 120, 5, 90);

        auto Building = NewCollection(L"ShatterBuildingCollection", L"Building");
        At<void(*)(UObj*, UObj*)>(AddSettingRva)(Top, Building);
        AddScalar(Building, BuildSens, L"ShatterBuildSensitivity", L"Building Sensitivity",
            L"Mouse look speed while in build mode, as a percentage of your normal sensitivity. 100% is no change.",
            10, 500, 5, 100, true);
        AddScalar(Building, EditSens, L"ShatterEditSensitivity", L"Editing Sensitivity",
            L"Mouse look speed while editing a build, as a percentage of your normal sensitivity. 100% is no change.",
            10, 500, 5, 100, true);

        auto Cross = NewCollection(L"ShatterCrosshairCollection", L"Crosshair");
        At<void(*)(UObj*, UObj*)>(AddSettingRva)(Top, Cross);
        AddBool(Cross, Crosshair, L"ShatterCrosshair", L"Custom Crosshair",
            L"Draws your own crosshair in the middle of the screen during matches.", false);
        static const wchar_t* Styles[] = { L"Cross", L"Dot", L"Circle", L"Cross and Dot" };
        AddNumber(Cross, Style, L"ShatterCrosshairStyle", L"Style", nullptr, 0, 3, 1, 0, Styles);
        AddNumber(Cross, Size, L"ShatterCrosshairSize", L"Size", nullptr, 2, 30, 2, 8);
        AddNumber(Cross, Thickness, L"ShatterCrosshairThickness", L"Thickness", nullptr, 1, 6, 1, 2);
        AddNumber(Cross, Gap, L"ShatterCrosshairGap", L"Gap", L"Space between the middle and the lines.", 0, 12, 1, 3);
        AddBool(Cross, Outline, L"ShatterCrosshairOutline", L"Outline", L"A dark edge so the crosshair shows on bright backgrounds.", true);
        static const wchar_t* Colors[ColorPresetCount];
        for (int i = 0; i < ColorPresetCount; i++)
            Colors[i] = ColorPresets[i].Name;
        AddNumber(Cross, Color, L"ShatterCrosshairColor", L"Color", nullptr, 0, ColorPresetCount - 1, 1, 1, Colors);

        Mark("registering", L"ShatterCollection");
        At<void(*)(UObj*, UObj*)>(RegisterSettingRva)(Registry, Top);
        Log("[Settings] added the Shatter settings to a registry");
    }

    using OnInitializeFn = void(__fastcall*)(UObj*, UObj*);
    OnInitializeFn OriginalOnInitialize = nullptr;
    volatile bool bBuildFailed = false;

    void SafeBuild(UObj* Registry, UObj* LocalPlayer)
    {
        if (bBuildFailed)
            return;
        Log("[Settings] building the Shatter settings (registry %p, local player %p)", Registry, LocalPlayer);
        __try { BuildCollection(Registry, LocalPlayer); }
        __except (LogCrash(GetExceptionInformation(), "building the Shatter settings")) { bBuildFailed = true; }
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

    UObj* volatile OurContent = nullptr;
    volatile int32_t OurContentIndex = 0, OurContentSerial = 0;
    UObj* volatile OurMenu = nullptr;
    volatile int32_t OurMenuIndex = 0, OurMenuSerial = 0;

    bool IsAlive(UObj* Object, int32_t Index, int32_t Serial)
    {
        return Object && ObjectAt(Index) == Object && SerialAt(Index) == Serial;
    }

    bool IsOurWidget(UObj* Tab)
    {
        return bAddingTab || (Tab == OurContent && IsAlive(Tab, OurContentIndex, OurContentSerial));
    }

    void __fastcall FilterHook(UObj* Tab, void* FilterState)
    {
        if (!IsOurWidget(Tab))
            return OriginalFilter(Tab, FilterState);

        __try
        {
            auto Registry = *(UObj**)((uint8_t*)Tab + ShimRegistry);
            auto Collection = Registry ? FindShatterCollection(Registry) : nullptr;
            if (!Collection && Registry)
            {
                SafeBuild(Registry, LocalPlayer());
                Collection = FindShatterCollection(Registry);
            }
            if (Collection)
                At<void(*)(void*, UObj*)>(AddToFilterRva)(FilterState, Collection);
            else
                Log("[Settings] the Shatter tab opened but its settings weren't in the registry (registry %p)", Registry);
        }
        __except (LogCrash(GetExceptionInformation(), "showing the Shatter tab")) {}
    }

    bool PatchShim(UObj* TabClass)
    {
        if (PatchedShimVTable)
            return true;
        auto Default = *(UObj**)((uint8_t*)TabClass + ClassDefaultObject);
        if (!Default)
            return false;
        void** VTable = Default->VTable;
        if (VTable[ShimCheckIndex] != At<void*>(ShimCheckRva))
        {
            Log("[Settings] the borrowed tab's vtable isn't the one we expect, no Shatter tab");
            return false;
        }
        OriginalFilter = (FilterFn)VTable[ShimFilterIndex];
        DWORD OldProtect;
        VirtualProtect(&VTable[ShimFilterIndex], sizeof(void*), PAGE_READWRITE, &OldProtect);
        VTable[ShimFilterIndex] = (void*)&FilterHook;
        VirtualProtect(&VTable[ShimFilterIndex], sizeof(void*), OldProtect, &OldProtect);
        PatchedShimVTable = VTable;
        return true;
    }

    UObj* IconTexture = nullptr;
    bool bIconFailed = false;

    bool WriteIconFile(wchar_t (&Path)[MAX_PATH])
    {
        wchar_t* Local = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &Local)))
            return false;
        swprintf_s(Path, L"%s\\Shatter", Local);
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
            Log("[Settings] couldn't load the Shatter icon (import %p, library %p)", Import, Library);
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
            Log("[Settings] the game couldn't read the Shatter icon");
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

    bool MakeIconBrush(UObj* Menu, const uint8_t* Borrowed, uint8_t (&Icon)[BrushSize])
    {
        auto Texture = LoadIconTexture(Menu);
        static UObj* MakeBrush = FindFunction(L"MakeBrushFromTexture", L"WidgetBlueprintLibrary");
        if (!Texture || !MakeBrush)
            return false;

        static int32_t SizeOffset = PropertyOffset(L"SlateBrush", L"ImageSize");
        int32_t Width = 48, Height = 48;
        if (SizeOffset > 0 && SizeOffset + 8 <= (int32_t)BrushSize)
        {
            float W = *(float*)(Borrowed + SizeOffset), H = *(float*)(Borrowed + SizeOffset + 4);
            if (W >= 8 && W <= 512 && H >= 8 && H <= 512)
                Width = (int32_t)W, Height = (int32_t)H;
        }

        struct FParams { UObj* Texture; int32_t Width; int32_t Height; uint8_t ReturnValue[BrushSize]; };
        static_assert(offsetof(FParams, ReturnValue) == 0x10);
        FParams Params{};
        Params.Texture = Texture;
        Params.Width = Width;
        Params.Height = Height;
        *(void**)Params.ReturnValue = *(void* const*)Borrowed;
        ProcessEvent(WidgetLibrary, MakeBrush, &Params);
        memcpy(Icon, Params.ReturnValue, sizeof(Icon));
        Log("[Settings] made the Shatter tab icon (%dx%d, size offset %d)", Width, Height, SizeOffset);
        return true;
    }

    bool SafeMakeIconBrush(UObj* Menu, const uint8_t* Borrowed, uint8_t (&Icon)[BrushSize])
    {
        __try { return MakeIconBrush(Menu, Borrowed, Icon); }
        __except (LogCrash(GetExceptionInformation(), "making the Shatter tab icon")) { bIconFailed = true; return false; }
    }

    UObj* HandledMenu = nullptr;
    int32_t HandledMenuSerial = 0;

    constexpr uint32_t OptionsMenuBorders[] = { 0x528, 0x530 };
    constexpr uint32_t BorderBrush = 0x158;
    constexpr uint32_t ImageBrush = 0x108;
    constexpr uint32_t UserWidgetTree = 0x1A8;
    constexpr uint32_t TreeRoot = 0x28;
    constexpr float PinkR = 0.973f, PinkG = 0.056f, PinkB = 0.768f;

    struct FPinked { UObj* Widget; UObj* SetBrush; uint32_t BrushOffset; int32_t Index, Serial; uint8_t Original[BrushSize]; };
    FPinked Pinked[48]{};
    int PinkedCount = 0;
    bool bPink = false;
    bool bLoggedTree = false;

    void CleanBrush(uint8_t* Brush)
    {
        memset(Brush + 0x38, 0, 16);
        memset(Brush + 0x70, 0, 16);
        if (Brush[0x30] == 1)
            Brush[0x30] = 0;
    }

    void SetWidgetBrush(UObj* Widget, UObj* Func, const uint8_t* Brush)
    {
        alignas(8) uint8_t Params[BrushSize];
        memcpy(Params, Brush, BrushSize);
        ProcessEvent(Widget, Func, Params);
    }

    void RestorePink()
    {
        for (int i = 0; i < PinkedCount; i++)
        {
            auto& P = Pinked[i];
            if (P.Widget && IsAlive(P.Widget, P.Index, P.Serial))
                SetWidgetBrush(P.Widget, P.SetBrush, P.Original);
        }
        PinkedCount = 0;
        bPink = false;
    }

    void PinkWidget(UObj* Widget, UObj* Func, uint32_t BrushOffset)
    {
        if (!Widget || !Func || PinkedCount >= 48)
            return;
        for (int i = 0; i < PinkedCount; i++)
            if (Pinked[i].Widget == Widget)
                return;
        auto& P = Pinked[PinkedCount++];
        memcpy(P.Original, (uint8_t*)Widget + BrushOffset, BrushSize);
        CleanBrush(P.Original);
        if (auto Resource = *(UObj**)(P.Original + 0x48))
            KeepAlive(Resource);

        alignas(8) uint8_t Pink[BrushSize];
        memcpy(Pink, P.Original, BrushSize);
        float Alpha = Pink[0x30] == 0 ? *(float*)(Pink + 0x2C) : 1.f;
        *(float*)(Pink + 0x20) = PinkR, *(float*)(Pink + 0x24) = PinkG, *(float*)(Pink + 0x28) = PinkB;
        *(float*)(Pink + 0x2C) = Alpha;
        Pink[0x30] = 0;
        *(UObj**)(Pink + 0x48) = nullptr;
        *(FName*)(Pink + 0x50) = FName{};
        Pink[0x6C] = 3;
        Pink[0x6D] = 0;
        Pink[0x6F] = 0;
        Pink[0x80] &= ~2;
        memset(Pink + 0x10, 0, 16);

        P.Widget = Widget, P.SetBrush = Func, P.BrushOffset = BrushOffset;
        P.Index = Widget->InternalIndex, P.Serial = SerialAt(Widget->InternalIndex);
        SetWidgetBrush(Widget, Func, Pink);
    }

    bool IsPanelBackground(UObj* Widget, uint32_t BrushOffset)
    {
        auto Resource = *(UObj**)((uint8_t*)Widget + BrushOffset + 0x48);
        if (!Resource)
            return false;
        wchar_t Name[128];
        NameToString(Resource->Name, Name, 128);
        return wcsstr(Name, L"SettingsPanelBG") != nullptr;
    }

    int WalkCount = 0;
    void WalkWidgets(UObj* Widget, int Depth)
    {
        if (!Widget || Depth > 40 || ++WalkCount > 4000)
            return;
        if (IsA(Widget, BorderClass))
        {
            if (IsPanelBackground(Widget, BorderBrush))
                PinkWidget(Widget, SetBrushFunc, BorderBrush);
        }
        else if (IsA(Widget, ImageClass))
        {
            if (IsPanelBackground(Widget, ImageBrush))
                PinkWidget(Widget, ImageSetBrushFunc, ImageBrush);
        }
        if (IsA(Widget, UserWidgetClass))
        {
            auto Tree = *(uint8_t**)((uint8_t*)Widget + UserWidgetTree);
            if (Tree)
                WalkWidgets(*(UObj**)(Tree + TreeRoot), Depth + 1);
        }
        if (IsA(Widget, PanelClass))
        {
            auto& Slots = *(FTArray*)((uint8_t*)Widget + PanelSlots);
            for (int32_t i = 0; Slots.Data && i < Slots.Num && i < 200; i++)
            {
                auto Slot = ((uint8_t**)Slots.Data)[i];
                if (Slot)
                    WalkWidgets(*(UObj**)(Slot + SlotContent), Depth + 1);
            }
        }
    }

    void UpdatePink(UObj* Menu)
    {
        static bool bLoggedClass = false;
        static UObj* CheckedClass = nullptr;
        static bool bClassOk = false;
        if (Menu->Class != CheckedClass)
        {
            CheckedClass = Menu->Class;
            bClassOk = NameIs(Menu->Class->Name, L"OptionsMenu_C");
        }
        if (!SetBrushFunc || !bClassOk)
        {
            if (!bLoggedClass)
                Log("[Settings] no pink background: %s", SetBrushFunc ? "the menu isn't OptionsMenu_C" : "Border.SetBrush not found");
            bLoggedClass = true;
            return;
        }
        bool bWant = Menu == OurMenu && Tellurium::SettingsTab::IsShatterTabShown();
        if (bWant == bPink)
            return;
        if (!bWant)
        {
            RestorePink();
            return;
        }
        UObj* Page = OurContent;
        if (Page && BorderClass && ImageClass && PanelClass && UserWidgetClass)
        {
            WalkCount = 0;
            WalkWidgets(Page, 0);
            if (!bLoggedTree)
                Log("[Settings] pink background: %d of %d widgets on the Shatter page", PinkedCount, WalkCount);
            bLoggedTree = true;
        }
        bPink = true;
    }

    void SafeUpdatePink(UObj* Menu)
    {
        __try { UpdatePink(Menu); }
        __except (LogCrash(GetExceptionInformation(), "colouring the menu background")) { SetBrushFunc = nullptr; }
    }

    void SafeTickPink()
    {
        __try
        {
            UObj* Menu = OurMenu;
            if (Menu && IsAlive(Menu, OurMenuIndex, OurMenuSerial))
                UpdatePink(Menu);
            else if (bPink)
                RestorePink();
        }
        __except (LogCrash(GetExceptionInformation(), "colouring the menu background")) { SetBrushFunc = nullptr; }
    }

    bool HasOurTab(UObj* TabList)
    {
        struct { FName TabNameID; UObj* ReturnValue; } Params{ TabName, nullptr };
        ProcessEvent(TabList, GetTabButtonFunc, &Params);
        return Params.ReturnValue != nullptr;
    }

    bool bGaveUp = false;
    bool GiveUp() { bGaveUp = true; return true; }

    constexpr uint32_t TabListRegistered = 0x2B8;
    constexpr uint32_t RegisteredElementSize = 0x28;

    template <typename F>
    void ForEachRegisteredTab(UObj* TabList, F&& Visit)
    {
        auto Map = (uint8_t*)TabList + TabListRegistered;
        auto& Elements = *(FTArray*)Map;
        auto Secondary = *(uint32_t**)(Map + 0x20);
        auto Bits = Secondary ? Secondary : (uint32_t*)(Map + 0x10);
        int32_t NumBits = *(int32_t*)(Map + 0x28);
        for (int32_t i = 0; i < Elements.Num && i < NumBits; i++)
        {
            if (!(Bits[i / 32] & (1u << (i % 32))))
                continue;
            auto Element = (uint8_t*)Elements.Data + i * RegisteredElementSize;
            Visit(*(FName*)Element, *(UObj**)(Element + 0x10), *(UObj**)(Element + 0x18));
        }
    }

    int32_t LastLoggedPre = -1, LastLoggedRegistered = -1;
    UObj* LastLoggedMenu = nullptr;

    bool AddTab(UObj* Menu)
    {
        auto TabList = *(UObj**)((uint8_t*)Menu + MenuTabList);
        auto Switcher = *(UObj**)((uint8_t*)Menu + MenuSwitcher);
        if (!TabList || !Switcher)
        {
            if (Menu != LastLoggedMenu)
                Log("[Settings] options menu %p has no tab list (%p) or switcher (%p) yet", Menu, TabList, Switcher);
            LastLoggedMenu = Menu, LastLoggedPre = -1, LastLoggedRegistered = -1;
            return false;
        }

        auto& Infos = *(FTArray*)((uint8_t*)TabList + TabListPreregistered);
        UObj* ContentClass = nullptr;
        UObj* ButtonType = nullptr;
        const uint8_t* BorrowedLabel = nullptr;
        for (int32_t i = 0; Infos.Data && i < Infos.Num && !ContentClass; i++)
        {
            auto Info = (uint8_t*)Infos.Data + i * TabInfoSize;
            auto Content = *(UObj**)(Info + TabInfoCreatedContent);
            if (Content && IsA(Content, ShimClass) && *(UObj**)(Info + TabInfoButtonType))
            {
                ContentClass = Content->Class;
                ButtonType = *(UObj**)(Info + TabInfoButtonType);
                BorrowedLabel = Info + TabInfoLabel;
            }
        }
        int32_t Registered = 0;
        bool bOurs = false;
        ForEachRegisteredTab(TabList, [&](FName Key, UObj* Button, UObj* Content)
        {
            Registered++;
            bOurs |= Key.Index == TabName.Index && Key.Number == TabName.Number;
            if (!ContentClass && Button && Content && IsA(Content, ShimClass))
            {
                ContentClass = Content->Class;
                ButtonType = Button->Class;
            }
        });

        if (bOurs)
            return true;
        if (Menu != LastLoggedMenu)
        {
            Log("[Settings] options menu %p: %d preregistered tabs, %d registered, settings tab to borrow: %s",
                Menu, Infos.Num, Registered, ContentClass ? "yes" : "not yet");
            LastLoggedMenu = Menu, LastLoggedPre = Infos.Num, LastLoggedRegistered = Registered;
        }
        if (Registered == 0)
            return false;
        if (!ContentClass)
            return false;
        if (!PatchShim(ContentClass))
            return GiveUp();

        auto Player = (uint8_t*)LocalPlayer();
        auto OwningPlayer = Player ? *(UObj**)(Player + 0x30) : nullptr;

        UObj* Content = nullptr;
        if (OurMenu == Menu && IsAlive(Menu, OurMenuIndex, OurMenuSerial) && OurContent &&
            IsAlive(OurContent, OurContentIndex, OurContentSerial))
            Content = OurContent;
        else
        {
            struct { UObj* WorldContextObject; UObj* WidgetType; UObj* OwningPlayer; UObj* ReturnValue; } Create{};
            Create.WorldContextObject = Menu;
            Create.WidgetType = ContentClass;
            Create.OwningPlayer = OwningPlayer;
            bAddingTab = true;
            ProcessEvent(WidgetLibrary, CreateWidgetFunc, &Create);
            bAddingTab = false;
            Content = Create.ReturnValue;
            if (!Content)
            {
                Log("[Settings] couldn't create the Shatter tab's page");
                return GiveUp();
            }
            OurContentIndex = Content->InternalIndex;
            OurContentSerial = SerialAt(Content->InternalIndex);
            OurContent = Content;
        }

        struct { UObj* Content; bool ReturnValue; } Has{ Content, false };
        ProcessEvent(Switcher, HasChildFunc, &Has);
        if (!Has.ReturnValue)
        {
            struct { UObj* Content; UObj* ReturnValue; } Add{ Content, nullptr };
            ProcessEvent(Switcher, AddChildFunc, &Add);
        }

        struct FRegisterParams
        {
            FName TabNameID;
            UObj* TabButtonType;
            uint8_t LabelInfo[LabelInfoSize];
            UObj* ContentWidget;
            bool ReturnValue;
            uint8_t Pad[7];
        };
        static_assert(offsetof(FRegisterParams, LabelInfo) == 0x10 && offsetof(FRegisterParams, ContentWidget) == 0xB0 && sizeof(FRegisterParams) == 0xC0);
        FRegisterParams Params{};
        Params.TabNameID = TabName;
        Params.TabButtonType = ButtonType;
        if (BorrowedLabel)
            memcpy(Params.LabelInfo, BorrowedLabel, LabelInfoSize);
        *(FText*)Params.LabelInfo = Text(L"Shatter", L"ShatterTab_Name");
        uint8_t Icon[BrushSize];
        if (SafeMakeIconBrush(Menu, Params.LabelInfo + LabelIcon, Icon))
            memcpy(Params.LabelInfo + LabelIcon, Icon, BrushSize);
        Params.ContentWidget = Content;
        ProcessEvent(TabList, RegisterFortTabFunc, &Params);

        OurMenuIndex = Menu->InternalIndex;
        OurMenuSerial = SerialAt(Menu->InternalIndex);
        OurMenu = Menu;
        Log("[Settings] added the Shatter tab (registered: %d)", Params.ReturnValue);
        SafeUpdatePink(Menu);
        return true;
    }

    bool SafeAddTab(UObj* Menu)
    {
        __try { return AddTab(Menu); }
        __except (LogCrash(GetExceptionInformation(), "adding the Shatter tab"))
        {
            bAddingTab = false;
            return GiveUp();
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
        bInMenuHook = true;
        if (!(Object == HandledMenu && SerialAt(Object->InternalIndex) == HandledMenuSerial))
        {
            if (TabName.Index == 0)
                TabName = MakeName(L"ShatterTab");
            bGaveUp = false;
            SafeAddTab(Object);
            if (bGaveUp)
            {
                HandledMenu = Object;
                HandledMenuSerial = SerialAt(Object->InternalIndex);
            }
        }
        bInMenuHook = false;
    }

    bool PatchSlot(UObj* Class, int Index, uint64_t ExpectedRva, void* Hook, void** Original, const char* What)
    {
        UObj* Default = nullptr;
        for (int Try = 0; Try < 120 && !(Default = *(UObj**)((uint8_t*)Class + ClassDefaultObject)); Try++)
            Sleep(1000);
        if (!Default)
        {
            Log("[Settings] the %s has no default object, no Shatter tab", What);
            return false;
        }
        void** VTable = Default->VTable;
        if (ExpectedRva && VTable[Index] != At<void*>(ExpectedRva))
        {
            Log("[Settings] the %s's vtable isn't the one we expect, no Shatter tab", What);
            return false;
        }
        *Original = VTable[Index];
        DWORD OldProtect;
        VirtualProtect(&VTable[Index], sizeof(void*), PAGE_READWRITE, &OldProtect);
        VTable[Index] = Hook;
        VirtualProtect(&VTable[Index], sizeof(void*), OldProtect, &OldProtect);
        return true;
    }

    DWORD WINAPI SettingsThread(LPVOID)
    {
        Log("[Settings] starting (12.41)");
        CreateThread(0, 0, SaveThread, 0, 0, 0);
        while (!EnsureClasses())
            Sleep(1000);
        FillVTables();

        if (!PatchSlot(RegistryClass, OnInitializeIndex, OnInitializeRva, (void*)&OnInitializeHook, (void**)&OriginalOnInitialize, "settings registry"))
            return 0;
        if (!PatchSlot(OptionsMenuClass, ProcessEventIndex, 0, (void*)&MenuProcessEventHook, (void**)&OriginalMenuProcessEvent, "settings menu"))
            return 0;
        Log("[Settings] watching the settings menu (12.41)");
        return 0;
    }
}

void Tellurium::SettingsTab::Start()
{
    if (!IsSupported())
        return;
    CreateThread(0, 0, SettingsThread, 0, 0, 0);
}

void Tellurium::SettingsTab::GameThreadTick()
{
    static ULONGLONG Next = 0;
    ULONGLONG Now = GetTickCount64();
    if (Now < Next || !bClassesReady || !SetBrushFunc)
        return;
    Next = Now + 50;
    SafeTickPink();
}

bool Tellurium::SettingsTab::IsShatterTabShown()
{
    __try
    {
        UObj* Menu = OurMenu;
        UObj* Content = OurContent;
        if (!Menu || !Content || !IsAlive(Menu, OurMenuIndex, OurMenuSerial) || !IsAlive(Content, OurContentIndex, OurContentSerial))
            return false;
        if (!*((uint8_t*)Menu + ActivatableIsActive))
            return false;
        auto Switcher = *(uint8_t**)((uint8_t*)Menu + MenuSwitcher);
        if (!Switcher)
            return false;
        auto& Slots = *(FTArray*)(Switcher + PanelSlots);
        int32_t Active = *(int32_t*)(Switcher + SwitcherActiveIndex);
        if (Active < 0 || Active >= Slots.Num || !Slots.Data)
            return false;
        auto Slot = ((uint8_t**)Slots.Data)[Active];
        return Slot && *(UObj**)(Slot + SlotContent) == Content;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
