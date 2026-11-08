/* SPDX-License-Identifier: MPL-2.0 */
/* DUBWISE: the app's half of the contract with fm1-base's platform (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "DUBWISE"
#define APP_SPLASH_RGB RGB(250, 200, 40)
#define APP_TAGLINE "a dub sound system"
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_BLE_AT_BOOT 0

/* the store: the app's settings, the panel calibration and Bluetooth's record (the platform's; DUBWISE builds
 * without Bluetooth, the slot is kept as fm1-base's template has it), and their flash offsets */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* the host simulator's checks (host/plat_host.c "expect") */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
