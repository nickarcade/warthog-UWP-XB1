#include "host.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <windows.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#ifndef GL_DEPTH_STENCIL_OES
#define GL_DEPTH_STENCIL_OES 0x84F9
#endif
#ifndef GL_UNSIGNED_INT_24_8_OES
#define GL_UNSIGNED_INT_24_8_OES 0x84FA
#endif
#ifndef GL_DEPTH24_STENCIL8_OES
#define GL_DEPTH24_STENCIL8_OES 0x88F0
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif
#ifndef GL_TEXTURE_LOD_BIAS
#define GL_TEXTURE_LOD_BIAS 0x8501
#endif

static int s_is_gles2 = -1;

static int is_gles2_context(void) {
    if (s_is_gles2 < 0) {
        const char *ver = (const char *)glGetString(GL_VERSION);
        s_is_gles2 = (ver && strstr(ver, "OpenGL ES 2.0") != NULL) ? 1 : 0;
        host_logf(HOST_LOG_INFO, "ANGLE context mode detected: %s (%s)",
                  s_is_gles2 ? "OpenGL ES 2.0 (ES3 compat shim active)" : "OpenGL ES 3.0+",
                  ver ? ver : "unknown");
    }
    return s_is_gles2;
}

/* =========================================================================
 * 1. VAO Extensions for ES 2.0 (GL_OES_vertex_array_object)
 * ========================================================================= */
typedef void (GL_APIENTRYP PFNGLGENVERTEXARRAYSOESPROC)(GLsizei n, GLuint *arrays);
typedef void (GL_APIENTRYP PFNGLDELETEVERTEXARRAYSOESPROC)(GLsizei n, const GLuint *arrays);
typedef void (GL_APIENTRYP PFNGLBINDVERTEXARRAYOESPROC)(GLuint array);

static PFNGLGENVERTEXARRAYSOESPROC s_glGenVertexArraysOES = NULL;
static PFNGLDELETEVERTEXARRAYSOESPROC s_glDeleteVertexArraysOES = NULL;
static PFNGLBINDVERTEXARRAYOESPROC s_glBindVertexArrayOES = NULL;
static int s_vao_probed = 0;

static void probe_vao_ext(void) {
    if (s_vao_probed) return;
    s_vao_probed = 1;
    s_glGenVertexArraysOES = (PFNGLGENVERTEXARRAYSOESPROC)eglGetProcAddress("glGenVertexArraysOES");
    s_glDeleteVertexArraysOES = (PFNGLDELETEVERTEXARRAYSOESPROC)eglGetProcAddress("glDeleteVertexArraysOES");
    s_glBindVertexArrayOES = (PFNGLBINDVERTEXARRAYOESPROC)eglGetProcAddress("glBindVertexArrayOES");
}

void GUEST_ABI hostgl_glGenVertexArrays(GLsizei n, GLuint *arrays) {
    if (is_gles2_context()) {
        probe_vao_ext();
        if (s_glGenVertexArraysOES) { s_glGenVertexArraysOES(n, arrays); return; }
        for (GLsizei i = 0; i < n; ++i) arrays[i] = i + 1;
        return;
    }
    glGenVertexArrays(n, arrays);
}

void GUEST_ABI hostgl_glDeleteVertexArrays(GLsizei n, const GLuint *arrays) {
    if (is_gles2_context()) {
        probe_vao_ext();
        if (s_glDeleteVertexArraysOES) s_glDeleteVertexArraysOES(n, arrays);
        return;
    }
    glDeleteVertexArrays(n, arrays);
}

void GUEST_ABI hostgl_glBindVertexArray(GLuint array) {
    if (is_gles2_context()) {
        probe_vao_ext();
        if (s_glBindVertexArrayOES) s_glBindVertexArrayOES(array);
        return;
    }
    glBindVertexArray(array);
}

/* =========================================================================
 * 2. Texture & Framebuffer Shims
 * ========================================================================= */
