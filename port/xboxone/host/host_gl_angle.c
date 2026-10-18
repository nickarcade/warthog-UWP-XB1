#include "host.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <windows.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#ifndef GL_MAJOR_VERSION
#define GL_MAJOR_VERSION 0x821B
#endif
#ifndef GL_MINOR_VERSION
#define GL_MINOR_VERSION 0x821C
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif
#ifndef GL_TEXTURE_LOD_BIAS
#define GL_TEXTURE_LOD_BIAS 0x8501
#endif
#ifndef GL_ARRAY_BUFFER_BINDING
#define GL_ARRAY_BUFFER_BINDING 0x8894
#endif
#ifndef GL_VERTEX_ATTRIB_ARRAY_DIVISOR
#define GL_VERTEX_ATTRIB_ARRAY_DIVISOR 0x88FE
#endif
#ifndef GL_VERTEX_ATTRIB_ARRAY_INTEGER
#define GL_VERTEX_ATTRIB_ARRAY_INTEGER 0x88FD
#endif

typedef void (GL_APIENTRYP PFNGLDRAWELEMENTSBASEVERTEXEXTPROC)(
    GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex);
typedef void (GL_APIENTRYP PFNGLCOPYIMAGESUBDATAEXTPROC)(
    GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ,
    GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ,
    GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth);

static PFNGLDRAWELEMENTSBASEVERTEXEXTPROC s_pfnDrawElementsBaseVertex = NULL;
static PFNGLCOPYIMAGESUBDATAEXTPROC s_pfnCopyImageSubData = NULL;
static int s_angle_exts_probed = 0;

static void probe_angle_extensions(void) {
    if (s_angle_exts_probed) return;
    s_angle_exts_probed = 1;

    s_pfnDrawElementsBaseVertex = (PFNGLDRAWELEMENTSBASEVERTEXEXTPROC)
        eglGetProcAddress("glDrawElementsBaseVertex");
    if (!s_pfnDrawElementsBaseVertex)
        s_pfnDrawElementsBaseVertex = (PFNGLDRAWELEMENTSBASEVERTEXEXTPROC)
            eglGetProcAddress("glDrawElementsBaseVertexEXT");
    if (!s_pfnDrawElementsBaseVertex)
        s_pfnDrawElementsBaseVertex = (PFNGLDRAWELEMENTSBASEVERTEXEXTPROC)
            eglGetProcAddress("glDrawElementsBaseVertexOES");

    s_pfnCopyImageSubData = (PFNGLCOPYIMAGESUBDATAEXTPROC)
        eglGetProcAddress("glCopyImageSubData");
    if (!s_pfnCopyImageSubData)
        s_pfnCopyImageSubData = (PFNGLCOPYIMAGESUBDATAEXTPROC)
            eglGetProcAddress("glCopyImageSubDataEXT");
    if (!s_pfnCopyImageSubData)
        s_pfnCopyImageSubData = (PFNGLCOPYIMAGESUBDATAEXTPROC)
            eglGetProcAddress("glCopyImageSubDataOES");
}

void GUEST_ABI hostgl_glDrawElementsBaseVertex(
    GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex)
{
    probe_angle_extensions();
    if (basevertex == 0) {
        glDrawElements(mode, count, type, indices);
        return;
    }
    if (s_pfnDrawElementsBaseVertex) {
        s_pfnDrawElementsBaseVertex(mode, count, type, indices, basevertex);
        return;
    }

    GLint prev_array_buffer = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);

    struct {
        GLint enabled;
        GLint vbo;
        GLint size;
        GLint type;
        GLint normalized;
        GLint stride;
        GLint integer;
        GLint divisor;
        void *pointer;
    } attrs[16];

    for (GLuint i = 0; i < 16; ++i) {
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attrs[i].enabled);
        if (!attrs[i].enabled) continue;
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &attrs[i].divisor);
        if (attrs[i].divisor != 0) continue;
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &attrs[i].vbo);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &attrs[i].size);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &attrs[i].type);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &attrs[i].normalized);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &attrs[i].stride);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_INTEGER, &attrs[i].integer);
        glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &attrs[i].pointer);

        if (attrs[i].vbo != 0 && attrs[i].stride > 0) {
            glBindBuffer(GL_ARRAY_BUFFER, (GLuint)attrs[i].vbo);
            const uint8_t *shifted_ptr = (const uint8_t *)attrs[i].pointer + ((intptr_t)basevertex * attrs[i].stride);
            if (attrs[i].integer) {
                glVertexAttribIPointer(i, attrs[i].size, (GLenum)attrs[i].type, attrs[i].stride, shifted_ptr);
            } else {
                glVertexAttribPointer(i, attrs[i].size, (GLenum)attrs[i].type, (GLboolean)attrs[i].normalized, attrs[i].stride, shifted_ptr);
            }
        }
    }

    glDrawElements(mode, count, type, indices);

    for (GLuint i = 0; i < 16; ++i) {
        if (!attrs[i].enabled || attrs[i].divisor != 0) continue;
        if (attrs[i].vbo != 0 && attrs[i].stride > 0) {
            glBindBuffer(GL_ARRAY_BUFFER, (GLuint)attrs[i].vbo);
            if (attrs[i].integer) {
                glVertexAttribIPointer(i, attrs[i].size, (GLenum)attrs[i].type, attrs[i].stride, attrs[i].pointer);
            } else {
                glVertexAttribPointer(i, attrs[i].size, (GLenum)attrs[i].type, (GLboolean)attrs[i].normalized, attrs[i].stride, attrs[i].pointer);
            }
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prev_array_buffer);
}

