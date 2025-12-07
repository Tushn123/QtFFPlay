/*
 * 平台相关的窗口嵌入实现
 */

#include "platform_embed.h"
#include <SDL_syswm.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <Cocoa/Cocoa.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#endif

int embed_sdl_window(SDL_Window *sdl_window, void *parent_handle, int width, int height)
{
    if (!sdl_window || !parent_handle)
        return -1;

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    
    if (!SDL_GetWindowWMInfo(sdl_window, &wmInfo)) {
        SDL_Log("Failed to get SDL window info: %s", SDL_GetError());
        return -1;
    }

#ifdef _WIN32
    /* ===== Windows 实现 ===== */
    HWND sdl_hwnd = wmInfo.info.win.window;
    HWND parent_hwnd = (HWND)parent_handle;
    
    SDL_Log("[EMBED] SDL HWND=%p, Parent HWND=%p", sdl_hwnd, parent_hwnd);
    
    // 检查窗口是否有效
    if (!IsWindow(sdl_hwnd) || !IsWindow(parent_hwnd)) {
        SDL_Log("[EMBED] ERROR: Invalid window handle!");
        return -1;
    }
    
    // 设置为子窗口
    HWND old_parent = SetParent(sdl_hwnd, parent_hwnd);
    SDL_Log("[EMBED] SetParent result: old_parent=%p", old_parent);
    
    // 修改窗口样式为子窗口
    LONG old_style = GetWindowLong(sdl_hwnd, GWL_STYLE);
    LONG new_style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS;
    SetWindowLong(sdl_hwnd, GWL_STYLE, new_style);
    SDL_Log("[EMBED] Style changed: 0x%lX -> 0x%lX", old_style, new_style);
    
    // 设置窗口位置和大小
    BOOL pos_result = SetWindowPos(sdl_hwnd, HWND_TOP, 0, 0, width, height, 
                                   SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    SDL_Log("[EMBED] SetWindowPos result: %d", pos_result);
    
    // 显式显示
    ShowWindow(sdl_hwnd, SW_SHOW);
    SDL_ShowWindow(sdl_window);
    
    // 验证最终状态
    RECT rect;
    GetWindowRect(sdl_hwnd, &rect);
    SDL_Log("[EMBED] Final rect: (%ld,%ld)-(%ld,%ld), visible=%d", 
            rect.left, rect.top, rect.right, rect.bottom,
            IsWindowVisible(sdl_hwnd));
    
    return 0;

#elif defined(__APPLE__)
    /* ===== macOS 实现 ===== */
    /* TODO: macOS 窗口嵌入需要 Objective-C 代码，暂时使用独立窗口 */
    SDL_Log("Warning: macOS window embedding not yet implemented, using standalone window");
    SDL_ShowWindow(sdl_window);
    SDL_SetWindowSize(sdl_window, width, height);
    return 0;

#elif defined(__linux__)
    /* ===== Linux 实现 ===== */
    if (wmInfo.subsystem == SDL_SYSWM_X11) {
        /* X11 实现 */
        Window sdl_window_id = wmInfo.info.x11.window;
        Window parent_window_id = (Window)(uintptr_t)parent_handle;
        Display *display = wmInfo.info.x11.display;
        
        // 设置为子窗口
        XReparentWindow(display, sdl_window_id, parent_window_id, 0, 0);
        
        // 调整大小
        XResizeWindow(display, sdl_window_id, width, height);
        
        // 显示窗口
        XMapWindow(display, sdl_window_id);
        XFlush(display);
        
        SDL_Log("SDL window embedded (X11): %dx%d", width, height);
        return 0;
        
    } else if (wmInfo.subsystem == SDL_SYSWM_WAYLAND) {
        /* Wayland 不支持窗口嵌入，使用独立窗口 */
        SDL_Log("Warning: Wayland does not support window embedding, using standalone window");
        SDL_ShowWindow(sdl_window);
        SDL_SetWindowSize(sdl_window, width, height);
        return 0;
    }
    
    SDL_Log("Unsupported window system on Linux");
    return -1;

#else
    /* ===== 未知平台 ===== */
    SDL_Log("Warning: Window embedding not implemented for this platform");
    SDL_ShowWindow(sdl_window);
    SDL_SetWindowSize(sdl_window, width, height);
    return -1;
#endif
}

int resize_embedded_window(SDL_Window *sdl_window, int width, int height)
{
    if (!sdl_window)
        return -1;

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    
    if (!SDL_GetWindowWMInfo(sdl_window, &wmInfo)) {
        return -1;
    }

#ifdef _WIN32
    HWND sdl_hwnd = wmInfo.info.win.window;
    SetWindowPos(sdl_hwnd, NULL, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;

#elif defined(__APPLE__)
    /* TODO: macOS 调整大小实现 */
    SDL_SetWindowSize(sdl_window, width, height);
    return 0;

#elif defined(__linux__)
    if (wmInfo.subsystem == SDL_SYSWM_X11) {
        Window sdl_window_id = wmInfo.info.x11.window;
        Display *display = wmInfo.info.x11.display;
        XResizeWindow(display, sdl_window_id, width, height);
        XFlush(display);
        return 0;
    }
    SDL_SetWindowSize(sdl_window, width, height);
    return 0;

#else
    SDL_SetWindowSize(sdl_window, width, height);
    return 0;
#endif
}

