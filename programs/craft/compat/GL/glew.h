#ifndef LIWUS_CRAFT_GLEW_H
#define LIWUS_CRAFT_GLEW_H
#include <stddef.h>

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_TRIANGLES 0x0004
#define GL_LINES 0x0001
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_COLOR_LOGIC_OP 0x0BF2
#define GL_INVERT 0x150A
#define GL_SCISSOR_TEST 0x0C11
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84

GLenum glewInit(void);
#define GLEW_OK 0
void glGenBuffers(GLsizei, GLuint *);
void glBindBuffer(GLenum, GLuint);
void glBufferData(GLenum, GLsizeiptr, const GLvoid *, GLenum);
void glDeleteBuffers(GLsizei, const GLuint *);
void glEnableVertexAttribArray(GLuint);
void glDisableVertexAttribArray(GLuint);
void glVertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid *);
void glDrawArrays(GLenum, GLint, GLsizei);
void glEnable(GLenum);
void glDisable(GLenum);
void glBlendFunc(GLenum, GLenum);
void glPolygonOffset(GLfloat, GLfloat);
void glLineWidth(GLfloat);
void glLogicOp(GLenum);
void glViewport(GLint, GLint, GLsizei, GLsizei);
void glScissor(GLint, GLint, GLsizei, GLsizei);
void glClear(GLbitfield);
void glClearColor(GLclampf, GLclampf, GLclampf, GLclampf);
void glGenTextures(GLsizei, GLuint *);
void glActiveTexture(GLenum);
void glBindTexture(GLenum, GLuint);
void glTexParameteri(GLenum, GLenum, GLint);
void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
GLuint glCreateShader(GLenum);
void glShaderSource(GLuint, GLsizei, const GLchar *const *, const GLint *);
void glCompileShader(GLuint);
void glGetShaderiv(GLuint, GLenum, GLint *);
void glGetShaderInfoLog(GLuint, GLsizei, GLsizei *, GLchar *);
GLuint glCreateProgram(void);
void glAttachShader(GLuint, GLuint);
void glLinkProgram(GLuint);
void glGetProgramiv(GLuint, GLenum, GLint *);
void glGetProgramInfoLog(GLuint, GLsizei, GLsizei *, GLchar *);
void glDetachShader(GLuint, GLuint);
void glDeleteShader(GLuint);
void glUseProgram(GLuint);
GLint glGetAttribLocation(GLuint, const GLchar *);
GLint glGetUniformLocation(GLuint, const GLchar *);
void glUniformMatrix4fv(GLint, GLsizei, GLboolean, const GLfloat *);
void glUniform1i(GLint, GLint);
void glUniform1f(GLint, GLfloat);
void glUniform3f(GLint, GLfloat, GLfloat, GLfloat);

#endif
