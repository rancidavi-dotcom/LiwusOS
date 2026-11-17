/*
 * Unity's optional ScreenSelector plugin is a GTK2 desktop helper. GTK2 is
 * not part of LiwusOS and the player does not call an exported entry point
 * from this plugin during normal startup. Replacing it with this loadable
 * no-op keeps Unity's plugin discovery path successful without importing a
 * second windowing toolkit.
 *
 * Build for the staged Granny player with:
 *   gcc -shared -fPIC -O2 -Wall -Wextra -Werror \
 *     -o ScreenSelector.so screenselector_stub.c
 */

void liwus_screenselector_stub(void) {}
