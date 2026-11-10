/* Spotify mod: the logo goes into Mount\spotifyradio\ as the Xbox Music icon, and a logo this mod
   recorded somewhere else is removed. */
#include "spotify_bridge.c"
#include "spotify_mod.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); fails++; } } while (0)

int main(void) {
    wchar_t root[MAX_PATH], data[MAX_PATH], exe[MAX_PATH], hud[MAX_PATH], old[MAX_PATH], f[MAX_PATH];
    swprintf(root, MAX_PATH, L"%hs\\fh2-logo-test-%lu", getenv("TEMP"), GetCurrentProcessId());
    swprintf(data, MAX_PATH, L"%ls\\mods\\FH_SpotifyRadio\\", root);
    swprintf(exe, MAX_PATH, L"%ls\\Mount\\game.exe", root);
    swprintf(hud, MAX_PATH, L"%ls\\Mount\\spotifyradio\\ui\\textures\\Anthem\\HUD\\", root);
    swprintf(old, MAX_PATH, L"%ls\\Mount\\HUD_RadioIcon_Xbox_Music.swatchbin", root);
    CreateDirectoryW(root, NULL);
    swprintf(f, MAX_PATH, L"%ls\\mods", root); CreateDirectoryW(f, NULL); CreateDirectoryW(data, NULL);
    swprintf(f, MAX_PATH, L"%ls\\Mount", root); CreateDirectoryW(f, NULL);
    AzimuthModHost host = {sizeof(host), AZIMUTH_MOD_API_VERSION, NULL, exe, root, data, AZIMUTH_GAME_FORZA_HORIZON_2};
#define AT(dir, name) (swprintf(f, MAX_PATH, L"%ls%ls", dir, name), f)
#define EXISTS(dir, name) (GetFileAttributesW(AT(dir, name)) != INVALID_FILE_ATTRIBUTES)
    CHECK(!install_logo(&host, 0) && !EXISTS(hud, L"HUD_RadioIcon_Xbox_Music.swatchbin"), "no logo file: nothing placed");

    FILE *w = _wfopen(AT(data, L"HUD_RadioIcon_Spotify.swatchbin"), L"wb"); fputs("logo", w); fclose(w);
    w = _wfopen(old, L"wb"); fputs("logo", w); fclose(w); /* placed elsewhere last time, and recorded */
    w = _wfopen(AT(data, L"logo_installed.txt"), L"wb"); fwrite(old, sizeof(wchar_t), wcslen(old), w); fclose(w);
    CHECK(install_logo(&host, 0) && EXISTS(hud, L"HUD_RadioIcon_Xbox_Music.swatchbin") &&
              GetFileAttributesW(old) == INVALID_FILE_ATTRIBUTES,
          "logo in spotifyradio, the recorded old copy removed");
    CHECK(install_logo(&host, 0) && EXISTS(hud, L"HUD_RadioIcon_Xbox_Music.swatchbin"), "again: still there");

    /* Forza Horizon 3: its own file replaces Groove Music's logo, and neither game removes the other's. */
    CHECK(!install_logo(&host, 1), "no Forza Horizon 3 logo file: nothing placed");
    w = _wfopen(AT(data, L"HUD_RadioLogo_Spotify_FH3.swatchbin"), L"wb"); fputs("logo", w); fclose(w);
    CHECK(install_logo(&host, 1) && EXISTS(hud, L"RadioLogos\\Groove_Music.swatchbin") &&
              EXISTS(hud, L"HUD_RadioIcon_Xbox_Music.swatchbin"),
          "Forza Horizon 3 logo placed, the Forza Horizon 2 logo kept");

    /* An older install's folder: its files move and lose the FH2_, what the zip brought stays new. */
    wchar_t mods[MAX_PATH], older[MAX_PATH];
    swprintf(mods, MAX_PATH, L"%ls\\mods", root);
    swprintf(older, MAX_PATH, L"%ls\\mods\\FH2_SpotifyRadio\\", root);
    CreateDirectoryW(older, NULL);
    AzimuthModHost moved = host;
    moved.mods_dir = mods;
    w = _wfopen(AT(older, L"FH2_SpotifyRadio.ini"), L"wb"); fputs("settings", w); fclose(w);
    w = _wfopen(AT(older, L"credentials.json"), L"wb"); fputs("login", w); fclose(w);
    w = _wfopen(AT(older, L"librespot.exe"), L"wb"); fputs("old", w); fclose(w);
    w = _wfopen(AT(data, L"librespot.exe"), L"wb"); fputs("new", w); fclose(w);
    CreateDirectoryW(AT(older, L"Songs"), NULL);
    move_old_data(&moved);
    CHECK(EXISTS(data, L"FH_SpotifyRadio.ini") && EXISTS(data, L"credentials.json") && EXISTS(data, L"Songs"),
          "an older install's files move");
    char kept[8] = {0};
    w = _wfopen(AT(data, L"librespot.exe"), L"rb"); fread(kept, 1, 3, w); fclose(w);
    CHECK(!strcmp(kept, "new"), "a file the zip brought stays the new one");

    wchar_t cmd[MAX_PATH + 32]; swprintf(cmd, MAX_PATH + 32, L"cmd /c rmdir /s /q \"%ls\"", root); _wsystem(cmd);
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