#define MAX_TRACKED_FBOS 512
static struct {
    GLuint fbo;
    GLuint color_tex;
} s_fbo_table[MAX_TRACKED_FBOS];

static GLuint s_read_fbo = 0;
static GLuint s_draw_fbo = 0;

static void record_fbo_color(GLuint fbo, GLuint tex) {
    if (!fbo) return;
    int free_slot = -1;
    for (int i = 0; i < MAX_TRACKED_FBOS; ++i) {
        if (s_fbo_table[i].fbo == fbo) {
            s_fbo_table[i].color_tex = tex;
            return;
        }
        if (free_slot < 0 && s_fbo_table[i].fbo == 0) free_slot = i;
    }
    if (free_slot >= 0) {
        s_fbo_table[free_slot].fbo = fbo;
        s_fbo_table[free_slot].color_tex = tex;
    }
}

static GLuint lookup_fbo_color(GLuint fbo) {
    if (!fbo) return 0;
    for (int i = 0; i < MAX_TRACKED_FBOS; ++i) {
        if (s_fbo_table[i].fbo == fbo) return s_fbo_table[i].color_tex;
    }
    return 0;
}

void GUEST_ABI hostgl_glBindFramebuffer(GLenum target, GLuint framebuffer) {
    if (target == GL_FRAMEBUFFER) {
        s_read_fbo = framebuffer;
        s_draw_fbo = framebuffer;
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    } else if (target == GL_READ_FRAMEBUFFER) {
        s_read_fbo = framebuffer;
        if (!is_gles2_context()) glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    } else if (target == GL_DRAW_FRAMEBUFFER) {
        s_draw_fbo = framebuffer;
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    } else {
        glBindFramebuffer(target, framebuffer);
    }
}

void GUEST_ABI hostgl_glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
    if (framebuffers) {
        for (GLsizei j = 0; j < n; ++j) {
            for (int i = 0; i < MAX_TRACKED_FBOS; ++i) {
                if (s_fbo_table[i].fbo == framebuffers[j]) {
                    s_fbo_table[i].fbo = 0;
                    s_fbo_table[i].color_tex = 0;
                }
            }
        }
    }
    glDeleteFramebuffers(n, framebuffers);
}

void GUEST_ABI hostgl_glTexImage2D(
    GLenum target, GLint level, GLint internalformat,
    GLsizei width, GLsizei height, GLint border,
    GLenum format, GLenum type, const void *pixels)
{
    if (is_gles2_context()) {
        if (internalformat == GL_RGBA8 || internalformat == GL_RGBA) {
            internalformat = GL_RGBA;
            format = GL_RGBA;
        } else if (internalformat == GL_RGB8 || internalformat == GL_RGB) {
            internalformat = GL_RGB;
            format = GL_RGB;
        } else if (internalformat == GL_R8) {
            internalformat = GL_LUMINANCE;
            format = GL_LUMINANCE;
        } else if (internalformat == GL_RG8) {
            internalformat = GL_LUMINANCE_ALPHA;
            format = GL_LUMINANCE_ALPHA;
        } else if (internalformat == GL_DEPTH24_STENCIL8 || format == GL_DEPTH_STENCIL) {
            internalformat = GL_DEPTH_STENCIL_OES;
            format = GL_DEPTH_STENCIL_OES;
            type = GL_UNSIGNED_INT_24_8_OES;
        }
    }
    glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);

    if (is_gles2_context() && level == 0 && target == GL_TEXTURE_2D) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

void GUEST_ABI hostgl_glFramebufferTexture2D(
    GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
    GLenum actual_target = is_gles2_context() ? GL_FRAMEBUFFER : target;
    if (attachment == GL_COLOR_ATTACHMENT0) {
        record_fbo_color(s_draw_fbo, texture);
    }
    if (is_gles2_context() && attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        glFramebufferTexture2D(actual_target, GL_DEPTH_ATTACHMENT, textarget, texture, level);
        glFramebufferTexture2D(actual_target, GL_STENCIL_ATTACHMENT, textarget, texture, level);
        return;
    }
    glFramebufferTexture2D(actual_target, attachment, textarget, texture, level);
}

