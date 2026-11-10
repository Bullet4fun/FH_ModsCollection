/* Extra Options' move of old add-ons into mods\Forza Horizon 2: add-on packages move, the loader
   mods' data folders stay, also when the mod's DLL is gone. */
#include "shared/extra_options.h"
#include "shared/support.h"
#include <stdio.h>
#include <stdlib.h>

static int fm5_mode, loader_requested;
static wchar_t mods_directory[PATH_CAPACITY];
static void report(const void *addon, const char *message) { (void)addon; (void)message; }
void log_append_value(const char *label, int value) { (void)label; (void)value; }
#include "shared/addon_layout.h"

static int failures;
#define CHECK(ok, what) do { if (!(ok)) { printf("FAIL: %s\n", what); ++failures; } } while (0)

static wchar_t root[MAX_PATH], path[MAX_PATH];

static const wchar_t *at(const wchar_t *relative) {
    swprintf(path, MAX_PATH, L"%lsmods\\%ls", root, relative);
    return path;
}
static void folder(const wchar_t *relative) { CreateDirectoryW(at(relative), NULL); }
static void file(const wchar_t *relative) {
    CloseHandle(CreateFileW(at(relative), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
}
static int exists(const wchar_t *relative) { return GetFileAttributesW(at(relative)) != INVALID_FILE_ATTRIBUTES; }

int main(void) {
    swprintf(root, MAX_PATH, L"%hs\\fh2-eo-layout-%lu\\", getenv("TEMP"), GetCurrentProcessId());
    CreateDirectoryW(root, NULL);
    CreateDirectoryW(at(L""), NULL);
    file(L"loadorder.txt");
    folder(L"Weather Control");
    file(L"Weather Control\\mod.json");
    folder(L"Loose Media");
    folder(L"Loose Media\\media");
    folder(L"FH_SpotifyRadio");
    file(L"FH_SpotifyRadio.dll");
    folder(L"FH_LineInRadio"); // its DLL was removed: Spotify Radio replaced it
    folder(L"FH_LineInRadio\\Songs");

    layout_reset();
    layout_configure(root);
    CHECK(addons_layout_prepare(), "the layout is prepared");
    CHECK(exists(L"Forza Horizon 2\\Weather Control\\mod.json"), "an add-on with a manifest moves");
    CHECK(exists(L"Forza Horizon 2\\Loose Media\\media"), "an add-on without a manifest moves");
    CHECK(exists(L"Forza Horizon 2\\loadorder.txt"), "the load order moves");
    CHECK(exists(L"FH_SpotifyRadio") && !exists(L"Forza Horizon 2\\FH_SpotifyRadio"), "a mod's data folder stays");
    CHECK(exists(L"FH_LineInRadio\\Songs") && !exists(L"Forza Horizon 2\\FH_LineInRadio"),
          "a removed mod's data folder stays");

    // only loader mods in mods\: their names must not end up in the add-on folder
    swprintf(root, MAX_PATH, L"%hs\\fh2-eo-layout-%lu-b\\", getenv("TEMP"), GetCurrentProcessId());
    CreateDirectoryW(root, NULL);
    CreateDirectoryW(at(L""), NULL);
    file(L"loadorder.txt");
    folder(L"FH_SpotifyRadio");
    file(L"FH_SpotifyRadio.dll");
    layout_reset();
    layout_configure(root);
    CHECK(addons_layout_prepare(), "the layout without add-ons is prepared");
    CHECK(!exists(L"Forza Horizon 2\\loadorder.txt"), "no load order of loader mods in the title folder");
    CHECK(!exists(L"loadorder.txt"), "the old load order is gone");
    if (failures) return 1;
    printf("all passed\n");
    return 0;
}
