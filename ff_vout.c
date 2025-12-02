/*
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2024 FFPlayer contributors
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "ff_vout.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* 跨平台 OpenGL 头文件 */
#ifdef _WIN32
    #include <windows.h>
    #include <GL/gl.h>
    #include <GL/glext.h>
#elif defined(__APPLE__)
    #define GL_SILENCE_DEPRECATION
    #include <OpenGL/gl3.h>
#else /* Linux */
    #include <GL/gl.h>
    #include <GL/glext.h>
#endif

#include <SDL_opengl.h>

/* OpenGL 函数指针（用于扩展函数）*/
#ifndef __APPLE__
static PFNGLCREATESHADERPROC            glCreateShader = NULL;
static PFNGLSHADERSOURCEPROC            glShaderSource = NULL;
static PFNGLCOMPILESHADERPROC           glCompileShader = NULL;
static PFNGLGETSHADERIVPROC             glGetShaderiv = NULL;
static PFNGLGETSHADERINFOLOGPROC        glGetShaderInfoLog = NULL;
static PFNGLCREATEPROGRAMPROC           glCreateProgram = NULL;
static PFNGLATTACHSHADERPROC            glAttachShader = NULL;
static PFNGLLINKPROGRAMPROC             glLinkProgram = NULL;
static PFNGLGETPROGRAMIVPROC            glGetProgramiv = NULL;
static PFNGLGETPROGRAMINFOLOGPROC       glGetProgramInfoLog = NULL;
static PFNGLUSEPROGRAMPROC              glUseProgram = NULL;
static PFNGLDELETESHADERPROC            glDeleteShader = NULL;
static PFNGLDELETEPROGRAMPROC           glDeleteProgram = NULL;
static PFNGLGETUNIFORMLOCATIONPROC      glGetUniformLocation = NULL;
static PFNGLUNIFORM1IPROC               glUniform1i = NULL;
static PFNGLUNIFORM4FPROC               glUniform4f = NULL;
static PFNGLUNIFORMMATRIX4FVPROC        glUniformMatrix4fv = NULL;
static PFNGLGENVERTEXARRAYSPROC         glGenVertexArrays = NULL;
static PFNGLDELETEVERTEXARRAYSPROC      glDeleteVertexArrays = NULL;
static PFNGLBINDVERTEXARRAYPROC         glBindVertexArray = NULL;
static PFNGLGENBUFFERSPROC              glGenBuffers = NULL;
static PFNGLDELETEBUFFERSPROC           glDeleteBuffers = NULL;
static PFNGLBINDBUFFERPROC              glBindBuffer = NULL;
static PFNGLBUFFERDATAPROC              glBufferData = NULL;
static PFNGLVERTEXATTRIBPOINTERPROC     glVertexAttribPointer = NULL;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = NULL;
static PFNGLACTIVETEXTUREPROC           glActiveTexture_func = NULL;
static PFNGLBUFFERSUBDATAPROC           glBufferSubData = NULL;
#define glActiveTexture glActiveTexture_func
#endif

/* Shader 源码 */
static const char *vertex_shader_src = 
    "#version 330 core\n"
    "layout (location = 0) in vec2 a_position;\n"
    "layout (location = 1) in vec2 a_texcoord;\n"
    "out vec2 v_texcoord;\n"
    "uniform mat4 u_mvp;\n"
    "void main() {\n"
    "    gl_Position = u_mvp * vec4(a_position, 0.0, 1.0);\n"
    "    v_texcoord = a_texcoord;\n"
    "}\n";

