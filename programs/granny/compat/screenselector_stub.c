/*
 * Unity's optional ScreenSelector plugin is a GTK2 desktop helper. GTK2 is
 * not part of LiwusOS. Unity resolves the plugin's exported callbacks during
 * startup; this loadable no-op keeps those calls non-blocking without
 * importing a second windowing toolkit.
 *
 * Build for the staged Granny player with:
 *   gcc -shared -fPIC -O2 -Wall -Wextra -Werror \
 *     -o ScreenSelector.so screenselector_stub.c
 */

typedef void (*set_resolution_fn)(int width, int height, int fullscreen);
typedef void (*set_display_fn)(int display);

static set_resolution_fn set_selected_resolution;
static set_display_fn set_selected_display;

void liwus_screenselector_stub(void) {}

/* The stock plugin exposes these callbacks and Unity looks them up with
 * dlsym() before creating the SDL window.  A no-op implementation is enough
 * for the headless-compatible path: SDL already selected the primary display,
 * so the GTK selector must return immediately instead of opening a dialog. */
void SetupUnityCallbacks(void *get_axis_description,
                         void *configure_axis,
                         void *get_resolutions,
                         void *get_selected_resolution,
                         set_resolution_fn set_resolution,
                         void *get_selected_quality,
                         void *get_quality_levels,
                         void *set_quality_level,
                         void *get_displays,
                         void *get_selected_display,
                         set_display_fn set_display) {
  (void)get_axis_description;
  (void)configure_axis;
  (void)get_resolutions;
  (void)get_selected_resolution;
  (void)get_selected_quality;
  (void)get_quality_levels;
  (void)set_quality_level;
  (void)get_displays;
  (void)get_selected_display;
  set_selected_resolution = set_resolution;
  set_selected_display = set_display;
}

void LoadScreenSelectorWindow(void *module, const char *title,
                              const char *icon, const char *unused) {
  (void)module;
  (void)title;
  (void)icon;
  (void)unused;
  if (set_selected_resolution)
    set_selected_resolution(800, 600, 0);
  if (set_selected_display)
    set_selected_display(0);
}
void OnScreenSelectorOK(void) {}
void OnScreenSelectorQuit(void) {}
void OnInputListRowActivated(void) {}
void OnSelectedDisplayChanged(void) {}
