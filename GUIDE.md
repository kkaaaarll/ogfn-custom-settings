# Quick guide

## Add it to Tellurium

1. Copy everything in `src` into your Tellurium project folder (next to `Hooks.cpp`).
2. In Visual Studio right click the project, Add, Existing Item, and pick the new files.
3. In `Hooks.cpp` add `#include "SettingsTab.h"` and `#include "Fov.h"` at the top.
4. In the init function, after the redirect is set up, call:

```cpp
if (Tellurium::Fov::Start()) // loads the saved values; false if this isn't 12.41
    Tellurium::SettingsTab::Start();
```

5. Build Release x64 and use the DLL like normal.

Open settings in game and the tab shows up next to the others.

## Using the values

Everything lives in `Tellurium::CustomSettings::Settings`:

```cpp
auto& S = Tellurium::CustomSettings::Settings;
if (S.bCustomFOV)
    FOV = S.FOV;
```

`SettingsVersion` goes up every time a value changes, so you can check it to see if something needs updating. Saving to disk happens a moment after the last change, so dragging a slider doesn't write the file every step.

Custom FOV, building sensitivity and editing sensitivity all work on their own once `Fov::Start()` is called. They apply straight away, no restart needed.

The crosshair rows only store values. Drawing the crosshair is up to your own code (an ImGui overlay or similar). `Fov::InMatch()` tells you whether you're in a match, and `SettingsTab::IsShatterTabShown()` whether the settings menu is open on the tab.

## Changing the tab

- Name: search `Text(L"Shatter", L"ShatterTab_Name")` in `SettingsTab.cpp`
- Icon: replace the bytes in `TabIcon.h` with your own PNG (128x128 or 256x256 works well). Any file to C array converter works
- Background colour: `PinkR`, `PinkG`, `PinkB` in `SettingsTab.cpp` (linear colour, not sRGB)
- Rows: `BuildCollection` in `SettingsTab.cpp`. Use `AddBool` for on/off rows, `AddScalar` for sliders and `AddNumber` for arrow lists

## If something goes wrong

Check `%LOCALAPPDATA%\Shatter\client.log`. The first line says when the DLL was built, so you can tell the right one loaded. Every step logs a `[Settings]` line, and if something fails it says which row it was on.