void GUEST_ABI hostgl_glFramebufferRenderbuffer(
    GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)
{
    GLenum actual_target = is_gles2_context() ? GL_FRAMEBUFFER : target;
    if (is_gles2_context() && attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        glFramebufferRenderbuffer(actual_target, GL_DEPTH_ATTACHMENT, renderbuffertarget, renderbuffer);
        glFramebufferRenderbuffer(actual_target, GL_STENCIL_ATTACHMENT, renderbuffertarget, renderbuffer);
        return;
    }
    glFramebufferRenderbuffer(actual_target, attachment, renderbuffertarget, renderbuffer);
}

void GUEST_ABI hostgl_glDrawBuffers(GLsizei n, const GLenum *bufs) {
    if (is_gles2_context()) return;
    glDrawBuffers(n, bufs);
}

void GUEST_ABI hostgl_glReadBuffer(GLenum src) {
    if (is_gles2_context()) return;
    glReadBuffer(src);
}

/* =========================================================================
 * 3. Sampler Object Emulation for ES 2.0
 * ========================================================================= */
#define MAX_SAMPLERS 64
struct emu_sampler {
    GLint min_filter;
    GLint mag_filter;
    GLint wrap_s;
    GLint wrap_t;
};
static struct emu_sampler s_samplers[MAX_SAMPLERS];
static GLuint s_bound_sampler[16];
static GLenum s_active_unit = 0;

static void apply_sampler_to_target(GLuint sampler, GLenum target) {
    if (!sampler || sampler >= MAX_SAMPLERS) return;
    if (s_samplers[sampler].min_filter)
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, s_samplers[sampler].min_filter);
    if (s_samplers[sampler].mag_filter)
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, s_samplers[sampler].mag_filter);
    if (s_samplers[sampler].wrap_s)
        glTexParameteri(target, GL_TEXTURE_WRAP_S, s_samplers[sampler].wrap_s);
    if (s_samplers[sampler].wrap_t)
        glTexParameteri(target, GL_TEXTURE_WRAP_T, s_samplers[sampler].wrap_t);
}

void GUEST_ABI hostgl_glGenSamplers(GLsizei count, GLuint *samplers) {
    if (!is_gles2_context()) { glGenSamplers(count, samplers); return; }
    static GLuint next_id = 1;
    for (GLsizei i = 0; i < count; ++i) {
        GLuint id = (next_id < MAX_SAMPLERS) ? next_id++ : (MAX_SAMPLERS - 1);
        s_samplers[id].min_filter = GL_LINEAR;
        s_samplers[id].mag_filter = GL_LINEAR;
        s_samplers[id].wrap_s = GL_REPEAT;
        s_samplers[id].wrap_t = GL_REPEAT;
        samplers[i] = id;
    }
}

void GUEST_ABI hostgl_glDeleteSamplers(GLsizei count, const GLuint *samplers) {
    if (!is_gles2_context()) glDeleteSamplers(count, samplers);
}

void GUEST_ABI hostgl_glSamplerParameteri(GLuint sampler, GLenum pname, GLint param) {
    if (param == GL_CLAMP_TO_BORDER) param = GL_CLAMP_TO_EDGE;
    if (!is_gles2_context()) { glSamplerParameteri(sampler, pname, param); return; }
    if (!sampler || sampler >= MAX_SAMPLERS) return;
    if (pname == GL_TEXTURE_MIN_FILTER) {
        if (param == GL_NEAREST_MIPMAP_NEAREST || param == GL_NEAREST_MIPMAP_LINEAR)
            param = GL_NEAREST;
        else if (param == GL_LINEAR_MIPMAP_NEAREST || param == GL_LINEAR_MIPMAP_LINEAR)
            param = GL_LINEAR;
        s_samplers[sampler].min_filter = param;
    } else if (pname == GL_TEXTURE_MAG_FILTER) {
        s_samplers[sampler].mag_filter = param;
    } else if (pname == GL_TEXTURE_WRAP_S) {
        s_samplers[sampler].wrap_s = param;
    } else if (pname == GL_TEXTURE_WRAP_T) {
        s_samplers[sampler].wrap_t = param;
    }
}

