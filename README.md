# ogfn-custom-settings (12.41)

Adds your own tab to the Fortnite settings menu, with real settings rows that look and work like the game's own (sliders, arrows, option bars, the description panel on the right).

![preview](preview.png)

The example tab has three sections:

- **Camera:** custom FOV, with a 60 to 120 slider
- **Building:** building sensitivity and editing sensitivity, each 10% to 500% of your normal look sensitivity, like current Fortnite's build and edit mode multipliers
- **Crosshair:** style, size, thickness, gap, outline, color

While the tab is open the menu background turns pink (`#fc43e3`), and it goes back when you switch tabs. Values save to `%LOCALAPPDATA%\Shatter\client.ini` and load again next launch.

## 12.41 only

This branch only works on 12.41 (`++Fortnite+Release-12.41-CL-12905909`). The 19.10 version is on the [`19.10`](../../tree/19.10) branch.

It calls engine functions directly by address and reads class fields by offset. Those values were pulled out of the 12.41 exe with IDA and an SDK dump, and every build moves them around, so on any other version they point at the wrong thing. The code checks the exe timestamp first and does nothing if it isn't 12.41.

## How it works

- Hooks the settings registry so a Shatter collection gets built and registered next to the game's own ones
- Each row gets a data source that reads and writes the values in `CustomSettings.h`. The sliders are the game's own slider rows (the ones mouse sensitivity uses)
- 12.41's options menu registers its tabs one by one and clears them every time it closes. So on every menu event the code checks the tab list and adds the Shatter tab again if it's missing, using one of the game's existing tab layouts
- The pink background swaps the brush on the page's `M_UI_SettingsPanelBG` border, and puts the original back when you leave the tab
- The tab icon is a PNG built into the DLL (`TabIcon.h`), loaded as a texture at runtime
- Custom FOV hooks `GetFOVAngle` on the local camera manager and scales the result, so ADS zoom still works
- Building and editing sensitivity scale the player controller's `InputYawScale` / `InputPitchScale` while you hold the building or editing tool, and put the game's values back when you switch to anything else

It's written as a drop-in for Tellurium. See [GUIDE.md](GUIDE.md) for setup.

## Credits

Built on [Tellurium](https://github.com/plooshi/Tellurium) by Sarah (plooshware). Tellurium is the redirect / SSL bypass DLL this plugs into, all credit to her for it.
