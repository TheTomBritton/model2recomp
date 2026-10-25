/*
 * Virtua Cop - browser launcher.
 *
 * Sega Model 2, 1994: i960KB, MB86233 TGP, and the Model 1 I/O board 2
 * behind the dual-port RAM, with the light guns on that board's FPGA. The
 * page writes the region images a player's own vcop.zip produces
 * (tools/mame_romset.py's layout) into MEMFS at /roms and calls main;
 * everything after that is the corpus launcher's boot chain, with this
 * title's gun range and board variant filled in.
 *
 *   vcop [rom_dir]      MODEL2_MAX_FRAMES caps a headless run
 */

#include "model2recomp/model2recomp.h"
#include "model2recomp/i960.h"
#include "model2recomp/bus.h"
#include "model2recomp/func_table.h"
#include "model2recomp/io.h"
#include "game/functions.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* A browser tab has no quit to save the cabinet's settings on, so the page
 * calls this every few seconds and keeps the file across visits. */
EMSCRIPTEN_KEEPALIVE void web_save_nvram(void) { model2recomp_save_nvram(); }

int main(int argc, char *argv[])
{
    const char *rom_dir = (argc > 1) ? argv[1] : "/roms";

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (!model2recomp_init("Virtua Cop", 1, MODEL2_ORIGINAL)) {
        fprintf(stderr, "init failed\n");
        return 1;
    }
    /* The backup RAM the game keeps its bookkeeping and rankings in, if the
     * page kept it from a last visit. */
    model2recomp_set_nvram("vcop.nv");
    /* MAME's PORT_MINMAX for this title's guns (model2.cpp, vcop). */
    io_set_gun_range(0x083, 0x276, 0x024, 0x1A9);

    if (!model2recomp_load_rom(rom_dir)) {
        fprintf(stderr, "no ROM images in %s\n", rom_dir);
        model2recomp_shutdown();
        return 1;
    }

    game_register_all();

    const char *cap = getenv("MODEL2_MAX_FRAMES");
    model2recomp_set_frame_limit(cap ? strtol(cap, NULL, 10) : 0);

    /* Boot the i960 the way the hardware does: the initialization boot record
     * gives the PRCB and the first instruction, the reset stub relocates the
     * PRCB and issues an IAC reinitialize that hands over to the firmware. */
    uint32_t prcb = bus_read32(0x00000004);
    uint32_t ip   = bus_read32(0x0000000C);

    for (int hop = 0; ip != 0 && hop < 8; hop++) {
        I960_FP = bus_read32(prcb + 0x18);
        I960_SP = I960_FP + 0x40;
        g_i960.IP = ip;
        printf("[vcop] boot %d: IP 0x%08X PRCB 0x%08X FP 0x%08X\n", hop, ip, prcb, I960_FP);
        if (!func_table_call(ip)) {
            fprintf(stderr, "[vcop] entry 0x%08X not registered\n", ip);
            break;
        }
        ip = bus_iac_take_reinit(&prcb);
    }

    printf("[vcop] guest returned; idling\n");
    while (model2recomp_begin_frame()) {
        model2recomp_trigger_vblank();
        model2recomp_end_frame();
    }
    model2recomp_shutdown();
    return 0;
}