void GUEST_ABI hostgl_glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) {
    if (!is_gles2_context()) glSamplerParameterf(sampler, pname, param);
}

void GUEST_ABI hostgl_glSamplerParameterfv(GLuint sampler, GLenum pname, const GLfloat *param) {
    if (!is_gles2_context()) glSamplerParameterfv(sampler, pname, param);
}

void GUEST_ABI hostgl_glActiveTexture(GLenum texture) {
    if (texture >= GL_TEXTURE0 && texture < GL_TEXTURE0 + 16)
        s_active_unit = texture - GL_TEXTURE0;
    glActiveTexture(texture);
}

void GUEST_ABI hostgl_glBindSampler(GLuint unit, GLuint sampler) {
    if (!is_gles2_context()) { glBindSampler(unit, sampler); return; }
    if (unit < 16) {
        s_bound_sampler[unit] = sampler;
        GLenum prev_unit = s_active_unit;
        glActiveTexture(GL_TEXTURE0 + unit);
        apply_sampler_to_target(sampler, GL_TEXTURE_2D);
        glActiveTexture(GL_TEXTURE0 + prev_unit);
    }
}

void GUEST_ABI hostgl_glBindTexture(GLenum target, GLuint texture) {
    glBindTexture(target, texture);
    if (is_gles2_context() && (target == GL_TEXTURE_2D || target == GL_TEXTURE_CUBE_MAP)) {
        if (s_bound_sampler[s_active_unit])
            apply_sampler_to_target(s_bound_sampler[s_active_unit], target);
    }
}

void GUEST_ABI hostgl_glTexParameteri(GLenum target, GLenum pname, GLint param) {
    if (pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T || pname == GL_TEXTURE_WRAP_R) {
        if (param == GL_CLAMP_TO_BORDER) param = GL_CLAMP_TO_EDGE;
    }
    if (pname == GL_TEXTURE_LOD_BIAS) return;
    if (is_gles2_context() && pname == GL_TEXTURE_MAX_LEVEL) return;
    glTexParameteri(target, pname, param);
}

void GUEST_ABI hostgl_glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    if (pname == GL_TEXTURE_LOD_BIAS) return;
    glTexParameterf(target, pname, param);
}

/* =========================================================================
 * 4. GLSL #version 300 es -> #version 100 Translation
 * ========================================================================= */
#define MAX_SHADERS 1024
static struct {
    GLuint shader;
    GLenum type;
    uint32_t attr_mask;
    char attr_names[16][32];
} s_shader_info[MAX_SHADERS];

static void record_shader_attr(GLuint shader, int loc, const char *name) {
    if (loc < 0 || loc >= 16) return;
    int slot = -1;
    for (int i = 0; i < MAX_SHADERS; ++i) {
        if (s_shader_info[i].shader == shader) { slot = i; break; }
        if (slot < 0 && s_shader_info[i].shader == 0) slot = i;
    }
    if (slot >= 0) {
        s_shader_info[slot].shader = shader;
        s_shader_info[slot].attr_mask |= (1u << loc);
        strncpy(s_shader_info[slot].attr_names[loc], name, 31);
        s_shader_info[slot].attr_names[loc][31] = '\0';
    }
}

#define MAX_PROGRAMS 512
static struct {
    GLuint program;
    GLuint vs;
} s_prog_info[MAX_PROGRAMS];

