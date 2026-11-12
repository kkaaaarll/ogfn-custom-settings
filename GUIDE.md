# Quick guide

## Add it to Tellurium

1. Copy everything in `src` into your Tellurium project folder (next to `Hooks.cpp`).
2. In Visual Studio right click the project, Add, Existing Item, and pick the new files.
3. In `Hooks.cpp` add `#include "SettingsTab.h"` and `#include "Fov.h"` at the top.
4. In the init function, after the redirect is set up, call:

```cpp
Tellurium::SettingsTab::Start();
Tellurium::Fov::Start();
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

`SettingsVersion` goes up every time a value changes, so you can check it to see if something needs updating.

Custom FOV works on its own once `Fov::Start()` is called. It scales the game's FOV instead of replacing it, so aiming down sights still zooms in. Changes apply straight away, no restart needed.

The crosshair rows only store values. Drawing the crosshair is up to your own code (an ImGui overlay or similar).

## Changing the tab

- Name: search `Text(L"Refract", L"RefractTab_Name")` in `SettingsTab.cpp`
- Icon: replace the bytes in `TabIcon.h` with your own PNG (128x128 works well). Any file to C array converter works
- Rows: `BuildCollection` in `SettingsTab.cpp`. Use `AddBool` for on/off rows and `AddNumber` for everything else

## If something goes wrong

Check `%LOCALAPPDATA%\Refract\client.log`. Every step logs a `[Settings]` line, and if something fails it says which row it was on.
