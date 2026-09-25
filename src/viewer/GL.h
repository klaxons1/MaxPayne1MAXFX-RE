// Tiny OpenGL 3.3 core loader. Function pointers are resolved through
// SDL_GL_GetProcAddress so the project does not depend on GL headers.
#ifndef MAXFX_VIEWER_GL_H
#define MAXFX_VIEWER_GL_H

#include <cstddef>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef unsigned int GLbitfield;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef std::ptrdiff_t GLsizeiptr;
typedef std::ptrdiff_t GLintptr;
typedef void GLvoid;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_FRONT_AND_BACK 0x0408
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_NEAREST 0x2600
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_EQUAL 0x0202
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207
#define GL_ONE 1
#define GL_ZERO 0
#define GL_FUNC_ADD 0x8006
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#define GL_CULL_FACE_MODE 0x0B45
#define GL_BACK 0x0405
#define GL_FRONT 0x0404
#define GL_DEPTH_WRITEMASK 0x0B72
#define GL_BLEND_SRC_ALPHA 0x80CB
#define GL_SCISSOR_TEST 0x0C11
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_SHADING_LANGUAGE_VERSION 0x8B22

#ifndef APIENTRY
#if defined(_WIN32)
#define APIENTRY __stdcall
#else
#define APIENTRY
#endif
#endif

extern GLenum(APIENTRY* glGetError)();
extern void(APIENTRY* glEnable)(GLenum);
extern void(APIENTRY* glDisable)(GLenum);
extern void(APIENTRY* glViewport)(GLint, GLint, GLsizei, GLsizei);
extern void(APIENTRY* glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
extern void(APIENTRY* glClear)(GLbitfield);
extern void(APIENTRY* glDepthFunc)(GLenum);
extern void(APIENTRY* glDepthMask)(GLboolean);
extern void(APIENTRY* glBlendFunc)(GLenum, GLenum);
extern void(APIENTRY* glCullFace)(GLenum);
extern void(APIENTRY* glFrontFace)(GLenum);
extern void(APIENTRY* glPolygonMode)(GLenum, GLenum);
extern void(APIENTRY* glLineWidth)(GLfloat);
extern void(APIENTRY* glPixelStorei)(GLenum, GLint);
extern const GLubyte*(APIENTRY* glGetString)(GLenum);

extern void(APIENTRY* glGenBuffers)(GLsizei, GLuint*);
extern void(APIENTRY* glDeleteBuffers)(GLsizei, const GLuint*);
extern void(APIENTRY* glBindBuffer)(GLenum, GLuint);
extern void(APIENTRY* glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
extern void(APIENTRY* glBufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);

extern void(APIENTRY* glGenVertexArrays)(GLsizei, GLuint*);
extern void(APIENTRY* glDeleteVertexArrays)(GLsizei, const GLuint*);
extern void(APIENTRY* glBindVertexArray)(GLuint);
extern void(APIENTRY* glEnableVertexAttribArray)(GLuint);
extern void(APIENTRY* glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);

extern void(APIENTRY* glGenTextures)(GLsizei, GLuint*);
extern void(APIENTRY* glDeleteTextures)(GLsizei, const GLuint*);
extern void(APIENTRY* glBindTexture)(GLenum, GLuint);
extern void(APIENTRY* glActiveTexture)(GLenum);
extern void(APIENTRY* glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                    const void*);
extern void(APIENTRY* glTexParameteri)(GLenum, GLenum, GLint);
extern void(APIENTRY* glGenerateMipmap)(GLenum);

extern GLuint(APIENTRY* glCreateShader)(GLenum);
extern void(APIENTRY* glDeleteShader)(GLuint);
extern void(APIENTRY* glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
extern void(APIENTRY* glCompileShader)(GLuint);
extern void(APIENTRY* glGetShaderiv)(GLuint, GLenum, GLint*);
extern void(APIENTRY* glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
extern GLuint(APIENTRY* glCreateProgram)();
extern void(APIENTRY* glDeleteProgram)(GLuint);
extern void(APIENTRY* glAttachShader)(GLuint, GLuint);
extern void(APIENTRY* glLinkProgram)(GLuint);
extern void(APIENTRY* glGetProgramiv)(GLuint, GLenum, GLint*);
extern void(APIENTRY* glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
extern void(APIENTRY* glUseProgram)(GLuint);
extern GLint(APIENTRY* glGetUniformLocation)(GLuint, const GLchar*);
extern void(APIENTRY* glUniform1i)(GLint, GLint);
extern void(APIENTRY* glUniform1f)(GLint, GLfloat);
extern void(APIENTRY* glUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
extern void(APIENTRY* glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
extern void(APIENTRY* glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);

extern void(APIENTRY* glDrawArrays)(GLenum, GLint, GLsizei);
extern void(APIENTRY* glDrawElements)(GLenum, GLsizei, GLenum, const void*);

bool loadGL(void* (*getProc)(const char*));
GLuint compileProgram(const char* vs, const char* fs, char* error, std::size_t errorSize);

#endif  // MAXFX_VIEWER_GL_H
