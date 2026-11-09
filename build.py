"""Build the mods with Zig 0.15.2, including cross-compilation on Linux.
Usage: python build.py /path/to/zig (--mod | --spotify | --discord | --line-in) [--output PATH]
The mod loader (Azimuth Mod Loader, dinput8.dll) lives in its own repo, Azimuth_ModLoader.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parent
COMMON_SOURCES = [
    "extra-options/shared/boot_diagnostics.c",
    "extra-options/fh2/dllmain.c",
    "extra-options/fh2/runtime.c",
    "extra-options/fh2/settings.c",
    "extra-options/shared/support.c",
    "extra-options/shared/logging.c",
    "extra-options/fh2/patch_engine.c",
    "extra-options/fh2/verification.c",
    "extra-options/shared/sha256.c",
    "extra-options/fh2/patches_generated.c",
    "extra-options/fh2/garage.c",
    "extra-options/fh2/road_placement.c",
    "extra-options/fh2/treasure_map.c",
    "extra-options/fh2/minimap.c",
    "extra-options/fh2/rims.c",
    "extra-options/fh2/engine_swaps.c",
    "extra-options/fh2/transmission_swaps.c",
    "extra-options/fh2/return_garage.c",
    "extra-options/fh2/stock_wheels.c",
    "extra-options/fh2/wheel_variants.c",
    "extra-options/fh2/world_drivatars.c",
    "extra-options/fh2/local_liveries.c",
    "extra-options/fh2/designs.c",
    "extra-options/shared/plugins.c",
    "extra-options/fh2/display_units.c",
    "extra-options/fh2/drift_camera.c",
    "extra-options/fh2/video_settings.c",
    "extra-options/fh2/camera_fov.c",
    "extra-options/fh2/video_options.c",
    "extra-options/shared/video_options_text.c",
    "extra-options/fh2/video_options_xaml.c",
    "extra-options/fh2/photo_sampling.c",
    "extra-options/fh2/sun_shadows.c",
    "extra-options/fh2/livery.c",
    "extra-options/fh2/media_overrides.c",
    "extra-options/shared/media_files.c",
    "extra-options/fh2/video_redirect.c",
    "extra-options/fh2/showroom_photo.c",
    "extra-options/fh2/manufacturer_jump.c",
    "extra-options/fh2/presentation_controls.c",
    "extra-options/fh2/driver_options.c",
    "extra-options/fh2/photo_environment.c",
    "extra-options/fh2/audio_volume.c",
    "extra-options/fh2/pause_menu.c",
    "extra-options/shared/addon_json.c",
    "extra-options/shared/carbin_patch.c",
    "extra-options/shared/addon_database.c",
    "extra-options/shared/addon_cache.c", "extra-options/shared/addon_cache_io.c",
    "extra-options/shared/addons.c",
    "extra-options/shared/addon_archives.c",
    "extra-options/shared/addon_strings.c",
    "extra-options/shared/addon_wheels.c",
    "extra-options/fh2/vehicle_controls.c",
    "extra-options/fh2/headlight_beams.c",
    "extra-options/fh2/blinkers.c",
    "extra-options/fh2/aero_tuning.c",
    "extra-options/fh2/roof_fade.c",
    "extra-options/fh2/roof_aero.c",
    "extra-options/fh2/speed_zone.c",
    "extra-options/fh2/autodrive.c",
    "extra-options/fh2/game_input.c",
    "extra-options/fh2/bindings.c",
    "extra-options/fh2/offline_events.c",
    "extra-options/fh2/custom_showcases.c",
    "extra-options/fh2/offline_event_division.c",
    "extra-options/fh2/offline_event_setup.c",
    "extra-options/fh2/autodrive_game.c",
    "extra-options/fh2/autodrive_notice.c",
    "extra-options/fh2/autodrive_ui.c",
    "extra-options/fh2/ui_controls.c",
    "extra-options/shared/adapters/mod.c",
    "extra-options/shared/adapters/azimuth_host.c",
    "extra-options/shared/adapters/azimuth_markers.c",
    "common/mod_log.c",
]
FM5_SOURCES = ["extra-options/fm5/fm5.c", "extra-options/fm5/fm5_mods.c", "extra-options/fm5/fm5_carbin.c", "extra-options/fm5/fm5_rims.c", "extra-options/fm5/fm5_camera.c",
               "extra-options/fm5/fm5_camera_menu.c", "extra-options/fm5/fm5_manufacturer.c", "extra-options/fm5/fm5_video.c", "extra-options/fm5/fm5_vehicle.c", "extra-options/fm5/fm5_ui.c", "extra-options/fm5/fm5_settings.c", "extra-options/fm5/fm5_driver.c", "extra-options/fm5/fm5_audio.c", "extra-options/fm5/fm5_badge.c", "extra-options/fm5/fm5_start.c", "extra-options/fm5/fm5_designs.c", "extra-options/fm5/fm5_local_liveries.c"]
FH3_SOURCES = ["extra-options/fh3/fh3.c", "extra-options/fh3/fh3_mods.c", "extra-options/fh3/fh3_text.c"]
# Separate mods for the loader; each is its own DLL in data\bin\mods\.
DISCORD_SOURCES = ["discord-rpc/discord_mod.c", "discord-rpc/discord_rpc.c", "common/support.c", "common/mod_log.c"]
SPOTIFY_SOURCES = ["spotify-radio/spotify_mod.c", "common/mod_log.c", "spotify-radio/spotify_bridge.c", "spotify-radio/track_controls.c",
                   "spotify-radio/pinyin_table.c", "spotify-radio/unidecode_table.c", "common/support.c"]
# Line-in Radio is Spotify Radio's bridge built with LINE_IN_RADIO: a sound device instead of librespot.
LINE_IN_SOURCES = ["line-in-radio/line_in_capture.c", "line-in-radio/line_in_songs.c", "line-in-radio/line_in_player.c",
                   "line-in-radio/line_in_menu.c",
                   *SPOTIFY_SOURCES]
INCLUDE_DIRS = ["common", "sdk", "extra-options", "spotify-radio", "discord-rpc"]
SPOTIFY_VERSION = "1.4.2"
DISCORD_VERSION = "1.4.1"
LINE_IN_VERSION = "1.0.2"


def version_resource(compiler, directory, filename, description, product, version):
    """A VERSIONINFO resource for the DLL (file description, version, license), compiled by zig rc:
    an unsigned DLL without one looks more suspicious to antivirus heuristics."""
    numbers = ",".join(version.split(".") + ["0"])
    strings = {"CompanyName": "Bullet (FH2 mods)", "FileDescription": description, "FileVersion": version,
               "InternalName": filename.rsplit(".", 1)[0], "LegalCopyright": "GPL-3.0-or-later",
               "OriginalFilename": filename, "ProductName": product, "ProductVersion": version}
    values = "\n".join(f'            VALUE "{key}", "{value}"' for key, value in strings.items())
    script = directory / "version.rc"
    script.write_text(f"""1 VERSIONINFO
