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

#ifndef FF_VOUT_H
#define FF_VOUT_H

#include <stdint.h>
#include <SDL.h>

/*
 * =============================================================================
 * 视频输出抽象层 (Video Output)
 * 
 * 封装 OpenGL 渲染，提供跨平台的视频输出接口
 * =============================================================================
 */

/* 前向声明 */
typedef struct FFVout FFVout;
typedef struct FFVoutTexture FFVoutTexture;

/* 像素格式 */
typedef enum FFVoutPixelFormat {
    VOUT_FMT_UNKNOWN = 0,
    VOUT_FMT_YUV420P,       /* YUV420 平面格式 (最常用) */
    VOUT_FMT_NV12,          /* NV12 半平面格式 */
    VOUT_FMT_BGRA,          /* BGRA 打包格式 */
    VOUT_FMT_RGBA,          /* RGBA 打包格式 */
} FFVoutPixelFormat;

/* 显示缩放模式 */
typedef enum FFVoutScaleMode {
    VOUT_SCALE_ASPECT_FIT = 0,  /* 保持宽高比，适应窗口（默认）*/
    VOUT_SCALE_ASPECT_FILL,     /* 保持宽高比，填满窗口（裁剪）*/
    VOUT_SCALE_STRETCH,         /* 拉伸填满窗口（不保持宽高比）*/
} FFVoutScaleMode;

/* 矩形区域 */
typedef struct FFVoutRect {
    int x, y;
    int w, h;
} FFVoutRect;

/*
 * =============================================================================
 * 上下文管理
 * =============================================================================
 */

/**
 * 创建视频输出上下文
 * @param window SDL 窗口（用于创建 OpenGL 上下文）
 * @return 成功返回 FFVout 指针，失败返回 NULL
 */
FFVout *vout_create(SDL_Window *window);

/**
 * 销毁视频输出上下文
 */
void vout_destroy(FFVout *vout);

/**
 * 窗口大小改变时调用
 * @param width  新宽度
 * @param height 新高度
 */
void vout_set_size(FFVout *vout, int width, int height);

/**
 * 获取当前视口大小
 */
void vout_get_size(FFVout *vout, int *width, int *height);

/**
 * 设置缩放模式
 * @param mode 缩放模式
 */
void vout_set_scale_mode(FFVout *vout, FFVoutScaleMode mode);

/**
 * 获取当前缩放模式
 */
FFVoutScaleMode vout_get_scale_mode(FFVout *vout);

/*
 * =============================================================================
 * 纹理管理
 * =============================================================================
 */

/**
 * 创建纹理
 * @param vout   视频输出上下文
 * @param width  纹理宽度
 * @param height 纹理高度
 * @param format 像素格式
 * @return 成功返回纹理指针，失败返回 NULL
 */
FFVoutTexture *vout_texture_create(FFVout *vout, int width, int height, 
                                   FFVoutPixelFormat format);

/**
 * 销毁纹理
 */
void vout_texture_destroy(FFVoutTexture *texture);

/**
 * 重新分配纹理（如果尺寸或格式变化）
 * @return 0 成功，-1 失败
 */
int vout_texture_realloc(FFVout *vout, FFVoutTexture **texture,
                         int width, int height, FFVoutPixelFormat format);

/**
 * 上传 YUV420P 数据到纹理
 * @return 0 成功，-1 失败
 */
int vout_texture_upload_yuv420p(FFVoutTexture *texture,
                                const uint8_t *y_data, int y_pitch,
                                const uint8_t *u_data, int u_pitch,
                                const uint8_t *v_data, int v_pitch,
                                int width, int height);

/**
 * 上传 RGBA/BGRA 数据到纹理
 * @return 0 成功，-1 失败
 */
int vout_texture_upload_rgba(FFVoutTexture *texture, 
                             const uint8_t *pixels, int pitch,
                             int width, int height);

/**
 * 锁定纹理用于直接写入（主要用于字幕渲染）
 * @param pixels 输出：像素数据指针
 * @param pitch  输出：每行字节数
 * @return 0 成功，-1 失败
 */
int vout_texture_lock(FFVoutTexture *texture, void **pixels, int *pitch);

/**
 * 解锁纹理
 */
void vout_texture_unlock(FFVoutTexture *texture);

/**
 * 获取纹理尺寸
 */
void vout_texture_get_size(FFVoutTexture *texture, int *width, int *height);

/*
 * =============================================================================
 * 渲染操作
 * =============================================================================
 */

/**
 * 开始一帧渲染
 */
void vout_render_begin(FFVout *vout);

/**
 * 清屏
 * @param r, g, b 颜色分量 (0-255)
 */
void vout_clear(FFVout *vout, uint8_t r, uint8_t g, uint8_t b);

/**
 * 设置绘制颜色
 */
void vout_set_draw_color(FFVout *vout, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

/**
 * 填充矩形
 */
void vout_fill_rect(FFVout *vout, int x, int y, int w, int h);

/**
 * 绘制纹理
 * @param texture  纹理
 * @param src_rect 源矩形（NULL = 整个纹理）
 * @param dst_rect 目标矩形
 * @param flip_v   是否垂直翻转
 */
void vout_draw_texture(FFVout *vout, FFVoutTexture *texture,
                       const FFVoutRect *src_rect,
                       const FFVoutRect *dst_rect,
                       int flip_v);

/**
 * 绘制纹理（带混合模式，用于字幕）
 */
void vout_draw_texture_blend(FFVout *vout, FFVoutTexture *texture,
                             const FFVoutRect *src_rect,
                             const FFVoutRect *dst_rect);

/**
 * 结束一帧渲染并显示
 */
void vout_render_present(FFVout *vout);

#endif /* FF_VOUT_H */

