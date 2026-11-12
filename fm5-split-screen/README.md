# FM5 Split-Screen

Two-player split-screen for Forza Motorsport 5. On Azimuth the game sees only one signed-in user, so a second
pad can't join. This mod adds a second player, "Guest", for the second pad.

Install: put `FM5_SplitScreen.dll` in the `mods` folder next to `azimuth.exe`. No settings.

## How it plays

- The first pad that presses A is player 1, with your profile and save. The other pad plays as Guest.
- Unplugging a pad and plugging it back in is fine. If player 1's pad leaves, the next pad that presses A
  becomes player 1; Guest's pad stays Guest's.
- Guest can pick from your garage and from the rentals. Guest can't buy cars.
- Your save is read only for Guest: whatever the game tries to save for Guest is dropped, so Guest can't change
  or damage your save. Guest's own progress is not kept.

Tested on Azimuth 1.0.4 with Xbox pads and a DualSense, with and without Steam.

## Known issue

Guest reads your garage when they join. A car you buy while Guest is already in won't show up in Guest's
garage straight away. Fix: Guest presses B to leave and Menu (Start) to join again, and the car is there.
You only hit this if you buy a car in the middle of a split-screen session.

## How it works

The mod doesn't patch Azimuth or the game's code. Azimuth gives the game all its WinRT objects (users, pads,
save storage) through one import; the mod points that import at its own function and wraps those objects.
It writes what it does to `mods\FM5_SplitScreen\FM5_SplitScreen.log`.

## Credits

Based on [Maiteux Zanela's research](https://github.com/maiteuxzanela/fm5-splitscreen-fix): their notes on
how Azimuth handles users and pads made this mod much quicker to write.
