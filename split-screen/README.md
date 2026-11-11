# Forza Motorsport 5 Split-Screen & Multiplayer Mod

A mod for [Azimuth Mod Loader](https://github.com/Bullet4fun/Azimuth_ModLoader) enabling **full 2-Player Split-Screen multiplayer** in *Forza Motorsport 5* (Xbox One Durango) running on PC via Azimuth 1.0.4.

Based on the standalone fix by [Maiteux Zanela (fm5-splitscreen-fix)](https://github.com/maiteuxzanela/fm5-splitscreen-fix).

## Features

1. **WinRT Multi-User Identity & Controller Pairing Emulation:**
   - Overcomes Azimuth's hardcoded single-user mapping, enabling distinct Player 1 and Player 2 user profiles.
   - Synthesizes COM `IControllerPairingChangedEventArgs` with full 13-slot vtable and proper `QueryInterface` semantics.
   - Fixes Azimuth's DirectInput button bitshift defect, restoring `GamepadButtons::Menu` (Start) and `GamepadButtons::View` (Back).
2. **Race Transition & Window Event Shield (RVA `0x5d7500` / `0x687f48`):**
   - Fixes the crash when clicking "Start Race" in the split-screen car selection lobby by returning `S_OK` on `WindowActivated` and installing an anti-NULL guard on the event queue iterator.
3. **Visibility Thread Buffer Shield (RVA `0x859516`, `0x859500`, `0x85b773`, `0x8580e0`):**
   - Eliminates catastrophic memory corruption on the 3D track caused by visibility buffer saturation in split-screen.
   - Patches unsigned `JAE` to signed `JGE` in `vector::insert`, eliminating runaway backward memory copying.
   - Patches helper `0x14085b770` with early `JAE` return, eliminating runaway forward memory copying.
   - Clamps `where <= _Mylast` across registers (`%r8`, `%rbp`, and stack spill).

## Install

1. Place `FM5_SplitScreen.dll` inside the `mods` folder next to `azimuth.exe`.
2. Launch the game through Azimuth (with `Azimuth_ModLoader`'s `dinput8.dll` installed).
3. Connect two controllers.
4. Press **A** on Controller 1 at the start screen.
5. In the main menu, go to **Multiplayer** -> **Split-Screen**.
6. In the lobby, press **Start / Menu** on Controller 2 to join as Player 2.
7. Select cars and track, start the race, and drive!
