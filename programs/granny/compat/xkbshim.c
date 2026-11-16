#include <X11/XKBlib.h>

/*
 * The Linux Unity player only needs XKB when it is available.  Liwus' X11
 * transport currently exposes the core protocol but not the XKB extension;
 * preloading this small shim makes Xlib select its documented fallback path.
 * Build as:
 *   cc -shared -fPIC -O2 -o libxkbshim.so xkbshim.c -lX11
 */
Bool XkbQueryExtension(Display *display, int *opcode_rtrn, int *event_rtrn,
                       int *error_rtrn, int *major_in_out,
                       int *minor_in_out) {
  (void)display;
  (void)opcode_rtrn;
  (void)event_rtrn;
  (void)error_rtrn;
  (void)major_in_out;
  (void)minor_in_out;
  return False;
}
