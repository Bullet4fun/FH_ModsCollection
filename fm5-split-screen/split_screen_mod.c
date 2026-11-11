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

static void **factory_slot;
static uintptr_t image_size;

/* Puts factory_hook in the slot in place of resolved, Azimuth's function. */
static int hook_import(void *resolved) {
    DWORD protection;
    if (!VirtualProtect(factory_slot, sizeof(void *), PAGE_READWRITE, &protection)) {
        return 0;
    }
    original_factory = (FactoryFn)resolved;
    int hooked = InterlockedCompareExchangePointer(factory_slot, (void *)factory_hook, resolved) == resolved;
    VirtualProtect(factory_slot, sizeof(void *), protection, &protection);
    return hooked;
}

/* Mods load before Azimuth resolves the game's imports: until then the slot holds the import's name
   (an offset into the image), and resolving overwrites anything written there. So this thread waits
   for Azimuth's pointer, puts the hook in its place at once (the game is only starting), and keeps
   an eye on the slot afterwards. */
static DWORD WINAPI watch_thread(void *unused) {
    (void)unused;
    ULONGLONG start = GetTickCount64();
    int hooked = 0, warned = 0;
    for (unsigned tick = 0;; ++tick) {
        void *value = *(void *volatile *)factory_slot;
        if (value != (void *)factory_hook && (uintptr_t)value > image_size && hook_import(value)) {
            WrapLine line;
            line.length = 0;
            line_add(&line, hooked ? "WARNING: the factory import was written again; back through the mod"
                                   : "Azimuth resolved the game's factory import; it passes through the mod after ");
            if (!hooked) {
                line_add_number(&line, GetTickCount64() - start);
                line_add(&line, " ms");
            }
            line_log(&line);
            hooked = 1;
        }
        if (!hooked && GetTickCount64() - start < 120000) {
            Sleep(0); // the game starts right after resolving: catch the pointer within microseconds
            continue;
        }
        if (!hooked && !warned) {
            warned = 1;
            log_append("WARNING: Azimuth never resolved the game's factory import; the mod stays off\r\n");
        }
        Sleep(100);
        if (tick % 20 == 0) {
            wrap_report();
        }
        if (hooked) {
            players_tick();
        }
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
    factory_slot = import_slot(host->game_image, "vccorlib110.dll",
                               "?GetActivationFactoryByPCWSTR@@YAJPEAXAEAVGuid@Platform@@PEAPEAX@Z");
    if (!factory_slot) {
        log_append("WARNING: the game has no factory import where it should be; the mod stays off\r\n");
        return 1;
    }
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)host->game_image;
    image_size = ((IMAGE_NT_HEADERS64 *)(host->game_image + dos->e_lfanew))->OptionalHeader.SizeOfImage;
    players_init();
    players_strings(import_slot(host->game_image, "combase.dll", "WindowsCreateString"),
                    import_slot(host->game_image, "combase.dll", "WindowsGetStringRawBuffer"),
                    import_slot(host->game_image, "combase.dll", "WindowsDeleteString"));
    HANDLE thread = CreateThread(NULL, 0, watch_thread, NULL, 0, NULL);
    if (!thread) {
        log_append("WARNING: cannot start the mod's thread; the mod stays off\r\n");
        return 1;
    }
    CloseHandle(thread);
    return 1;
}