void GUEST_ABI hostgl_glCopyImageSubData(
    GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ,
    long long dstName, long long dstTarget, long long dstLevel, long long dstX, long long dstY, long long dstZ,
    long long srcWidth, long long srcHeight, long long srcDepth)
{
    probe_angle_extensions();
    if (s_pfnCopyImageSubData) {
        s_pfnCopyImageSubData(srcName, srcTarget, srcLevel, srcX, srcY, srcZ,
                             (GLuint)dstName, (GLenum)dstTarget, (GLint)dstLevel,
                             (GLint)dstX, (GLint)dstY, (GLint)dstZ,
                             (GLsizei)srcWidth, (GLsizei)srcHeight, (GLsizei)srcDepth);
        return;
    }

    GLint prev_read_fbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
    GLuint temp_fbo = 0;
    glGenFramebuffers(1, &temp_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, temp_fbo);

    if (srcTarget == GL_RENDERBUFFER) {
        glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, srcName);
    } else {
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, srcTarget, srcName, srcLevel);
    }

    if (dstTarget == GL_TEXTURE_2D || (dstTarget >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && dstTarget <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)) {
        GLint prev_tex = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
        glBindTexture(dstTarget == GL_TEXTURE_2D ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP, (GLuint)dstName);
        glCopyTexSubImage2D((GLenum)dstTarget, (GLint)dstLevel, (GLint)dstX, (GLint)dstY, srcX, srcY, (GLsizei)srcWidth, (GLsizei)srcHeight);
        glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_read_fbo);
    glDeleteFramebuffers(1, &temp_fbo);
}

void GUEST_ABI hostgl_glGetIntegerv(GLenum pname, GLint *params) {
    if (!params) return;
    if (pname == GL_MAJOR_VERSION) {
        *params = 3;
        return;
    }
    if (pname == GL_MINOR_VERSION) {
        *params = 0;
        return;
    }
    glGetIntegerv(pname, params);
}

void GUEST_ABI hostgl_glTexParameteri(GLenum target, GLenum pname, GLint param) {
    if (pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T || pname == GL_TEXTURE_WRAP_R) {
        if (param == GL_CLAMP_TO_BORDER) {
            param = GL_CLAMP_TO_EDGE;
        }
    }
    if (pname == GL_TEXTURE_LOD_BIAS) {
        return;
    }
    glTexParameteri(target, pname, param);
}

void GUEST_ABI hostgl_glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    if (pname == GL_TEXTURE_LOD_BIAS) {
        return;
    }
    glTexParameterf(target, pname, param);
}

uint32_t GUEST_ABI hostgl_glGetString(GLenum name) {
    static uint32_t guest_str_buf = 0;
    static unsigned int ring_slot = 0;
    if (!guest_str_buf) {
        void *p = host_low_map(16384, 3);
        guest_str_buf = (uint32_t)(uintptr_t)p;
    }
    if (!guest_str_buf) return 0;

    uint32_t offset = (ring_slot++ & 3) * 4096;
    char *dst = (char *)(uintptr_t)(guest_str_buf + offset);

    if (name == GL_VERSION) {
        const char *override_ver = "3.0 OpenGL ES 3.0 (ANGLE 2.1.13 D3D11)";
        strncpy(dst, override_ver, 4095);
        dst[4095] = '\0';
        return guest_str_buf + offset;
    }
    if (name == GL_SHADING_LANGUAGE_VERSION) {
        const char *override_glsl = "3.00 OpenGL ES GLSL ES 3.00 (ANGLE)";
        strncpy(dst, override_glsl, 4095);
        dst[4095] = '\0';
        return guest_str_buf + offset;
    }
    const char *src = (const char *)glGetString(name);
    if (!src) return 0;
    strncpy(dst, src, 4095);
    dst[4095] = '\0';
    return guest_str_buf + offset;
}
