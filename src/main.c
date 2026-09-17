/* Test Drive Enhanced — entry point.
 *
 * usage: testdrive-enhanced [--game-dir DIR] [--scale N] [--res-scale N] [--frame-rate FPS] [--bios-keys] [--check]
 *   --res-scale  output resolution as a multiple of 320x200 (default 4, 1..8)
 *   --frame-rate drawing rate while driving (default 60; 0 = as fast as possible)
 *   --bios-keys driving keys act only through key repeat, exactly like the original (default: held keys)
 *   --game-dir  folder with the original game files (default: "Game" next to the working directory)
 *   --scale     initial window scale (default 3)
 *   --check     load and verify TDEGA.EXE, print a summary and exit (no window)
 */
#define SDL_MAIN_HANDLED
#if defined(__psp2__) || defined(__vita__)
#include <SDL2/SDL.h>
#include <psp2/kernel/processmgr.h>
#include <sys/stat.h>
int _newlib_heap_size_user = 32 * 1024 * 1024; /* 32 MB user heap */
#elif __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "mem.h"
#include "enhanced/enhanced.h"
#include "platform/gfx.h"

int game_main(void);   /* game/flow.c: port of main() at image 0x0010 */

int main(int argc, char **argv)
{
#if defined(__psp2__) || defined(__vita__)
    const char *dir = "ux0:data/TestDrive";
    int scale = 1, res_scale = 2;
    mkdir("ux0:data", 0777);
    mkdir("ux0:data/TestDrive", 0777);
    struct stat st;
    if (stat("ux0:data/TestDrive/TDEGA.EXE", &st) == 0 || stat("ux0:data/TestDrive/tdega.exe", &st) == 0) {
        dir = "ux0:data/TestDrive";
    } else if (stat("app0:Game/TDEGA.EXE", &st) == 0 || stat("app0:Game/tdega.exe", &st) == 0) {
        dir = "app0:Game";
    } else {
        dir = "ux0:data/TestDrive";
    }
#else
    const char *dir = "Game";
    int scale = 3, res_scale = 4;
#endif
    bool check = false, rate_set = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--game-dir") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--res-scale") && i + 1 < argc) res_scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--check")) check = true;
        else if (!strcmp(argv[i], "--bios-keys")) host_set_held_keys(false);
        else if (!strcmp(argv[i], "--frame-rate") && i + 1 < argc) { host_set_frame_rate(atoi(argv[++i])); rate_set = true; }
        else {
            fprintf(stderr, "usage: %s [--game-dir DIR] [--scale N] [--res-scale N] [--frame-rate FPS] [--bios-keys] [--check]\n", argv[0]);
            return 2;
        }
    }

    char exe_path[1024];
    snprintf(exe_path, sizeof exe_path, "%s/TDEGA.EXE", dir);
    char err[256];
    if (!mem_load_exe(exe_path, err, sizeof err)) {
        /* Try lowercase */
        snprintf(exe_path, sizeof exe_path, "%s/tdega.exe", dir);
        if (!mem_load_exe(exe_path, err, sizeof err)) {
            fprintf(stderr, "%s\n", err);
#if defined(__psp2__) || defined(__vita__)
            char msg[512];
            snprintf(msg, sizeof msg,
                "Could not find Test Drive game data!\n\n"
                "Please copy original DOS game files\n"
                "(TDEGA.EXE and all assets) into:\n"
                "ux0:data/TestDrive/\n\n"
                "Details: %s", err);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Test Drive Vita", msg, NULL);
#else
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Test Drive", err, NULL);
#endif
            return 1;
        }
    }
    if (check) {
        printf("TDEGA.EXE ok: image %u bytes at %04X:0000, DGROUP %04X\n", mem_image_size, LOAD_SEG, DGROUP);
        return 0;
    }

    if (!host_init(dir, scale)) return 1;
    gfx_set_output_scale(res_scale);
    enh_init();
    if (!rate_set) host_set_frame_rate(60);
    int rc = game_main();
    host_shutdown();
    return rc;
}
