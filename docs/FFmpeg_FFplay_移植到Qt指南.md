# FFplay 移植到 Qt 工程指南

本文档详细说明如何将 FFmpeg 的 ffplay.c 移植到 Qt 工程中。提供了 5.1.2和6.1.1两个版本的代码分支。

**注意：**FFmpeg 4 的移植难度简单。而FFmpeg 5 之后，代码结构出现大量改动，ffplay想要单独分离出来会涉及十几个文件和很多坑。所以保留 5 和 6 两个版本的移植代码。

---

## 环境要求

### 必需组件

1. **Qt 5.12 或更高版本**
   - MinGW 或 MSVC 2017/2019 编译器，64-bit
   
2. **FFmpeg 6.1.1或FFmpeg 5.1.2**
   - 预编译的库文件（.lib 或 .dll.a）
   - 头文件
   - DLL 文件

3. **SDL2 2.0.22**
   - 开发库和头文件
   - DLL 文件

4. **FFmpeg源码**
   - 用于提取 fftools 相关源文件。

---

### 前期准备

1. **FFmpeg编译**
   - 方法一：如果你想开箱即用，可以下载 **Gyan.dev** 编译好的FFmpeg（功能全面、更新及时、库文件丰富，**推荐**），https://github.com/GyanD/codexffmpeg/releases?q=&expanded=true
   - 方法二：如果你想定制化编译FFmpeg，可以通过 **Btbn** 自行编译（优于自己搭环境编译），https://github.com/BtbN/FFmpeg-Builds
2. **SDL2 编译**
   - SDL编译难度不大，可直接使用本代码中的相关库。

---

## 项目目录结构

```
qt_ffplay/
├── bin/                          # 可执行文件输出目录
│   ├── qt_ffplay.exe
│   ├── avcodec-60.dll
│   ├── avdevice-60.dll
│   ├── avfilter-9.dll
│   ├── avformat-60.dll
│   ├── avutil-58.dll
│   ├── postproc-57.dll
│   ├── swresample-4.dll
│   ├── swscale-7.dll
│   └── SDL2.dll
│
├── include/                      # 头文件和源文件目录
│   ├── ffmpeg/
│   │   ├── libavcodec/           # FFmpeg 库头文件
│   │   ├── libavdevice/
│   │   ├── libavfilter/
│   │   ├── libavformat/
│   │   ├── libavutil/			  # 缺少哪个文件则从源码拷贝
│   │   │   ├── intmath.h		  # 如果没有，从源码拷贝
│   │   │   ├── internal.h		  # 如果没有，从源码拷贝
│   │   │   ├── libm.h			  # 如果没有，从源码拷贝
│   │   │   ├── getenv_utf8.h	  # 如果没有，从源码拷贝
│   │   │   ├── wchar_filename.h  # 如果没有，从源码拷贝
│   │   │   └── ffversion.h       # 如果没有，需./configure编译和make后，从源码拷贝。或者手动创建
│   │   ├── libpostproc/
│   │   ├── libswresample/
│   │   ├── libswscale/
│   │   │
│   │   ├── compat/               # 兼容性头文件
│   │   │   ├── os2threads.h
│   │   │   ├── va_copy.h
│   │   │   ├── w32dlfcn.h
│   │   │   └── w32pthreads.h
│   │   │
│   │   ├──             # 从源码拷贝
│   │   ├── cmdutils.h            # 从源码拷贝
│   │   ├──           # 从源码拷贝
│   │   ├── opt_common.h          # 从源码拷贝
│   │   ├── fopen_utf8.h          # 从源码拷贝
│   │   ├── config.h              # 需./configure编译后，从源码拷贝
│   │   └── config_components.h   # 需./configure编译后，从源码拷贝
│   │
│   └── SDL2/
│       └── *.h
│
├── lib/                              # 库文件目录
│   ├── ffmpeg/
│   │   ├── avcodec.lib           # MSVC 使用
│   │   ├── libavcodec.dll.a      # MinGW 使用
│   │   └── ...（其他库文件）
│   └── SDL2/
│       └── x64/
│           └── SDL2.lib
│
├── ffmpeg-6.1.1-source/              # FFmpeg 源码（用于提取文件）
│
├── ffplay.c                          # FFplay 主程序（保留在根目录）
├── cmdutils.c                        # 从源码拷贝
├── opt_common.c					  # 从源码拷贝
└── qt_ffplay.pro                     # Qt 项目文件
```
