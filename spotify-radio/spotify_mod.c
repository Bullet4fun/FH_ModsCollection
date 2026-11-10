/* Spotify radio mod (mods\FH_SpotifyRadio.dll): Spotify Connect (librespot) plays as the
   game's own "Xbox Music" station, with Now Playing on the radio HUD. No settings. Its folder
   mods\FH_SpotifyRadio\ holds the log, librespot.exe, onevent.cmd and librespot's files (Spotify
   login, volume, nowplaying.txt). */
#include "track_controls.h"
#include "azimuth_mod.h"
#include "spotify_bridge.h"
#include "logging.h"
#include "mod_log.h"
#include "support.h"

#ifndef SPOTIFY_VERSION
#define SPOTIFY_VERSION "dev" // build.py passes the release version
#endif
#ifndef LINE_IN_VERSION
#define LINE_IN_VERSION "dev"
#endif

AZIMUTH_MOD_GAMES(AZIMUTH_GAME_FORZA_HORIZON_2 | AZIMUTH_GAME_FORZA_HORIZON_3);
// The game is told apart by the code at its hook sites (spotify_bridge_detect_game), not host->game:
// a host->game check made one antivirus ML engine flag the DLL.

static wchar_t path[PATH_CAPACITY];

static int same_ignore_case(const wchar_t *a, const wchar_t *b) {
    for (;; ++a, ++b) {
        wchar_t x = *a >= L'A' && *a <= L'Z' ? *a + 32 : *a, y = *b >= L'A' && *b <= L'Z' ? *b + 32 : *b;
        if (x != y) {
            return 0;
        }
        if (!x) {
            return 1;
        }
    }
}

static wchar_t logo_source[PATH_CAPACITY], logo_target[PATH_CAPACITY], logo_record[PATH_CAPACITY];
static wchar_t logo_previous[PATH_CAPACITY];

/* Copies the mod's logo into Mount\spotifyradio\, a folder only this mod uses, as the Xbox Music
   station's HUD icon; the mod mounts that folder into the game's files itself. A logo this mod placed
   somewhere else last time (logo_installed.txt) is removed. 1: the logo is in place. */
