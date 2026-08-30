#ifndef GUI_SETTINGS_H
#define GUI_SETTINGS_H

void app_settings_init(void);

/* Apply persisted sound config (volume/rate) from SDFS. Call after the
 * SDFS mount so the AC'97 mixer picks up the saved values at boot. */
void sound_config_apply(void);

#endif
