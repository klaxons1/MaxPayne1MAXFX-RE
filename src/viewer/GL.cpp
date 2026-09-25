#include "viewer/GL.h"

#include <cstring>

GLenum(APIENTRY* glGetError)() = 0;
void(APIENTRY* glEnable)(GLenum) = 0;
void(APIENTRY* glDisable)(GLenum) = 0;
void(APIENTRY* glViewport)(GLint, GLint, GLsizei, GLsizei) = 0;
void(APIENTRY* glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = 0;
void(APIENTRY* glClear)(GLbitfield) = 0;
void(APIENTRY* glDepthFunc)(GLenum) = 0;
void(APIENTRY* glDepthMask)(GLboolean) = 0;
void(APIENTRY* glBlendFunc)(GLenum, GLenum) = 0;
void(APIENTRY* glCullFace)(GLenum) = 0;
void(APIENTRY* glFrontFace)(GLenum) = 0;
void(APIENTRY* glPolygonMode)(GLenum, GLenum) = 0;
void(APIENTRY* glLineWidth)(GLfloat) = 0;
void(APIENTRY* glPixelStorei)(GLenum, GLint) = 0;
const GLubyte*(APIENTRY* glGetString)(GLenum) = 0;
void(APIENTRY* glGenBuffers)(GLsizei, GLuint*) = 0;
void(APIENTRY* glDeleteBuffers)(GLsizei, const GLuint*) = 0;
void(APIENTRY* glBindBuffer)(GLenum, GLuint) = 0;
void(APIENTRY* glBufferData)(GLenum, GLsizeiptr, const void*, GLenum) = 0;
void(APIENTRY* glBufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*) = 0;
void(APIENTRY* glGenVertexArrays)(GLsizei, GLuint*) = 0;
void(APIENTRY* glDeleteVertexArrays)(GLsizei, const GLuint*) = 0;
void(APIENTRY* glBindVertexArray)(GLuint) = 0;
void(APIENTRY* glEnableVertexAttribArray)(GLuint) = 0;
void(APIENTRY* glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = 0;
void(APIENTRY* glGenTextures)(GLsizei, GLuint*) = 0;
void(APIENTRY* glDeleteTextures)(GLsizei, const GLuint*) = 0;
void(APIENTRY* glBindTexture)(GLenum, GLuint) = 0;
void(APIENTRY* glActiveTexture)(GLenum) = 0;
void(APIENTRY* glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                             const void*) = 0;
void(APIENTRY* glTexParameteri)(GLenum, GLenum, GLint) = 0;
void(APIENTRY* glGenerateMipmap)(GLenum) = 0;
GLuint(APIENTRY* glCreateShader)(GLenum) = 0;
void(APIENTRY* glDeleteShader)(GLuint) = 0;
void(APIENTRY* glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = 0;
void(APIENTRY* glCompileShader)(GLuint) = 0;
void(APIENTRY* glGetShaderiv)(GLuint, GLenum, GLint*) = 0;
void(APIENTRY* glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = 0;
GLuint(APIENTRY* glCreateProgram)() = 0;
void(APIENTRY* glDeleteProgram)(GLuint) = 0;
void(APIENTRY* glAttachShader)(GLuint, GLuint) = 0;
void(APIENTRY* glLinkProgram)(GLuint) = 0;
void(APIENTRY* glGetProgramiv)(GLuint, GLenum, GLint*) = 0;
void(APIENTRY* glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = 0;
void(APIENTRY* glUseProgram)(GLuint) = 0;
GLint(APIENTRY* glGetUniformLocation)(GLuint, const GLchar*) = 0;
void(APIENTRY* glUniform1i)(GLint, GLint) = 0;
void(APIENTRY* glUniform1f)(GLint, GLfloat) = 0;
void(APIENTRY* glUniform3f)(GLint, GLfloat, GLfloat, GLfloat) = 0;
void(APIENTRY* glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = 0;
void(APIENTRY* glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*) = 0;
void(APIENTRY* glDrawArrays)(GLenum, GLint, GLsizei) = 0;
void(APIENTRY* glDrawElements)(GLenum, GLsizei, GLenum, const void*) = 0;

bool loadGL(void* (*getProc)(const char*)) {
#define LOAD(name)                                 \
    do {                                           \
        void* _p = getProc(#name);                 \
        if (_p == 0) {                             \
            return false;                          \
        }                                          \
        memcpy(&name, &_p, sizeof(void*));         \
    } while (0)

    LOAD(glGetError);
    LOAD(glEnable);
    LOAD(glDisable);
    LOAD(glViewport);
    LOAD(glClearColor);
    LOAD(glClear);
    LOAD(glDepthFunc);
    LOAD(glDepthMask);
    LOAD(glBlendFunc);
    LOAD(glCullFace);
    LOAD(glFrontFace);
    LOAD(glPolygonMode);
    LOAD(glLineWidth);
    LOAD(glPixelStorei);
    LOAD(glGetString);
    LOAD(glGenBuffers);
    LOAD(glDeleteBuffers);
    LOAD(glBindBuffer);
    LOAD(glBufferData);
    LOAD(glBufferSubData);
    LOAD(glGenVertexArrays);
    LOAD(glDeleteVertexArrays);
    LOAD(glBindVertexArray);
    LOAD(glEnableVertexAttribArray);
    LOAD(glVertexAttribPointer);
    LOAD(glGenTextures);
    LOAD(glDeleteTextures);
    LOAD(glBindTexture);
    LOAD(glActiveTexture);
    LOAD(glTexImage2D);
    LOAD(glTexParameteri);
    LOAD(glGenerateMipmap);
    LOAD(glCreateShader);
    LOAD(glDeleteShader);
    LOAD(glShaderSource);
    LOAD(glCompileShader);
    LOAD(glGetShaderiv);
    LOAD(glGetShaderInfoLog);
    LOAD(glCreateProgram);
    LOAD(glDeleteProgram);
    LOAD(glAttachShader);
    LOAD(glLinkProgram);
    LOAD(glGetProgramiv);
    LOAD(glGetProgramInfoLog);
    LOAD(glUseProgram);
    LOAD(glGetUniformLocation);
    LOAD(glUniform1i);
    LOAD(glUniform1f);
    LOAD(glUniform3f);
    LOAD(glUniform4f);
    LOAD(glUniformMatrix4fv);
    LOAD(glDrawArrays);
    LOAD(glDrawElements);
#undef LOAD
    return true;
}

static bool compileShader(GLenum type, const char* src, GLuint* out, char* error, std::size_t errorSize) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, 0);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        if (error && errorSize > 0) {
            glGetShaderInfoLog(shader, static_cast<GLsizei>(errorSize), 0, error);
        }
        glDeleteShader(shader);
        return false;
    }
    *out = shader;
    return true;
}

GLuint compileProgram(const char* vs, const char* fs, char* error, std::size_t errorSize) {
    GLuint v = 0, f = 0;
    if (!compileShader(GL_VERTEX_SHADER, vs, &v, error, errorSize)) {
        return 0;
    }
    if (!compileShader(GL_FRAGMENT_SHADER, fs, &f, error, errorSize)) {
        glDeleteShader(v);
        return 0;
    }
    const GLuint prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        if (error && errorSize > 0) {
            glGetProgramInfoLog(prog, static_cast<GLsizei>(errorSize), 0, error);
        }
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}
