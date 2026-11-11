/* FM5 Split-Screen (mods\FM5_SplitScreen.dll): a second signed-in player for Forza Motorsport 5's
   split-screen. Azimuth hands the game every WinRT factory through one import, vccorlib's
   GetActivationFactoryByPCWSTR; this mod points the game's import at its own function, which wraps
   the user and controller factories (players.c). Azimuth's own code and data stay untouched. */
#include "azimuth_mod.h"
#include "logging.h"
#include "mod_log.h"
#include "players.h"
#include "support.h"

AZIMUTH_MOD_GAMES(AZIMUTH_GAME_FORZA_MOTORSPORT_5);

typedef HRESULT (*FactoryFn)(const wchar_t *class_name, const GUID *iid, void **factory);
static FactoryFn original_factory;

static HRESULT factory_hook(const wchar_t *class_name, const GUID *iid, void **factory) {
    HRESULT result = original_factory(class_name, iid, factory);
    if (SUCCEEDED(result) && factory && *factory) {
        *factory = players_factory(class_name, iid, *factory);
    }
    return result;
}

/* The game's import address table entry for dll!name, found through its import directory. */
static void **import_slot(unsigned char *image, const char *dll, const char *name) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return NULL;
    }
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(image + dos->e_lfanew);
    DWORD size = nt->OptionalHeader.SizeOfImage;
    IMAGE_DATA_DIRECTORY *directory = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (nt->Signature != IMAGE_NT_SIGNATURE || !directory->VirtualAddress || directory->VirtualAddress >= size) {
        return NULL;
    }
    for (IMAGE_IMPORT_DESCRIPTOR *import = (IMAGE_IMPORT_DESCRIPTOR *)(image + directory->VirtualAddress);
         import->Name && import->Name < size; ++import) {
        if (!text_equal_ignore_case((const char *)image + import->Name, dll) || !import->OriginalFirstThunk ||
            import->OriginalFirstThunk >= size || import->FirstThunk >= size) {
            continue;
        }
        IMAGE_THUNK_DATA64 *names = (IMAGE_THUNK_DATA64 *)(image + import->OriginalFirstThunk);
        for (size_t i = 0; names[i].u1.AddressOfData; ++i) {
            ULONGLONG entry = names[i].u1.AddressOfData;
            if (IMAGE_SNAP_BY_ORDINAL64(entry) || entry + 2 >= size) {
                continue;
            }
            const char *found = (const char *)image + entry + 2; // past the hint
            size_t length = text_length(name);
            if (memory_equal(found, name, length + 1)) {
                return (void **)(image + import->FirstThunk) + i;
            }
        }
    }
    return NULL;
}

static DWORD WINAPI trace_thread(void *unused) {
    (void)unused;
    for (;;) {
        Sleep(2000);
        wrap_report();
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}

__declspec(dllexport) int AzimuthModLoad(const AzimuthModHost *host) {
    if (!host || host->size < sizeof(*host) || host->api_version != AZIMUTH_MOD_API_VERSION || !host->game_image) {
        return 0;
    }
    if (host->game != AZIMUTH_GAME_FORZA_MOTORSPORT_5) {
        return 1;
    }
    static wchar_t log_path[PATH_CAPACITY];
    CreateDirectoryW(host->data_dir, NULL);
    if (path_join(log_path, host->data_dir, L"FM5_SplitScreen.log")) {
        mod_log_open(log_path, 1, "SplitScreen");
    }
    log_append("FM5 Split-Screen " SPLIT_SCREEN_VERSION " by Bullet4fun: https://github.com/Bullet4fun/FH_ModsCollection\r\n");
    void **slot = import_slot(host->game_image, "vccorlib110.dll",
                              "?GetActivationFactoryByPCWSTR@@YAJPEAXAEAVGuid@Platform@@PEAPEAX@Z");
    DWORD protection;
    if (!slot || !*slot || !VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) {
        log_append("WARNING: the game's factory import is not where it should be; the mod stays off\r\n");
        return 1;
    }
    players_init();
    original_factory = (FactoryFn)*slot;
    *slot = (void *)factory_hook;
    VirtualProtect(slot, sizeof(*slot), protection, &protection);
    log_append("The game's WinRT factories now pass through the mod\r\n");
    HANDLE thread = CreateThread(NULL, 0, trace_thread, NULL, 0, NULL);
    if (thread) {
        CloseHandle(thread);
    }
    return 1;
}