GLuint GUEST_ABI hostgl_glCreateProgram(void) {
    GLuint p = glCreateProgram();
    for (int i = 0; i < MAX_PROGRAMS; ++i) {
        if (s_prog_info[i].program == 0 || s_prog_info[i].program == p) {
            s_prog_info[i].program = p;
            s_prog_info[i].vs = 0;
            break;
        }
    }
    return p;
}

void GUEST_ABI hostgl_glAttachShader(GLuint program, GLuint shader) {
    glAttachShader(program, shader);
    for (int i = 0; i < MAX_SHADERS; ++i) {
        if (s_shader_info[i].shader == shader && s_shader_info[i].attr_mask != 0) {
            for (int j = 0; j < MAX_PROGRAMS; ++j) {
                if (s_prog_info[j].program == program) {
                    s_prog_info[j].vs = shader;
                    break;
                }
            }
            break;
        }
    }
}

void GUEST_ABI hostgl_glLinkProgram(GLuint program) {
    if (is_gles2_context()) {
        for (int j = 0; j < MAX_PROGRAMS; ++j) {
            if (s_prog_info[j].program == program && s_prog_info[j].vs != 0) {
                GLuint vs = s_prog_info[j].vs;
                for (int i = 0; i < MAX_SHADERS; ++i) {
                    if (s_shader_info[i].shader == vs) {
                        for (int loc = 0; loc < 16; ++loc) {
                            if (s_shader_info[i].attr_mask & (1u << loc)) {
                                glBindAttribLocation(program, (GLuint)loc, s_shader_info[i].attr_names[loc]);
                            }
                        }
                        break;
                    }
                }
                break;
            }
        }
    }
    glLinkProgram(program);
}

static char *replace_all(const char *src, const char *needle, const char *repl) {
    size_t src_len = strlen(src);
    size_t n_len = strlen(needle);
    size_t r_len = strlen(repl);
    size_t count = 0;
    const char *p = src;
    while ((p = strstr(p, needle)) != NULL) { count++; p += n_len; }
    char *out = (char *)malloc(src_len + count * (r_len > n_len ? (r_len - n_len) : 0) + 1);
    if (!out) return NULL;
    char *dst = out;
    p = src;
    const char *match;
    while ((match = strstr(p, needle)) != NULL) {
        size_t chunk = (size_t)(match - p);
        memcpy(dst, p, chunk);
        dst += chunk;
        memcpy(dst, repl, r_len);
        dst += r_len;
        p = match + n_len;
    }
    strcpy(dst, p);
    return out;
}

