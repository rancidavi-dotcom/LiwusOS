#ifndef LIWUS_CRAFT_GLFW3_H
#define LIWUS_CRAFT_GLFW3_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GLFWwindow GLFWwindow;
typedef struct GLFWmonitor GLFWmonitor;
typedef struct GLFWvidmode {
    int width, height, redBits, greenBits, blueBits, refreshRate;
} GLFWvidmode;

typedef void (*GLFWkeyfun)(GLFWwindow *, int, int, int, int);
typedef void (*GLFWcharfun)(GLFWwindow *, unsigned int);
typedef void (*GLFWmousebuttonfun)(GLFWwindow *, int, int, int);
typedef void (*GLFWscrollfun)(GLFWwindow *, double, double);

#define GLFW_RELEASE 0
#define GLFW_PRESS 1
#define GLFW_REPEAT 2
#define GLFW_FALSE 0
#define GLFW_TRUE 1
#define GLFW_CURSOR 0x00033001
#define GLFW_CURSOR_NORMAL 0x00034001
#define GLFW_CURSOR_DISABLED 0x00034003
#define GLFW_MOUSE_BUTTON_LEFT 0
#define GLFW_MOUSE_BUTTON_RIGHT 1
#define GLFW_MOUSE_BUTTON_MIDDLE 2
#define GLFW_MOD_SHIFT 0x0001
#define GLFW_MOD_CONTROL 0x0002
#define GLFW_MOD_ALT 0x0004
#define GLFW_MOD_SUPER 0x0008
#define GLFW_KEY_SPACE 32
#define GLFW_KEY_APOSTROPHE 39
#define GLFW_KEY_COMMA 44
#define GLFW_KEY_MINUS 45
#define GLFW_KEY_PERIOD 46
#define GLFW_KEY_SLASH 47
#define GLFW_KEY_0 48
#define GLFW_KEY_1 49
#define GLFW_KEY_2 50
#define GLFW_KEY_3 51
#define GLFW_KEY_4 52
#define GLFW_KEY_5 53
#define GLFW_KEY_6 54
#define GLFW_KEY_7 55
#define GLFW_KEY_8 56
#define GLFW_KEY_9 57
#define GLFW_KEY_SEMICOLON 59
#define GLFW_KEY_EQUAL 61
#define GLFW_KEY_GRAVE_ACCENT 96
#define GLFW_KEY_A 65
#define GLFW_KEY_B 66
#define GLFW_KEY_C 67
#define GLFW_KEY_D 68
#define GLFW_KEY_E 69
#define GLFW_KEY_F 70
#define GLFW_KEY_G 71
#define GLFW_KEY_H 72
#define GLFW_KEY_I 73
#define GLFW_KEY_J 74
#define GLFW_KEY_K 75
#define GLFW_KEY_L 76
#define GLFW_KEY_M 77
#define GLFW_KEY_N 78
#define GLFW_KEY_O 79
#define GLFW_KEY_P 80
#define GLFW_KEY_Q 81
#define GLFW_KEY_R 82
#define GLFW_KEY_S 83
#define GLFW_KEY_T 84
#define GLFW_KEY_U 85
#define GLFW_KEY_V 86
#define GLFW_KEY_W 87
#define GLFW_KEY_X 88
#define GLFW_KEY_Y 89
#define GLFW_KEY_Z 90
#define GLFW_KEY_ESCAPE 256
#define GLFW_KEY_ENTER 257
#define GLFW_KEY_TAB 258
#define GLFW_KEY_BACKSPACE 259
#define GLFW_KEY_RIGHT 262
#define GLFW_KEY_LEFT 263
#define GLFW_KEY_DOWN 264
#define GLFW_KEY_UP 265
#define GLFW_KEY_LEFT_SHIFT 340
#define GLFW_KEY_RIGHT_SHIFT 344
#define GLFW_KEY_LEFT_CONTROL 341
#define GLFW_KEY_RIGHT_CONTROL 345
#define GLFW_KEY_F4 293

int glfwInit(void);
void glfwTerminate(void);
GLFWwindow *glfwCreateWindow(int, int, const char *, GLFWmonitor *, GLFWwindow *);
GLFWmonitor *glfwGetPrimaryMonitor(void);
const GLFWvidmode *glfwGetVideoModes(GLFWmonitor *, int *);
void glfwMakeContextCurrent(GLFWwindow *);
void glfwSwapInterval(int);
void glfwSwapBuffers(GLFWwindow *);
void glfwPollEvents(void);
int glfwWindowShouldClose(GLFWwindow *);
void glfwSetInputMode(GLFWwindow *, int, int);
int glfwGetInputMode(GLFWwindow *, int);
void glfwSetKeyCallback(GLFWwindow *, GLFWkeyfun);
void glfwSetCharCallback(GLFWwindow *, GLFWcharfun);
void glfwSetMouseButtonCallback(GLFWwindow *, GLFWmousebuttonfun);
void glfwSetScrollCallback(GLFWwindow *, GLFWscrollfun);
int glfwGetKey(GLFWwindow *, int);
void glfwGetCursorPos(GLFWwindow *, double *, double *);
const char *glfwGetClipboardString(GLFWwindow *);
void glfwGetWindowSize(GLFWwindow *, int *, int *);
void glfwGetFramebufferSize(GLFWwindow *, int *, int *);
double glfwGetTime(void);
void glfwSetTime(double);

#ifdef __cplusplus
}
#endif
#endif
