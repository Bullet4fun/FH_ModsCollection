# Extra Options

[RokkuDayo's Extra Options](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions) 0.6.0 for Forza
Horizon 2, Forza Horizon 3 and Forza Motorsport 5, as a mod for the Azimuth Mod Loader. Forza Horizon 3
support is by qiangqiang101.

## Features in Forza Horizon 2

- Change your car anywhere, "Return to Garage" in the pause menu
- "Jump to Manufacturer" in the car and rim menus
- All one-off races from online playable offline, your own car in showcase events
- Autodrive, drift camera, FOV for every camera
- Headlights (they light the road in daytime too), hazards, turn signals and roof toggles
- Rev your engine in the garage and the engine swap menu
- Photo mode in the "View Car" menus, time (with an hour slider), weather, driver and car lights in photo mode
- Master volume and dialogue toggles, spring and power units, a hide-the-UI key, vignette toggles
- Option to turn off Drivatars in free roam; Drivatars can use your local liveries
- All DLC, VIP and Treasure Map unlocked
- 4K liveries, unrestricted photo mode, photo supersampling
- All stock and aftermarket rims in one category, resizable stock rims
- Custom spawn location, minimap that follows the camera, intro skip
- `mediapc` modding folder, add-on mods, missing Azimuth save markers rebuilt from your saves
- Experimental: aero tuning without aero parts, every engine and gearbox as a swap

## Features in Forza Horizon 3

- Autodrive, drift camera, FOV for every camera, a minimap that follows the camera, an FPS readout
- "Jump to Manufacturer", "Return to Garage", online tabs offline, intro skip
- VIP, Treasure Map, hidden Autoshow cars and Forzathon horns unlocked
- Indicators, lights and doors through ANNA, 4K liveries, unrestricted photo mode
- `mediapc` modding folder, add-on mods

## Features in Forza Motorsport 5

- Drift camera and FOV sliders, cockpit motion blur, volume of each audio channel
- 4K liveries and 4x MSAA, player model selector, gamerpic badge
- "Jump to Manufacturer", all stock and aftermarket rims in one category, resizable stock rims
- VIP unlocked, Marketplace offline, headlight toggle, a hide-the-UI key
- Photo mode: no collision or boundaries, reset the car's damage, hide the driver, car lights
- Intro skip, optional E3 start screen, `mediapc` modding folder, add-on mods

## Config files

Each game has its own config file in `mods\FH_ExtraOptions\`: `FH2_ExtraOptions.ini`,
`FH3_ExtraOptions.ini` and `FM5_ExtraOptions.ini`.

Most features are set in the game's own menus. The config file turns each feature on or off;
a feature turned off there disappears from the menus. Its `[Bindings]` section sets the
controller and keyboard shortcuts.

The custom gamerpic of the original is left out: Azimuth sets your profile picture itself.

## Install

Unzip `FH_ExtraOptions.zip` into the Azimuth folder (where `azimuth.exe` is).

Add-on mods made for Extra Options (a folder with a `mod.json`) go in `mods\Forza Horizon 2\`,
`mods\Forza Horizon 3\` or `mods\Forza Motorsport 5\`, each with its own `loadorder.txt`. Forza Horizon 2
add-ons still in `mods\` move there by themselves. If an older `FH2_ExtraOptions.dll` is still in `mods`, delete it: two copies would run at once. The zip brings fresh config files in `mods\FH_ExtraOptions\`, so your old settings stay in `mods\FH2_ExtraOptions\`: copy `FH2_ExtraOptions.ini` over to keep them, then delete that folder. Don't install the original release's `winmm.dll` as
well: Extra Options would run twice.

## Controls

| Action | Button |
|---|---|
| Autodrive | D-pad down (F2 on keyboard) |
| Headlights | click the right stick (F3) |
| Hazards | tap D-pad up |
| Turn signals | hold D-pad left/right for half a second |
| Roof | hold the right stick for 1.5 seconds while standing still (F4) |
| Hide the UI | F1 |

Headlights, hazards, turn signals and roof are off by default: set them to 1 in the `[Vehicle]`
section of `mods\FH_ExtraOptions\FH2_ExtraOptions.ini`.

Some cars have glitched turn signals or roofs: see the original mod's
[wiki](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions/wiki/4.-Oddities).

The Treasure Map is off by default: set `UnlockTreasureMap=1` in the config file.

## AI usage disclosure (from the original author)

Codex was used in this project to create the DLL. The actual research was done manually, mostly based on things I already toyed with in the other titles from the series, but I don't yet have the C language knowledge to code something like this fully from scratch (I was initially editing the raw executable). Leaving this here as I believe people have the right to know.
