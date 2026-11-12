#pragma once

namespace Tellurium
{
    namespace CustomSettings
    {
        enum class CrosshairStyle : int { Cross, Dot, Circle, CrossAndDot };

        struct FSettings
        {
            bool bCustomFOV = false;
            float FOV = 90.f;

            bool bCrosshair = false;
            int CrosshairStyle = (int)CrosshairStyle::Cross;
            float CrosshairSize = 8.f;
            float CrosshairThickness = 2.f;
            float CrosshairGap = 3.f;
            bool bCrosshairOutline = true;
            float CrosshairColor[4] = { 0.f, 1.f, 0.9f, 1.f };
        };

        extern FSettings Settings;
        extern volatile long SettingsVersion;

        void Load();
        void Save();
    }
}
