/*
 * split_screen_mod.c — FM5 Split-Screen & Multiplayer Fix
 * Compatible with Azimuth Mod Loader (mods\FM5_SplitScreen.dll).
 */
#include "azimuth_mod.h"
#include "fm5_hooks.h"

AZIMUTH_MOD_GAMES(AZIMUTH_GAME_FORZA_MOTORSPORT_5);

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reserved;
    if (reason == DLL_PROCESS_DETACH) {
        fm5_hooks_shutdown();
    }
    return TRUE;
}

__declspec(dllexport) int AzimuthModLoad(const AzimuthModHost *host) {
    if (!host || host->size < sizeof(*host) || host->api_version != AZIMUTH_MOD_API_VERSION) {
        return 0;
    }
    if (host->game != AZIMUTH_GAME_FORZA_MOTORSPORT_5) {
        return 1;
    }

    fm5_hooks_init();
    return 1;
}