/* YUV420P 转 RGB 的片段着色器 */
static const char *fragment_shader_yuv_src =
    "#version 330 core\n"
    "in vec2 v_texcoord;\n"
    "out vec4 frag_color;\n"
    "uniform sampler2D tex_y;\n"
    "uniform sampler2D tex_u;\n"
    "uniform sampler2D tex_v;\n"
    "void main() {\n"
    "    float y = texture(tex_y, v_texcoord).r;\n"
    "    float u = texture(tex_u, v_texcoord).r - 0.5;\n"
    "    float v = texture(tex_v, v_texcoord).r - 0.5;\n"
    "    /* BT.601 转换 */\n"
    "    float r = y + 1.402 * v;\n"
    "    float g = y - 0.344 * u - 0.714 * v;\n"
    "    float b = y + 1.772 * u;\n"
    "    frag_color = vec4(r, g, b, 1.0);\n"
    "}\n";

/* RGBA 纹理的片段着色器 */
static const char *fragment_shader_rgba_src =
    "#version 330 core\n"
    "in vec2 v_texcoord;\n"
    "out vec4 frag_color;\n"
    "uniform sampler2D tex_rgba;\n"
    "void main() {\n"
    "    frag_color = texture(tex_rgba, v_texcoord);\n"
    "}\n";

/* 纯色填充的片段着色器 */
static const char *fragment_shader_color_src =
    "#version 330 core\n"
    "out vec4 frag_color;\n"
    "uniform vec4 u_color;\n"
    "void main() {\n"
    "    frag_color = u_color;\n"
    "}\n";

/* 纹理结构 */
struct FFVoutTexture {
    GLuint tex_id[3];       /* OpenGL 纹理 ID (YUV 或单个 RGBA) */
    int width;
    int height;
    FFVoutPixelFormat format;
    uint8_t *lock_buffer;   /* 锁定缓冲区 */
    int lock_pitch;
};

/* 视频输出上下文 */
struct FFVout {
    SDL_Window *window;
    SDL_GLContext gl_ctx;
    
    /* 视口大小 */
    int viewport_width;
    int viewport_height;
    
    /* 缩放模式 */
    FFVoutScaleMode scale_mode;
    
    /* Shader 程序 */
    GLuint program_yuv;     /* YUV420P 渲染 */
    GLuint program_rgba;    /* RGBA 渲染 */
    GLuint program_color;   /* 纯色填充 */
    
    /* 顶点数据 */
    GLuint vao;
    GLuint vbo;
    
    /* 当前绘制颜色 */
    float draw_color[4];
};

/* 加载 OpenGL 扩展函数 */
static int load_gl_functions(void)
{
#ifndef __APPLE__
    glCreateShader = (PFNGLCREATESHADERPROC)SDL_GL_GetProcAddress("glCreateShader");
    glShaderSource = (PFNGLSHADERSOURCEPROC)SDL_GL_GetProcAddress("glShaderSource");
    glCompileShader = (PFNGLCOMPILESHADERPROC)SDL_GL_GetProcAddress("glCompileShader");
    glGetShaderiv = (PFNGLGETSHADERIVPROC)SDL_GL_GetProcAddress("glGetShaderiv");
    glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)SDL_GL_GetProcAddress("glGetShaderInfoLog");
    glCreateProgram = (PFNGLCREATEPROGRAMPROC)SDL_GL_GetProcAddress("glCreateProgram");
    glAttachShader = (PFNGLATTACHSHADERPROC)SDL_GL_GetProcAddress("glAttachShader");
    glLinkProgram = (PFNGLLINKPROGRAMPROC)SDL_GL_GetProcAddress("glLinkProgram");
    glGetProgramiv = (PFNGLGETPROGRAMIVPROC)SDL_GL_GetProcAddress("glGetProgramiv");
    glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)SDL_GL_GetProcAddress("glGetProgramInfoLog");
    glUseProgram = (PFNGLUSEPROGRAMPROC)SDL_GL_GetProcAddress("glUseProgram");
    glDeleteShader = (PFNGLDELETESHADERPROC)SDL_GL_GetProcAddress("glDeleteShader");
    glDeleteProgram = (PFNGLDELETEPROGRAMPROC)SDL_GL_GetProcAddress("glDeleteProgram");
    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)SDL_GL_GetProcAddress("glGetUniformLocation");
    glUniform1i = (PFNGLUNIFORM1IPROC)SDL_GL_GetProcAddress("glUniform1i");
    glUniform4f = (PFNGLUNIFORM4FPROC)SDL_GL_GetProcAddress("glUniform4f");
    glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)SDL_GL_GetProcAddress("glUniformMatrix4fv");
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)SDL_GL_GetProcAddress("glGenVertexArrays");
    glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)SDL_GL_GetProcAddress("glDeleteVertexArrays");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)SDL_GL_GetProcAddress("glBindVertexArray");
    glGenBuffers = (PFNGLGENBUFFERSPROC)SDL_GL_GetProcAddress("glGenBuffers");
    glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteBuffers");
    glBindBuffer = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
    glBufferData = (PFNGLBUFFERDATAPROC)SDL_GL_GetProcAddress("glBufferData");
    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)SDL_GL_GetProcAddress("glVertexAttribPointer");
    glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)SDL_GL_GetProcAddress("glEnableVertexAttribArray");
    glActiveTexture_func = (PFNGLACTIVETEXTUREPROC)SDL_GL_GetProcAddress("glActiveTexture");
    glBufferSubData = (PFNGLBUFFERSUBDATAPROC)SDL_GL_GetProcAddress("glBufferSubData");
    
    if (!glCreateShader || !glShaderSource || !glCompileShader ||
        !glCreateProgram || !glAttachShader || !glLinkProgram ||
        !glUseProgram || !glGenVertexArrays || !glGenBuffers || !glBufferSubData) {
        fprintf(stderr, "Failed to load OpenGL functions\n");
        return -1;
    }
