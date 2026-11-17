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

void liwus_screenselector_stub(void) {}

/* The stock plugin exposes these callbacks and Unity looks them up with
 * dlsym() before creating the SDL window.  A no-op implementation is enough
 * for the headless-compatible path: SDL already selected the primary display,
 * so the GTK selector must return immediately instead of opening a dialog. */
void SetupUnityCallbacks(void) {}
void LoadScreenSelectorWindow(void) {}
void OnScreenSelectorOK(void) {}
void OnScreenSelectorQuit(void) {}
void OnInputListRowActivated(void) {}
void OnSelectedDisplayChanged(void) {}
