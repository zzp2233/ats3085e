#pragma once

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lvgx lvgx_t;

lvgx_t *lvgx_init(void);
void lvgx_setup_window(lvgx_t *lvgx, lv_obj_t *parent);
void lvgx_process_events(void);
int lvgx_launch_applet(const char *applet_name);
void lvgx_applet_list(lv_obj_t *parent);
bool lvgx_applet_actived(void);
#ifdef __cplusplus
}
#endif
