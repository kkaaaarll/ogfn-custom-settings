#include "pch.h"
#include "CustomSettings.h"
#include <shlobj.h>
#include <cstdio>

namespace Tellurium
{
    namespace CustomSettings
    {
        FSettings Settings;
        volatile long SettingsVersion = 1;

        namespace
        {
            bool SettingsPath(wchar_t (&Out)[MAX_PATH])
            {
                wchar_t* Local = nullptr;
                if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &Local)))
                    return false;
                swprintf_s(Out, L"%s\\Refract", Local);
                CoTaskMemFree(Local);
                CreateDirectoryW(Out, nullptr);
                wcscat_s(Out, L"\\client.ini");
                return true;
            }

            float ReadFloat(const wchar_t* File, const wchar_t* Key, float Default)
            {
                wchar_t Text[64];
                GetPrivateProfileStringW(L"Client", Key, L"", Text, 64, File);
                return Text[0] ? (float)_wtof(Text) : Default;
            }

            void WriteFloat(const wchar_t* File, const wchar_t* Key, float Value)
            {
                wchar_t Text[64];
                swprintf_s(Text, L"%g", Value);
                WritePrivateProfileStringW(L"Client", Key, Text, File);
            }

            float Clamp(float Value, float Min, float Max) { return Value < Min ? Min : Value > Max ? Max : Value; }
        }

        void Load()
        {
            wchar_t File[MAX_PATH];
            if (!SettingsPath(File))
                return;
            auto& S = Settings;
            S.bCustomFOV = ReadFloat(File, L"CustomFOV", 0) != 0;
            S.FOV = Clamp(ReadFloat(File, L"FOV", S.FOV), 60.f, 120.f);
            S.bCrosshair = ReadFloat(File, L"Crosshair", 0) != 0;
            S.CrosshairStyle = (int)Clamp(ReadFloat(File, L"CrosshairStyle", 0), 0, 3);
            S.CrosshairSize = Clamp(ReadFloat(File, L"CrosshairSize", S.CrosshairSize), 2.f, 30.f);
            S.CrosshairThickness = Clamp(ReadFloat(File, L"CrosshairThickness", S.CrosshairThickness), 1.f, 6.f);
            S.CrosshairGap = Clamp(ReadFloat(File, L"CrosshairGap", S.CrosshairGap), 0.f, 20.f);
            S.bCrosshairOutline = ReadFloat(File, L"CrosshairOutline", 1) != 0;
            const wchar_t* ColorKeys[] = { L"CrosshairR", L"CrosshairG", L"CrosshairB", L"CrosshairA" };
            for (int i = 0; i < 4; i++)
                S.CrosshairColor[i] = Clamp(ReadFloat(File, ColorKeys[i], S.CrosshairColor[i]), 0.f, 1.f);
            InterlockedIncrement(&SettingsVersion);
        }

        void Save()
        {
            wchar_t File[MAX_PATH];
            if (!SettingsPath(File))
                return;
            auto& S = Settings;
            WriteFloat(File, L"CustomFOV", S.bCustomFOV);
            WriteFloat(File, L"FOV", S.FOV);
            WriteFloat(File, L"Crosshair", S.bCrosshair);
            WriteFloat(File, L"CrosshairStyle", (float)S.CrosshairStyle);
            WriteFloat(File, L"CrosshairSize", S.CrosshairSize);
            WriteFloat(File, L"CrosshairThickness", S.CrosshairThickness);
            WriteFloat(File, L"CrosshairGap", S.CrosshairGap);
            WriteFloat(File, L"CrosshairOutline", S.bCrosshairOutline);
            const wchar_t* ColorKeys[] = { L"CrosshairR", L"CrosshairG", L"CrosshairB", L"CrosshairA" };
            for (int i = 0; i < 4; i++)
                WriteFloat(File, ColorKeys[i], S.CrosshairColor[i]);
        }
    }
}