FILEVERSION {numbers}
PRODUCTVERSION {numbers}
FILEOS 0x40004
FILETYPE 0x2
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904B0"
        BEGIN
{values}
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
""", encoding="utf-8")
    resource = directory / "version.res"
    subprocess.run([str(compiler), "rc", "/fo", str(resource), str(script)], check=True)
    return str(resource)


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("zig", type=Path)
    parser.add_argument("--mod", action="store_true",
                        help="Build ExtraOptions as data/bin/mods/FH_ExtraOptions.dll for the loader")
    parser.add_argument("--discord", action="store_true", help="Build the Discord Rich Presence mod")
    parser.add_argument("--spotify", action="store_true", help="Build the Spotify radio mod")
    parser.add_argument("--line-in", action="store_true", help="Build the Line-in radio mod")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--build-dir", type=Path)
    arguments = parser.parse_args()
    if sum((arguments.mod, arguments.discord, arguments.spotify, arguments.line_in)) != 1:
        parser.error("choose one of --mod, --discord, --spotify, --line-in")
    return arguments


def compiler_arguments(compiler, options):
    arguments = [
        str(compiler),
        "clang",
        "-target",
        "x86_64-w64-windows-gnu",
        "-c",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-stack-protector",
        "-fms-extensions",
        "-funwind-tables",
        "-ffunction-sections",  # with /opt:ref below: a mod ships only the code it calls
        "-fdata-sections",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        *(argument for folder in INCLUDE_DIRS for argument in ("-I", str(SOURCE / folder))),
        "-isystem",
        str(compiler.parent / "lib/include"),
        "-isystem",
        str(compiler.parent / "lib/libc/include/any-windows-any"),
    ]
    if options.mod:
        arguments += ["-DFH2_AZIMUTH=1", "-DFH2_ENABLE_FM5=1", "-DEO_ENABLE_FH3=1"]
    if options.spotify:
        arguments.append(f'-DSPOTIFY_VERSION="{SPOTIFY_VERSION}"')  # the log's first line
    if options.discord:
        arguments.append(f'-DDISCORD_VERSION="{DISCORD_VERSION}"')  # its line in the loader's log
    if options.line_in:  # here, not in INCLUDE_DIRS: the command line is in the debug info, so the other DLLs stay as they were
        arguments += ["-I", str(SOURCE / "line-in-radio"), "-DLINE_IN_RADIO=1", f'-DLINE_IN_VERSION="{LINE_IN_VERSION}"']
    return arguments


def describe(options):
    """(file description, product, version) for the VERSIONINFO resource."""
    if options.spotify:
        return "Spotify radio for Forza Horizon 2 (Azimuth Mod Loader mod)", "FH Mods Collection", SPOTIFY_VERSION
    if options.discord:
        return "Discord Rich Presence for Azimuth games (Azimuth Mod Loader mod)", "FH Mods Collection", DISCORD_VERSION
    if options.line_in:
        return "Line-in radio for Forza Horizon 2 (Azimuth Mod Loader mod)", "FH Mods Collection", LINE_IN_VERSION
    header = (SOURCE / "extra-options/shared/extra_options.h").read_text(encoding="utf-8")
    version = header.split('#define MOD_VERSION "', 1)[1].split('"', 1)[0]  # ExtraOptions' own version
    return "Forza Horizon 2, Forza Horizon 3 and Forza Motorsport 5 Extra Options by RokkuDayo (Azimuth Mod Loader mod)", "Forza Extra Options", version

def build(compiler, output, directory, options):
    directory.mkdir(parents=True, exist_ok=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    objects = []
    if options.discord:
        sources = DISCORD_SOURCES
    elif options.spotify:
        sources = SPOTIFY_SOURCES
    elif options.line_in:
        sources = LINE_IN_SOURCES
    else:  # --mod: the loader owns Azimuth's helper ABI and finds the game
        sources = [*COMMON_SOURCES, *FM5_SOURCES, *FH3_SOURCES]
    arguments = compiler_arguments(compiler, options)
    # Every step runs in the build folder with relative names, and source paths are mapped to ".":
    # file paths end up in the PDB id, so where the repo or the build folder lives must not matter.
    path_maps = [f"-ffile-prefix-map={SOURCE}=.", f"-ffile-prefix-map={compiler.parent}=zig"]
    for filename in sources:
        object_file = Path(filename).stem + ".obj"
        command = ([str(compiler), "clang", "-target", "x86_64-w64-windows-gnu", "-c"]
                   if filename.endswith(".s") else arguments)
        subprocess.run([*command, *path_maps, str(SOURCE / filename), "-o", object_file], check=True, cwd=directory)
        objects.append(object_file)
    import_library = "kernel32.lib"
    subprocess.run(
        [
            str(compiler),
            "dlltool",
            "-m",
            "i386:x86-64",
            "-d",
            str(SOURCE / ("extra-options/shared" if options.mod else "common") / "kernel32.def"),
            "-l",
            import_library,
        ],
        check=True,
        cwd=directory,
    )
    exception_library = "ntdll.lib"
    subprocess.run([str(compiler), "dlltool", "-m", "i386:x86-64", "-d", str(SOURCE / "common/ntdll.def"),
                    "-l", exception_library], check=True, cwd=directory)
    # keyboard, focus and socket calls imported by name: resolving them through GetProcAddress
    # strings looks like a hidden keylogger or backdoor to antivirus ML engines
    system_libraries = []
    # Line-in Radio: ole32 for the sound devices (WASAPI), mfplat/mfreadwrite to decode songs, shell32 for their tags,
    # combase for the players' media sessions (WinRT)
    line_in_libraries = ["ole32", "mfplat", "mfreadwrite", "shell32", "combase"] if options.line_in else []
    for name in ("user32", "ws2_32", *line_in_libraries):
        system_libraries.append(f"{name}.lib")
        subprocess.run([str(compiler), "dlltool", "-m", "i386:x86-64", "-d", str(SOURCE / f"common/{name}.def"),
                        "-l", system_libraries[-1]], check=True, cwd=directory)
    # /release (a PE checksum) and a debug directory keep antivirus ML engines quiet; the PDB
    # stays in the build folder.
    link_arguments = [
        str(compiler),
        "lld-link",
        "/dll",
        "/entry:DllMain",
        "/machine:x64",
        "/nodefaultlib",
        "/dynamicbase",
        "/nxcompat",
        "/out:" + output.name,  # copied out below: the output path is in the PDB id
        "/implib:extra_options.lib",
        *objects,
        import_library,
        exception_library,
        *system_libraries,
        Path(version_resource(compiler, directory, output.name, *describe(options))).name,
        "/release",
        "/debug",
        "/opt:ref",  # /debug alone keeps unused functions (and their imports) in the DLL
        "/pdb:" + output.with_suffix(".pdb").name,
        "/pdbaltpath:%_PDB%",
        "/Brepro",  # PDB id from the content, not random
        "/pdbsourcepath:C:\\build",  # not the build folder's real path
        # the last commit's time instead of the clock: the same source gives the same bytes, so the
        # file scanned before a release is the file shipped
        "/timestamp:" + subprocess.run(["git", "-c", "safe.directory=*", "log", "-1", "--format=%ct"],
                                       cwd=SOURCE, capture_output=True, text=True, check=True).stdout.strip(),
    ]
    subprocess.run(link_arguments, check=True, cwd=directory)
    shutil.copyfile(directory / output.name, output)


def main():
    options = parse_arguments()
    compiler = options.zig.resolve()
    filename = ("FH_ExtraOptions.dll" if options.mod
                else "FH_DiscordRPC.dll" if options.discord
                else "FH2_LineInRadio.dll" if options.line_in
                else "FH2_SpotifyRadio.dll")
    output = (options.output or SOURCE.parent / filename).resolve()
    if options.build_dir:
        build(compiler, output, options.build_dir.resolve(), options)
    else:  # a fixed folder: object paths end up in the PDB id, so a random one changes the DLL
        directory = Path(tempfile.gettempdir()) / f"fh2-build-{Path(filename).stem}"
        shutil.rmtree(directory, ignore_errors=True)
        try:
            build(compiler, output, directory, options)
        finally:
            shutil.rmtree(directory, ignore_errors=True)
    print(output)


if __name__ == "__main__":
    main()