static int install_logo(const AzimuthModHost *host, int fh3) {
    if (!path_join(logo_source, host->data_dir, fh3 ? RADIO_WIDE(LOGO_FILE_FH3) : RADIO_WIDE(LOGO_FILE)) ||
        !path_join(logo_record, host->data_dir, fh3 ? L"logo_installed_fh3.txt" : L"logo_installed.txt") ||
        GetFileAttributesW(logo_source) == INVALID_FILE_ATTRIBUTES) {
        log_append(fh3 ? "SPOTIFY: no " LOGO_FILE_FH3 " in the mod folder; Xbox Music keeps its logo.\r\n"
                       : "SPOTIFY: no " LOGO_FILE " in the mod folder; Xbox Music keeps its logo.\r\n");
        return 0;
    }
    size_t cut = 0; // game_exe is ...\Mount\forza_durango_release_final.exe; the folder goes beside it
    for (size_t i = 0; host->game_exe[i]; ++i) {
        if (host->game_exe[i] == L'\\' || host->game_exe[i] == L'/') cut = i + 1;
    }
    for (size_t i = 0; i < cut; ++i) logo_target[i] = host->game_exe[i];
    logo_target[cut] = 0;
    if (!path_join(logo_target, logo_target, RADIO_WIDE(LOGO_FOLDER) L"\\ui\\textures\\Anthem\\HUD\\") ||
        !path_join(logo_target, logo_target, fh3 ? L"RadioLogos\\Groove_Music.swatchbin" : L"HUD_RadioIcon_Xbox_Music.swatchbin")) {
        return 0;
    }
    for (size_t i = cut; logo_target[i]; ++i) { // create spotifyradio\ui\textures\Anthem\HUD\ as needed
        if (logo_target[i] == L'\\') {
            logo_target[i] = 0;
            CreateDirectoryW(logo_target, NULL);
            logo_target[i] = L'\\';
        }
    }

    DWORD got = 0;
    HANDLE record = CreateFileW(logo_record, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (record != INVALID_HANDLE_VALUE) {
        ReadFile(record, logo_previous, sizeof(logo_previous) - sizeof(wchar_t), &got, NULL);
        CloseHandle(record);
    }
    logo_previous[got / sizeof(wchar_t)] = 0;
    if (logo_previous[0] && !same_ignore_case(logo_previous, logo_target)) {
        DeleteFileW(logo_previous);
    }
    if (!CopyFileW(logo_source, logo_target, FALSE)) {
        log_append("SPOTIFY: could not copy the logo into the game's " LOGO_FOLDER " folder.\r\n");
        return 0;
    }
    record = CreateFileW(logo_record, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (record != INVALID_HANDLE_VALUE) {
        size_t length = 0;
        while (logo_target[length]) ++length;
        DWORD written;
        WriteFile(record, logo_target, (DWORD)(length * sizeof(wchar_t)), &written, NULL);
        CloseHandle(record);
    }
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reserved;
    if (reason == DLL_PROCESS_DETACH) {
        spotify_bridge_stop();
    }
    return TRUE;
}

/* This mod was FH2_<name>: an older install's config file, login and songs move to its folder now,
   with the FH2_ gone from their names. What the zip just brought (librespot.exe) stays the new one. */
static void move_old_data(const AzimuthModHost *host) {
    static wchar_t old[PATH_CAPACITY], pattern[PATH_CAPACITY], from[PATH_CAPACITY], to[PATH_CAPACITY];
    if (!path_join(old, host->mods_dir, RADIO_WIDE(RADIO_OLD_FILE)) || !path_join(pattern, old, L"*")) {
        return;
    }
    WIN32_FIND_DATAW found;
    HANDLE find = FindFirstFileW(pattern, &found);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        wchar_t *name = found.cFileName;
        if (same_ignore_case(name, L".") || same_ignore_case(name, L"..") || !path_join(from, old, name)) {
            continue;
        }
        if (name[0] == L'F' && name[1] == L'H' && name[2] == L'2' && name[3] == L'_') {
            for (wchar_t *c = name + 2; (c[0] = c[1]) != 0; ++c) {
            }
        }
        if (path_join(to, host->data_dir, name)) {
            MoveFileExW(from, to, 0);
        }
    } while (FindNextFileW(find, &found));
    FindClose(find);
    RemoveDirectoryW(old); // fails harmlessly while something is left in it
}

__declspec(dllexport) int AzimuthModLoad(const AzimuthModHost *host) {
    if (!host || host->size < sizeof(*host) || host->api_version != AZIMUTH_MOD_API_VERSION || !host->game_image) {
        return 0;
    }
    int fh3 = spotify_bridge_detect_game(host->game_image);
    CreateDirectoryW(host->data_dir, NULL); // fails harmlessly if it exists
    move_old_data(host);
    static wchar_t previous[PATH_CAPACITY]; // keep last session's log: problems get reported after a restart
    if (path_join(path, host->data_dir, RADIO_WIDE(RADIO_FILE) L".log") &&
        path_join(previous, host->data_dir, RADIO_WIDE(RADIO_FILE) L".previous.log")) {
        MoveFileExW(path, previous, MOVEFILE_REPLACE_EXISTING);
        mod_log_open(path, 1, RADIO_TAG);
        log_append(fh3 ? "FH3 " RADIO_TITLE " " RADIO_VERSION "\r\n" : "FH2 " RADIO_TITLE " " RADIO_VERSION "\r\n");
    }
    // Both radios hook the same station: a second hook on the same code would crash the game.
    static const wchar_t *const rivals[] = {
#ifdef LINE_IN_RADIO
        L"FH_SpotifyRadio.dll",
#endif
        L"FH2_SpotifyRadio.dll", L"FH2_LineInRadio.dll"};
    for (unsigned i = 0; i < ARRAY_COUNT(rivals); ++i) {
        if (path_join(path, host->mods_dir, rivals[i]) && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
            log_append(RADIO_TITLE " stays off: the mods folder has another radio DLL that plays the same station "
                                   "(an old FH2_ one, or Spotify Radio next to Line-in Radio). Keep one.\r\n");
            return 1;
        }
    }
#ifdef LINE_IN_RADIO
    line_in_menu_install(host->game_image, fh3);
#endif
    if (install_logo(host, fh3)) {
        spotify_bridge_mount_logo_folder(host->game_image);
    }
    track_controls_load(host->data_dir);
    spotify_bridge_install_controls(host->game_image);
    spotify_bridge_start(host->data_dir);
    spotify_bridge_apply_audio_hook(host->game_image);
    return 1;
}
