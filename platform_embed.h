/*
 * 平台相关的窗口嵌入辅助模块
 * 提供跨平台的 SDL 窗口嵌入功能
 */

#ifndef PLATFORM_EMBED_H
#define PLATFORM_EMBED_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 将 SDL 窗口嵌入到原生父窗口中
 * sdl_window: SDL 窗口指针
 * parent_handle: 父窗口句柄 (HWND / NSView* / Window)
 * width: 窗口宽度
 * height: 窗口高度
 * 返回: 0 成功，-1 失败
 */
int embed_sdl_window(SDL_Window *sdl_window, void *parent_handle, int width, int height);

/*
 * 调整嵌入的 SDL 窗口大小
 * sdl_window: SDL 窗口指针
 * width: 新宽度
 * height: 新高度
 * 返回: 0 成功，-1 失败
 */
int resize_embedded_window(SDL_Window *sdl_window, int width, int height);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EMBED_H */

