/* Mod-loader adapter: this DLL is loaded from mods\ by the Azimuth Mod Loader, which has
   already located the game Azimuth mapped. */
#include "azimuth_mod.h"
#include "fh2/runtime.h"
#include "fh3/fh3.h"
#include "fm5/fm5.h"
#include "shared/logging.h"
#include "shared/support.h"

AZIMUTH_MOD_GAMES(AZIMUTH_GAME_FORZA_HORIZON_2 | AZIMUTH_GAME_FORZA_HORIZON_3 | AZIMUTH_GAME_FORZA_MOTORSPORT_5);

/* The mod was FH2_ExtraOptions.dll: an older install keeps its config files and settings. */
static void move_old_data(const AzimuthModHost *host) {
    static wchar_t before[PATH_CAPACITY], now[PATH_CAPACITY];
    if (path_join(before, host->mods_dir, L"FH2_ExtraOptions") && path_join(now, host->mods_dir, L"FH_ExtraOptions")) {
        MoveFileExW(before, now, 0); // fails harmlessly: nothing to move, or already moved
    }
}

void adapter_process_attach(HINSTANCE instance) {
    (void)instance; // the work happens in AzimuthModLoad, called by the loader
}

__declspec(dllexport) int AzimuthModLoad(const AzimuthModHost *host) {
    if (!host || host->size < sizeof(*host) || host->api_version != AZIMUTH_MOD_API_VERSION ||
        !host->game_image) {
        return 0;
    }
    move_old_data(host);
    CreateDirectoryW(host->data_dir, NULL); // config files, .dat, logs; fails harmlessly if it exists
    if (host->game == AZIMUTH_GAME_FORZA_MOTORSPORT_5) {
        return fm5_initialize(FM5_TITLE_ID, host->game_image, host->game_exe, host->data_dir);
    }
    if (host->game == AZIMUTH_GAME_FORZA_HORIZON_3) {
        return fh3_initialize(FH3_TITLE_ID, host->game_image, host->game_exe, host->data_dir);
    }
    if (host->game != AZIMUTH_GAME_FORZA_HORIZON_2) {
        return 0; // every patch is an address of one of the games above
    }
    if (!runtime_initialize(host->game_image, host->game_exe, host->data_dir)) {
        return 0;
    }
    char message[] = "LOADER: FH2 image at 0x0000000000000000; loaded as a mod.\r\n";
    const char digits[] = "0123456789abcdef";
    uintptr_t value = (uintptr_t)host->game_image;
    for (unsigned i = 0; i < 16; ++i) {
        message[sizeof("LOADER: FH2 image at 0x") - 1 + i] = digits[(value >> (60 - 4 * i)) & 15];
    }
    log_set_directory(host->data_dir);
    log_append(message);
    return 1;
}