static char *translate_es3_to_es2_shader(GLuint shader, const char *src) {
    int is_vs = (strstr(src, "gl_Position") != NULL);
    int tex_is_cube[4] = {0, 0, 0, 0};
    if (strstr(src, "uniform samplerCube tex0;")) tex_is_cube[0] = 1;
    if (strstr(src, "uniform samplerCube tex1;")) tex_is_cube[1] = 1;
    if (strstr(src, "uniform samplerCube tex2;")) tex_is_cube[2] = 1;
    if (strstr(src, "uniform samplerCube tex3;")) tex_is_cube[3] = 1;

    char *s = strdup(src);
    char *tmp;

    #define APPLY_REPL(old_s, new_s) do { \
        tmp = replace_all(s, (old_s), (new_s)); \
        if (tmp) { free(s); s = tmp; } \
    } while (0)

    APPLY_REPL("#version 300 es", "#version 100\n#extension GL_OES_standard_derivatives : enable\n");
    APPLY_REPL("#version 310 es", "#version 100\n#extension GL_OES_standard_derivatives : enable\n");
    APPLY_REPL("precision highp sampler2D;", "");
    APPLY_REPL("precision highp sampler3D;", "");
    APPLY_REPL("precision highp samplerCube;", "");
    APPLY_REPL("invariant gl_Position;", "");

    if (is_vs) {
        const char *old_unpack =
            "vec4 unpack_normpacked3(uint p)\n"
            "{\n"
            "\tint x = int(p << 21) >> 21;\n"
            "\tint y = int(p << 10) >> 21;\n"
            "\tint z = int(p) >> 22;\n"
            "\treturn vec4(float(x) / 1023.0, float(y) / 1023.0, float(z) / 511.0, 1.0);\n"
            "}";
        const char *new_unpack =
            "vec4 unpack_normpacked3(float p)\n"
            "{\n"
            "\tfloat x = mod(p, 2048.0);\n"
            "\tif (x >= 1024.0) x -= 2048.0;\n"
            "\tfloat y = mod(floor(p / 2048.0), 2048.0);\n"
            "\tif (y >= 1024.0) y -= 2048.0;\n"
            "\tfloat z = floor(p / 4194304.0);\n"
            "\tif (z >= 512.0) z -= 1024.0;\n"
            "\treturn vec4(x / 1023.0, y / 1023.0, z / 511.0, 1.0);\n"
            "}";
        APPLY_REPL(old_unpack, new_unpack);

        for (int i = 0; i < 16; ++i) {
            char pat_vec[64], pat_uint[64], rep_vec[64], rep_uint[64], attr_name[32];
            snprintf(pat_vec, sizeof(pat_vec), "layout(location = %d) in vec4 v%d_in;", i, i);
            snprintf(rep_vec, sizeof(rep_vec), "attribute vec4 v%d_in;", i);
            if (strstr(s, pat_vec)) {
                snprintf(attr_name, sizeof(attr_name), "v%d_in", i);
                record_shader_attr(shader, i, attr_name);
                APPLY_REPL(pat_vec, rep_vec);
            }
            snprintf(pat_uint, sizeof(pat_uint), "layout(location = %d) in uint v%d_packed;", i, i);
            snprintf(rep_uint, sizeof(rep_uint), "attribute float v%d_packed;", i);
            if (strstr(s, pat_uint)) {
                snprintf(attr_name, sizeof(attr_name), "v%d_packed", i);
                record_shader_attr(shader, i, attr_name);
                APPLY_REPL(pat_uint, rep_uint);
            }
        }
        if (strstr(s, "layout(location = 0) in vec2 position;")) {
            record_shader_attr(shader, 0, "position");
            APPLY_REPL("layout(location = 0) in vec2 position;", "attribute vec2 position;");
        }
        if (strstr(s, "layout(location = 1) in vec2 texcoord;")) {
            record_shader_attr(shader, 1, "texcoord");
            APPLY_REPL("layout(location = 1) in vec2 texcoord;", "attribute vec2 texcoord;");
        }
        if (strstr(s, "layout(location = 2) in vec4 color;")) {
            record_shader_attr(shader, 2, "color");
            APPLY_REPL("layout(location = 2) in vec4 color;", "attribute vec4 color;");
        }

        APPLY_REPL("\nout ", "\nvarying ");
        APPLY_REPL("vec4(lessThan(A, B))",
                   "vec4(A.x < B.x ? 1.0 : 0.0, A.y < B.y ? 1.0 : 0.0, A.z < B.z ? 1.0 : 0.0, A.w < B.w ? 1.0 : 0.0)");
    } else {
        APPLY_REPL("\nin ", "\nvarying ");
        APPLY_REPL("layout(location = 0) out vec4 fragment_color;", "");
        APPLY_REPL("layout(location = 0) out vec4 color;", "");
        APPLY_REPL("fragment_color =", "gl_FragColor =");

        const char *old_fetch =
            "\t\tivec2 size = textureSize(tex0, 0);\n"
            "\t\tivec2 pixel = clamp(ivec2(floor(uv * vec2(size))), ivec2(0), size - 1);\n"
            "\t\tfloat threshold = texelFetch(tex0, pixel, 0).r;";
        const char *new_fetch =
            "\t\tfloat threshold = texture2D(tex0, uv).r;";
        APPLY_REPL(old_fetch, new_fetch);

        for (int t = 0; t < 4; ++t) {
            char pat[32], rep[32];
            snprintf(pat, sizeof(pat), "texture(tex%d,", t);
            snprintf(rep, sizeof(rep), "%s(tex%d,", tex_is_cube[t] ? "textureCube" : "texture2D", t);
            APPLY_REPL(pat, rep);
        }
        APPLY_REPL("texture(", "texture2D(");
    }

    #undef APPLY_REPL
    return s;
}