#endif
    return 0;
}

/* 编译着色器 */
static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(shader, 512, NULL, info_log);
        fprintf(stderr, "Shader compilation failed: %s\n", info_log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

/* 创建着色器程序 */
static GLuint create_program(const char *vs_src, const char *fs_src)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    if (!vs) return 0;
    
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }
    
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(program, 512, NULL, info_log);
        fprintf(stderr, "Program linking failed: %s\n", info_log);
        glDeleteProgram(program);
        program = 0;
    }
    
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

/* 初始化顶点数据 */
static int init_vertex_data(FFVout *vout)
{
    /* 全屏四边形顶点: position(x,y) + texcoord(s,t) */
    float vertices[] = {
        /* 位置        纹理坐标 */
        -1.0f, -1.0f,  0.0f, 1.0f,  /* 左下 */
         1.0f, -1.0f,  1.0f, 1.0f,  /* 右下 */
         1.0f,  1.0f,  1.0f, 0.0f,  /* 右上 */
        -1.0f, -1.0f,  0.0f, 1.0f,  /* 左下 */
         1.0f,  1.0f,  1.0f, 0.0f,  /* 右上 */
        -1.0f,  1.0f,  0.0f, 0.0f,  /* 左上 */
    };
    
    glGenVertexArrays(1, &vout->vao);
    glGenBuffers(1, &vout->vbo);
    
    glBindVertexArray(vout->vao);
    glBindBuffer(GL_ARRAY_BUFFER, vout->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    
    /* 位置属性 */
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    /* 纹理坐标属性 */
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindVertexArray(0);
    return 0;
}

/*
 * =============================================================================
 * 公共 API 实现
 * =============================================================================
 */

FFVout *vout_create(SDL_Window *window)
{
    if (!window)
        return NULL;
    
    FFVout *vout = (FFVout *)calloc(1, sizeof(FFVout));
    if (!vout)
        return NULL;
    
    vout->window = window;
    
    /* 设置 OpenGL 属性 */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    
    /* 创建 OpenGL 上下文 */
    vout->gl_ctx = SDL_GL_CreateContext(window);
    if (!vout->gl_ctx) {
        fprintf(stderr, "Failed to create OpenGL context: %s\n", SDL_GetError());
        free(vout);
        return NULL;
    }
    
    SDL_GL_MakeCurrent(window, vout->gl_ctx);
    SDL_GL_SetSwapInterval(1); /* 开启垂直同步 */
    
    /* 加载 OpenGL 扩展函数 */
    if (load_gl_functions() < 0) {
        SDL_GL_DeleteContext(vout->gl_ctx);
        free(vout);
        return NULL;
    }
    
    /* 打印 OpenGL 信息 */
    printf("OpenGL Vendor: %s\n", glGetString(GL_VENDOR));
    printf("OpenGL Renderer: %s\n", glGetString(GL_RENDERER));
    printf("OpenGL Version: %s\n", glGetString(GL_VERSION));
    
    /* 创建着色器程序 */
    vout->program_yuv = create_program(vertex_shader_src, fragment_shader_yuv_src);
    vout->program_rgba = create_program(vertex_shader_src, fragment_shader_rgba_src);
    vout->program_color = create_program(vertex_shader_src, fragment_shader_color_src);
    
    if (!vout->program_yuv || !vout->program_rgba || !vout->program_color) {
        fprintf(stderr, "Failed to create shader programs\n");
        vout_destroy(vout);
        return NULL;
    }
    
    /* 初始化顶点数据 */
    if (init_vertex_data(vout) < 0) {
        vout_destroy(vout);
        return NULL;
    }
    
    /* 获取初始窗口大小 */
    SDL_GetWindowSize(window, &vout->viewport_width, &vout->viewport_height);
    glViewport(0, 0, vout->viewport_width, vout->viewport_height);
    
    /* 启用混合 */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    /* 默认绘制颜色：白色 */
    vout->draw_color[0] = 1.0f;
    vout->draw_color[1] = 1.0f;
    vout->draw_color[2] = 1.0f;
    vout->draw_color[3] = 1.0f;
    
    /* 默认缩放模式：保持宽高比 */
    vout->scale_mode = VOUT_SCALE_ASPECT_FILL;
    
    return vout;
}

void vout_destroy(FFVout *vout)
{
    if (!vout)
        return;
    
    if (vout->vao) glDeleteVertexArrays(1, &vout->vao);
    if (vout->vbo) glDeleteBuffers(1, &vout->vbo);
    if (vout->program_yuv) glDeleteProgram(vout->program_yuv);
    if (vout->program_rgba) glDeleteProgram(vout->program_rgba);
    if (vout->program_color) glDeleteProgram(vout->program_color);
    
    if (vout->gl_ctx)
        SDL_GL_DeleteContext(vout->gl_ctx);
    
    free(vout);
}

void vout_set_size(FFVout *vout, int width, int height)
{
    if (!vout)
        return;
    
    vout->viewport_width = width;
    vout->viewport_height = height;
    glViewport(0, 0, width, height);
}

void vout_get_size(FFVout *vout, int *width, int *height)
{
    if (!vout)
        return;
    if (width) *width = vout->viewport_width;
    if (height) *height = vout->viewport_height;
}

void vout_set_scale_mode(FFVout *vout, FFVoutScaleMode mode)
{
    if (!vout)
        return;
    vout->scale_mode = mode;
}

FFVoutScaleMode vout_get_scale_mode(FFVout *vout)
{
    if (!vout)
        return VOUT_SCALE_ASPECT_FIT;
    return vout->scale_mode;
}

/*
 * =============================================================================
 * 纹理管理
 * =============================================================================
 */

FFVoutTexture *vout_texture_create(FFVout *vout, int width, int height, 
                                   FFVoutPixelFormat format)
{
    if (!vout || width <= 0 || height <= 0)
        return NULL;
    
    FFVoutTexture *tex = (FFVoutTexture *)calloc(1, sizeof(FFVoutTexture));
    if (!tex)
        return NULL;
    
    tex->width = width;
    tex->height = height;
    tex->format = format;
    
    if (format == VOUT_FMT_YUV420P) {
        /* 创建 Y, U, V 三个纹理 */
        glGenTextures(3, tex->tex_id);
        
        for (int i = 0; i < 3; i++) {
            int tex_w = (i == 0) ? width : width / 2;
            int tex_h = (i == 0) ? height : height / 2;
            
            glBindTexture(GL_TEXTURE_2D, tex->tex_id[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, tex_w, tex_h, 0, 
                        GL_RED, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
    } else {
        /* RGBA/BGRA: 创建单个纹理 */
        glGenTextures(1, tex->tex_id);
        glBindTexture(GL_TEXTURE_2D, tex->tex_id[0]);
        
        GLenum internal_format = GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void vout_texture_destroy(FFVoutTexture *texture)
{
    if (!texture)
        return;
    
    if (texture->format == VOUT_FMT_YUV420P) {
        glDeleteTextures(3, texture->tex_id);
    } else {
        glDeleteTextures(1, texture->tex_id);
    }
    
    if (texture->lock_buffer)
        free(texture->lock_buffer);
    
    free(texture);
}

int vout_texture_realloc(FFVout *vout, FFVoutTexture **texture,
                         int width, int height, FFVoutPixelFormat format)
{
    if (!vout || !texture)
        return -1;
    
    FFVoutTexture *tex = *texture;
    
    /* 检查是否需要重新分配 */
    if (tex && tex->width == width && tex->height == height && tex->format == format)
        return 0;
    
    /* 销毁旧纹理 */
    if (tex) {
        vout_texture_destroy(tex);
        *texture = NULL;
    }
    
    /* 创建新纹理 */
    *texture = vout_texture_create(vout, width, height, format);
    return (*texture) ? 0 : -1;
}

int vout_texture_upload_yuv420p(FFVoutTexture *texture,
                                const uint8_t *y_data, int y_pitch,
                                const uint8_t *u_data, int u_pitch,
                                const uint8_t *v_data, int v_pitch,
                                int width, int height)
{
    if (!texture || texture->format != VOUT_FMT_YUV420P)
        return -1;
    
    /* 上传 Y 平面 */
    glBindTexture(GL_TEXTURE_2D, texture->tex_id[0]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, y_pitch);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                   GL_RED, GL_UNSIGNED_BYTE, y_data);
    
    /* 上传 U 平面 */
    glBindTexture(GL_TEXTURE_2D, texture->tex_id[1]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, u_pitch);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width/2, height/2,
                   GL_RED, GL_UNSIGNED_BYTE, u_data);
    
    /* 上传 V 平面 */
    glBindTexture(GL_TEXTURE_2D, texture->tex_id[2]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, v_pitch);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width/2, height/2,
                   GL_RED, GL_UNSIGNED_BYTE, v_data);
    
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return 0;
}

int vout_texture_upload_rgba(FFVoutTexture *texture, 
                             const uint8_t *pixels, int pitch,
                             int width, int height)
{
    if (!texture || (texture->format != VOUT_FMT_RGBA && 
                     texture->format != VOUT_FMT_BGRA))
        return -1;
    
    glBindTexture(GL_TEXTURE_2D, texture->tex_id[0]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
    
    GLenum format = (texture->format == VOUT_FMT_BGRA) ? GL_BGRA : GL_RGBA;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                   format, GL_UNSIGNED_BYTE, pixels);
    
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return 0;
}

int vout_texture_lock(FFVoutTexture *texture, void **pixels, int *pitch)
{
    if (!texture || !pixels || !pitch)
        return -1;
    
    /* 分配临时缓冲区 */
    int bytes_per_pixel = (texture->format == VOUT_FMT_YUV420P) ? 1 : 4;
    int buffer_pitch = texture->width * bytes_per_pixel;
    int buffer_size = buffer_pitch * texture->height;
    
    if (!texture->lock_buffer) {
        texture->lock_buffer = (uint8_t *)malloc(buffer_size);
        if (!texture->lock_buffer)
            return -1;
    }
    
    texture->lock_pitch = buffer_pitch;
    *pixels = texture->lock_buffer;
    *pitch = buffer_pitch;
    return 0;
}

void vout_texture_unlock(FFVoutTexture *texture)
{
    if (!texture || !texture->lock_buffer)
        return;
    
    /* 将锁定缓冲区数据上传到纹理 */
    if (texture->format == VOUT_FMT_RGBA || texture->format == VOUT_FMT_BGRA) {
        vout_texture_upload_rgba(texture, texture->lock_buffer, 
                                texture->lock_pitch,
                                texture->width, texture->height);
    }
}

void vout_texture_get_size(FFVoutTexture *texture, int *width, int *height)
{
    if (!texture)
        return;
    if (width) *width = texture->width;
    if (height) *height = texture->height;
}

/*
 * =============================================================================
 * 渲染操作
 * =============================================================================
 */

void vout_render_begin(FFVout *vout)
{
    if (!vout)
        return;
    
    SDL_GL_MakeCurrent(vout->window, vout->gl_ctx);
}

void vout_clear(FFVout *vout, uint8_t r, uint8_t g, uint8_t b)
{
    if (!vout)
        return;
    
    glClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void vout_set_draw_color(FFVout *vout, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (!vout)
        return;
    
    vout->draw_color[0] = r / 255.0f;
    vout->draw_color[1] = g / 255.0f;
    vout->draw_color[2] = b / 255.0f;
    vout->draw_color[3] = a / 255.0f;
}

/* 辅助函数：根据缩放模式计算目标矩形 */
static void calculate_scaled_rect(FFVout *vout, FFVoutTexture *texture,
                                  const FFVoutRect *dst_rect, FFVoutRect *scaled_rect)
{
    int tex_w = texture->width;
    int tex_h = texture->height;
    
    /* 使用整个视口作为目标区域 */
    int viewport_w = vout->viewport_width;
    int viewport_h = vout->viewport_height;
    
    (void)dst_rect; /* 忽略传入的 dst_rect，使用视口大小 */
    
    switch (vout->scale_mode) {
    case VOUT_SCALE_STRETCH:
        /* 拉伸填满，不保持宽高比 */
        scaled_rect->x = 0;
        scaled_rect->y = 0;
        scaled_rect->w = viewport_w;
        scaled_rect->h = viewport_h;
        break;
        
    case VOUT_SCALE_ASPECT_FILL:
        /* 保持宽高比，填满窗口（裁剪） */
        {
            float scale_w = (float)viewport_w / tex_w;
            float scale_h = (float)viewport_h / tex_h;
            float scale = (scale_w > scale_h) ? scale_w : scale_h;
            
            int new_w = (int)(tex_w * scale);
            int new_h = (int)(tex_h * scale);
            
            scaled_rect->x = (viewport_w - new_w) / 2;
            scaled_rect->y = (viewport_h - new_h) / 2;
            scaled_rect->w = new_w;
            scaled_rect->h = new_h;
        }
        break;
        
    case VOUT_SCALE_ASPECT_FIT:
    default:
        /* 保持宽高比，适应窗口（默认） */
        {
            float scale_w = (float)viewport_w / tex_w;
            float scale_h = (float)viewport_h / tex_h;
            float scale = (scale_w < scale_h) ? scale_w : scale_h;
            
            int new_w = (int)(tex_w * scale);
            int new_h = (int)(tex_h * scale);
            
            scaled_rect->x = (viewport_w - new_w) / 2;
            scaled_rect->y = (viewport_h - new_h) / 2;
            scaled_rect->w = new_w;
            scaled_rect->h = new_h;
        }
        break;
    }
}

/* 辅助函数：更新顶点缓冲区 */
static void update_vertices(FFVout *vout, const FFVoutRect *dst_rect, int flip_v)
{
    float x1 = 2.0f * dst_rect->x / vout->viewport_width - 1.0f;
    float y1 = 1.0f - 2.0f * dst_rect->y / vout->viewport_height;
    float x2 = 2.0f * (dst_rect->x + dst_rect->w) / vout->viewport_width - 1.0f;
    float y2 = 1.0f - 2.0f * (dst_rect->y + dst_rect->h) / vout->viewport_height;
    
    float t_top = flip_v ? 1.0f : 0.0f;
    float t_bottom = flip_v ? 0.0f : 1.0f;
    
    float vertices[] = {
        x1, y2, 0.0f, t_bottom,
        x2, y2, 1.0f, t_bottom,
        x2, y1, 1.0f, t_top,
        x1, y2, 0.0f, t_bottom,
        x2, y1, 1.0f, t_top,
        x1, y1, 0.0f, t_top,
    };
    
    glBindBuffer(GL_ARRAY_BUFFER, vout->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
}

void vout_fill_rect(FFVout *vout, int x, int y, int w, int h)
{
    if (!vout || w <= 0 || h <= 0)
        return;
    
    FFVoutRect rect = {x, y, w, h};
    update_vertices(vout, &rect, 0);
    
    glUseProgram(vout->program_color);
    GLint loc = glGetUniformLocation(vout->program_color, "u_color");
    glUniform4f(loc, vout->draw_color[0], vout->draw_color[1], 
                vout->draw_color[2], vout->draw_color[3]);
    
    /* 单位矩阵 */
    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    loc = glGetUniformLocation(vout->program_color, "u_mvp");
    glUniformMatrix4fv(loc, 1, GL_FALSE, identity);
    
    glBindVertexArray(vout->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void vout_draw_texture(FFVout *vout, FFVoutTexture *texture,
                       const FFVoutRect *src_rect,
                       const FFVoutRect *dst_rect,
                       int flip_v)
{
    if (!vout || !texture || !dst_rect)
        return;
    
    (void)src_rect; /* 暂不处理源矩形裁剪 */
    
    /* 根据缩放模式计算实际渲染矩形 */
    FFVoutRect scaled_rect;
    calculate_scaled_rect(vout, texture, dst_rect, &scaled_rect);
    update_vertices(vout, &scaled_rect, flip_v);
    
    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    
    if (texture->format == VOUT_FMT_YUV420P) {
        glUseProgram(vout->program_yuv);
        
        /* 绑定 Y, U, V 纹理 */
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture->tex_id[0]);
        glUniform1i(glGetUniformLocation(vout->program_yuv, "tex_y"), 0);
        
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texture->tex_id[1]);
        glUniform1i(glGetUniformLocation(vout->program_yuv, "tex_u"), 1);
        
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, texture->tex_id[2]);
        glUniform1i(glGetUniformLocation(vout->program_yuv, "tex_v"), 2);
        
        glUniformMatrix4fv(glGetUniformLocation(vout->program_yuv, "u_mvp"), 
                          1, GL_FALSE, identity);
    } else {
        glUseProgram(vout->program_rgba);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture->tex_id[0]);
        glUniform1i(glGetUniformLocation(vout->program_rgba, "tex_rgba"), 0);
        
        glUniformMatrix4fv(glGetUniformLocation(vout->program_rgba, "u_mvp"), 
                          1, GL_FALSE, identity);
    }
    
    glBindVertexArray(vout->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void vout_draw_texture_blend(FFVout *vout, FFVoutTexture *texture,
                             const FFVoutRect *src_rect,
                             const FFVoutRect *dst_rect)
{
    if (!vout || !texture || !dst_rect)
        return;
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    vout_draw_texture(vout, texture, src_rect, dst_rect, 0);
}

void vout_render_present(FFVout *vout)
{
    if (!vout)
        return;
    
    SDL_GL_SwapWindow(vout->window);
}

