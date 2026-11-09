#include "shared/boot_diagnostics.h"
#include "fh2/runtime.h"
#include "fh2/features.h"
#include "shared/addons.h"
#include "shared/plugins.h"
#include "fh2/video_settings.h"
#include "shared/logging.h"
#include "fh2/patch_engine.h"
#include "fh2/settings.h"
#include "shared/support.h"

#ifdef FH2_AZIMUTH
#include "shared/adapters/azimuth_markers.h"
#endif

int runtime_initialize(unsigned char *game_image, const wchar_t *executable_path,
                       const wchar_t *settings_directory) {
    ModOptions options;
    settings_defaults(&options);
    addons_configure(executable_path);
    log_reset();
    int initialized = settings_read(settings_directory, &options);
    if (initialized) {
        video_settings_load(settings_directory);
        video_settings_seed(&options);
        video_settings_apply(&options);
        settings_apply_switches(&options);
        video_settings_show_features(&options);
    }
    if (initialized && options.enabled) {
        uint64_t begin=boot_diag_now();initialized = verify_game_file(executable_path);
        boot_diag_end("boot.verify_game_executable",NULL,begin);
    }
    static wchar_t cache_ini[PATH_CAPACITY];
    int cache=path_join(cache_ini,settings_directory,MOD_INI_NAME) &&
              GetPrivateProfileIntW(L"Mods",L"DatabaseCache",1,cache_ini)!=0;
    addons_cache_configure(settings_directory,cache,0);
    if (initialized && options.enabled) addons_layout_prepare();
    plugins_configure(game_image, executable_path, initialized && options.enabled);
    if (initialized) {
        uint64_t begin=boot_diag_now();initialized = patch_engine_apply(game_image, &options);
        boot_diag_end("boot.FH2_prepare_apply_hooks",NULL,begin);
    }
    log_start(settings_directory, &options);
#ifdef FH2_AZIMUTH
    if (initialized && options.enabled) {
        log_set_directory(settings_directory);
        if (options.generate_missing_save_commits){
            uint64_t begin=boot_diag_now();azimuth_repair_missing_markers((unsigned char *)GetModuleHandleW(NULL));
            boot_diag_end("boot.generate_missing_save_commits",NULL,begin);
        }
    }
#endif
    if (initialized && options.enabled) {
        log_set_directory(settings_directory); // later lines: only what failed (the settings are above)
    }
    return initialized;
}