void GUEST_ABI hostgl_glShaderSource(
    GLuint shader, GLsizei count, const unsigned long long *strings, const GLint *lengths)
{
    if (count <= 0 || !strings) return;
    size_t total_len = 0;
    for (GLsizei i = 0; i < count; ++i) {
        const char *part = (const char *)(uintptr_t)strings[i];
        if (!part) continue;
        total_len += (lengths && lengths[i] >= 0) ? (size_t)lengths[i] : strlen(part);
    }
    char *combined = (char *)malloc(total_len + 1);
    if (!combined) return;
    char *dst = combined;
    for (GLsizei i = 0; i < count; ++i) {
        const char *part = (const char *)(uintptr_t)strings[i];
        if (!part) continue;
        size_t len = (lengths && lengths[i] >= 0) ? (size_t)lengths[i] : strlen(part);
        memcpy(dst, part, len);
        dst += len;
    }
    *dst = '\0';

    if (is_gles2_context() && strstr(combined, "#version 3")) {
        char *translated = translate_es3_to_es2_shader(shader, combined);
        free(combined);
        const char *src_ptr = translated;
        glShaderSource(shader, 1, &src_ptr, NULL);
        free(translated);
        return;
    }

    const char *src_ptr = combined;
    glShaderSource(shader, 1, &src_ptr, NULL);
    free(combined);
}

void GUEST_ABI hostgl_glVertexAttribIPointer(
    GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer)
{
    if (is_gles2_context()) {
        glVertexAttribPointer(index, size, GL_UNSIGNED_INT, GL_FALSE, stride, pointer);
        return;
    }
    glVertexAttribIPointer(index, size, type, stride, pointer);
}

/* =========================================================================
 * 5. Fullscreen Blit Emulation for glBlitFramebuffer on ES 2.0
 * ========================================================================= */
static GLuint s_blit_prog = 0;
static GLuint s_blit_vbo = 0;
static GLint  s_blit_u_tex = -1;

static void ensure_blit_program(void) {
    if (s_blit_prog) return;
    const char *vs_src =
        "#version 100\n"
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_uv;\n"
        "varying vec2 v_uv;\n"
        "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); v_uv = a_uv; }\n";
    const char *fs_src =
        "#version 100\n"
        "precision mediump float;\n"
        "varying vec2 v_uv;\n"
        "uniform sampler2D u_tex;\n"
        "void main() { gl_FragColor = texture2D(u_tex, v_uv); }\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vs_src, NULL);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fs_src, NULL);
    glCompileShader(fs);

    s_blit_prog = glCreateProgram();
    glAttachShader(s_blit_prog, vs);
    glAttachShader(s_blit_prog, fs);
    glBindAttribLocation(s_blit_prog, 0, "a_pos");
    glBindAttribLocation(s_blit_prog, 1, "a_uv");
    glLinkProgram(s_blit_prog);
    s_blit_u_tex = glGetUniformLocation(s_blit_prog, "u_tex");
    glGenBuffers(1, &s_blit_vbo);
}

