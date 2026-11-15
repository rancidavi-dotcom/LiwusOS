#include "GLFW/glfw3.h"
#ifndef _config_h_
#define _config_h_

/* Port settings: keep Craft's gameplay and assets, with a smaller first-pass
 * render radius suited to the software framebuffer renderer. */
#define DEBUG 0
#define FULLSCREEN 1
#define WINDOW_WIDTH 320
#define WINDOW_HEIGHT 200
#define VSYNC 1
#define SCROLL_THRESHOLD 0.1
#define MAX_MESSAGES 4
#define DB_PATH "craft.db"
#define USE_CACHE 0
#define DAY_LENGTH 600
#define INVERT_MOUSE 0
#define SHOW_LIGHTS 1
#define SHOW_PLANTS 1
#define SHOW_CLOUDS 1
#define SHOW_TREES 1
#define SHOW_ITEM 1
#define SHOW_CROSSHAIRS 1
#define SHOW_WIREFRAME 1
#define SHOW_INFO_TEXT 1
#define SHOW_CHAT_TEXT 1
#define SHOW_PLAYER_NAMES 1
#define CRAFT_KEY_FORWARD 'W'
#define CRAFT_KEY_BACKWARD 'S'
#define CRAFT_KEY_LEFT 'A'
#define CRAFT_KEY_RIGHT 'D'
#define CRAFT_KEY_JUMP GLFW_KEY_SPACE
#define CRAFT_KEY_FLY GLFW_KEY_TAB
#define CRAFT_KEY_OBSERVE 'O'
#define CRAFT_KEY_OBSERVE_INSET 'P'
#define CRAFT_KEY_ITEM_NEXT 'E'
#define CRAFT_KEY_ITEM_PREV 'R'
#define CRAFT_KEY_ZOOM GLFW_KEY_LEFT_SHIFT
#define CRAFT_KEY_ORTHO 'F'
#define CRAFT_KEY_CHAT 't'
#define CRAFT_KEY_COMMAND '/'
#define CRAFT_KEY_SIGN '`'
#define CREATE_CHUNK_RADIUS 1
#define RENDER_CHUNK_RADIUS 1
#define RENDER_SIGN_RADIUS 2
#define DELETE_CHUNK_RADIUS 2
#define CHUNK_SIZE 32
#define COMMIT_INTERVAL 5

#endif