void GUEST_ABI hostgl_glBlitFramebuffer(
    GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
    GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
    GLbitfield mask, GLenum filter)
{
    if (!is_gles2_context()) {
        glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
        return;
    }
    if (!(mask & GL_COLOR_BUFFER_BIT)) return;

    GLuint src_tex = lookup_fbo_color(s_read_fbo);
    if (!src_tex) return;

    ensure_blit_program();

    GLint prev_prog = 0, prev_vbo = 0, prev_tex = 0, prev_active = 0;
    GLint prev_vp[4];
    GLboolean prev_depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prev_blend = glIsEnabled(GL_BLEND);
    GLboolean prev_cull  = glIsEnabled(GL_CULL_FACE);
    GLboolean prev_sciss = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_vbo);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
    glGetIntegerv(GL_VIEWPORT, prev_vp);

    glBindFramebuffer(GL_FRAMEBUFFER, s_draw_fbo);
    GLint vx = dstX0 < dstX1 ? dstX0 : dstX1;
    GLint vy = dstY0 < dstY1 ? dstY0 : dstY1;
    GLsizei vw = abs(dstX1 - dstX0);
    GLsizei vh = abs(dstY1 - dstY0);
    glViewport(vx, vy, vw, vh);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

    float v0 = (dstY0 > dstY1) ? 1.0f : 0.0f;
    float v1 = (dstY0 > dstY1) ? 0.0f : 1.0f;
    const float quad[16] = {
        -1.0f, -1.0f,  0.0f, v0,
         1.0f, -1.0f,  1.0f, v0,
        -1.0f,  1.0f,  0.0f, v1,
         1.0f,  1.0f,  1.0f, v1,
    };

    glUseProgram(s_blit_prog);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter == GL_LINEAR ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter == GL_LINEAR ? GL_LINEAR : GL_NEAREST);
    glUniform1i(s_blit_u_tex, 0);

    glBindBuffer(GL_ARRAY_BUFFER, s_blit_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)(2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    if (prev_depth) glEnable(GL_DEPTH_TEST);
    if (prev_blend) glEnable(GL_BLEND);
    if (prev_cull)  glEnable(GL_CULL_FACE);
    if (prev_sciss) glEnable(GL_SCISSOR_TEST);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);
    glActiveTexture((GLenum)prev_active);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prev_vbo);
    glUseProgram((GLuint)prev_prog);
}

/* =========================================================================
 * 6. BaseVertex, CopyImageSubData, and Query Overrides
 * ========================================================================= */
void GUEST_ABI hostgl_glDrawElementsBaseVertex(
    GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex)
{
    (void)basevertex;
    glDrawElements(mode, count, type, indices);
}

void GUEST_ABI hostgl_glCopyImageSubData(
    GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ,
    long long dstName, long long dstTarget, long long dstLevel, long long dstX, long long dstY, long long dstZ,
    long long srcWidth, long long srcHeight, long long srcDepth)
{
    (void)srcZ; (void)dstZ; (void)srcDepth;
    GLuint temp_fbo = 0;
    glGenFramebuffers(1, &temp_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, temp_fbo);
    if (srcTarget == GL_RENDERBUFFER) {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, srcName);
    } else {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, srcTarget, srcName, srcLevel);
    }
    if (dstTarget == GL_TEXTURE_2D || (dstTarget >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && dstTarget <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)) {
        GLint prev_tex = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
        glBindTexture(dstTarget == GL_TEXTURE_2D ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP, (GLuint)dstName);
        glCopyTexSubImage2D((GLenum)dstTarget, (GLint)dstLevel, (GLint)dstX, (GLint)dstY, srcX, srcY, (GLsizei)srcWidth, (GLsizei)srcHeight);
        glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, s_draw_fbo);
    glDeleteFramebuffers(1, &temp_fbo);
}

void GUEST_ABI hostgl_glGetIntegerv(GLenum pname, GLint *params) {
    if (!params) return;
    if (pname == GL_MAJOR_VERSION) { *params = 3; return; }
    if (pname == GL_MINOR_VERSION) { *params = 0; return; }
    glGetIntegerv(pname, params);
}
