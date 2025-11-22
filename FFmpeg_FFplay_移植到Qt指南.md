# FFmpeg 6.1.1 FFplay 移植到 Qt 工程指南

本文档详细说明如何将 FFmpeg 6.1.1 的 ffplay.c 移植到 Qt 工程中。

## 目录

- [环境要求](#环境要求)
- [项目目录结构](#项目目录结构)
- [步骤一：创建 Qt 工程](#步骤一创建-qt-工程)
- [步骤二：从 FFmpeg 源码拷贝必需文件](#步骤二从-ffmpeg-源码拷贝必需文件)
- [步骤三：创建配置文件](#步骤三创建配置文件)
- [步骤四：配置 Qt 项目文件](#步骤四配置-qt-项目文件)
- [步骤五：编译和调试](#步骤五编译和调试)
- [常见问题解决](#常见问题解决)

---

## 环境要求

### 必需组件

1. **Qt 5.12 或更高版本**
   - MinGW 64-bit 或 MSVC 2017/2019 编译器
   
2. **FFmpeg 6.1.1**
   - 预编译的库文件（.lib 或 .dll.a）
   - 头文件
   - DLL 文件

3. **SDL2**
   - 开发库和头文件
   - DLL 文件

4. **FFmpeg 6.1.1 源码**
   - 用于提取 fftools 相关源文件

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
│   │   ├── libavutil/
│   │   │   └── ffversion.h       # 需./configure编译和make后，从源码拷贝。或者手动创建
│   │   ├── libpostproc/
│   │   ├── libswresample/
│   │   ├── libswscale/
│   │   │
│   │   ├── compat/               # 兼容性头文件
│   │   │   ├── va_copy.h
│   │   │   ├── w32dlfcn.h
│   │   │   └── w32pthreads.h
│   │   │
│   │   ├── cmdutils.c            # 从源码拷贝
│   │   ├── cmdutils.h            # 从源码拷贝
│   │   ├── opt_common.c          # 从源码拷贝
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
└── qt_ffplay.pro                     # Qt 项目文件
```

---

## 步骤一：创建 Qt 工程

1. 打开 Qt Creator
2. 文件 → 新建文件或项目
3. 选择 **"Application (Qt)"** → **"Qt Console Application"**
4. 设置项目名称为 `qt_ffplay`
5. 选择构建套件（MinGW 或 MSVC）

---

## 步骤二：从 FFmpeg 源码拷贝必需文件

### 2.1 核心源文件（从 `ffmpeg-6.1.1-source/fftools/` 拷贝）

在项目根目录执行以下命令（PowerShell）：

```powershell
# 创建目标目录
New-Item -ItemType Directory -Path "include\ffmpeg" -Force

# 拷贝核心源文件到 include/ffmpeg 目录
Copy-Item "ffmpeg-6.1.1-source\fftools\cmdutils.c" "include\ffmpeg\cmdutils.c" -Force
Copy-Item "ffmpeg-6.1.1-source\fftools\cmdutils.h" "include\ffmpeg\cmdutils.h" -Force
Copy-Item "ffmpeg-6.1.1-source\fftools\opt_common.c" "include\ffmpeg\opt_common.c" -Force
Copy-Item "ffmpeg-6.1.1-source\fftools\opt_common.h" "include\ffmpeg\opt_common.h" -Force
Copy-Item "ffmpeg-6.1.1-source\fftools\fopen_utf8.h" "include\ffmpeg\fopen_utf8.h" -Force

# ffplay.c 保留在根目录
Copy-Item "ffmpeg-6.1.1-source\fftools\ffplay.c" "ffplay.c" -Force
```

**文件说明：**
- `cmdutils.c/h` - 命令行工具通用函数 → `include/ffmpeg/`
- `opt_common.c/h` - 命令行选项处理 → `include/ffmpeg/`
- `fopen_utf8.h` - UTF-8 文件打开支持 → `include/ffmpeg/`
- `ffplay.c` - FFplay 主程序 → **根目录**（便于修改和调试）

### 2.2 兼容性头文件（从 `ffmpeg-6.1.1-source/compat/` 拷贝）

```powershell
# 创建 compat 目录
New-Item -ItemType Directory -Path "include\ffmpeg\compat" -Force

# 拷贝兼容性头文件到 include/ffmpeg/compat
Copy-Item "ffmpeg-6.1.1-source\compat\va_copy.h" "include\ffmpeg\compat\va_copy.h" -Force
Copy-Item "ffmpeg-6.1.1-source\compat\w32dlfcn.h" "include\ffmpeg\compat\w32dlfcn.h" -Force
Copy-Item "ffmpeg-6.1.1-source\compat\w32pthreads.h" "include\ffmpeg\compat\w32pthreads.h" -Force
```

**文件说明：**
- `va_copy.h` - va_list 相关兼容性宏
- `w32dlfcn.h` - Windows 动态链接库加载兼容
- `w32pthreads.h` - Windows 线程兼容

### 2.3 配置文件（从 `ffmpeg-6.1.1-source/` 拷贝）

⚠️ **重要提示：** `config.h` 和 `config_components.h` 是在 FFmpeg 编译时自动生成的文件。

**如果您的源码目录中已经有这些文件（已编译过的源码）：**

```powershell
# 拷贝配置文件到 include/ffmpeg 目录
Copy-Item "ffmpeg-6.1.1-source\config.h" "include\ffmpeg\config.h" -Force
Copy-Item "ffmpeg-6.1.1-source\config_components.h" "include\ffmpeg\config_components.h" -Force
```

**如果您的源码目录中没有这些文件（未编译的纯源码）：**

在源码目录下执行 configure 脚本生成这些文件：
```bash
cd ffmpeg-6.1.1-source
./configure
```

然后拷贝到项目：
```powershell
Copy-Item "ffmpeg-6.1.1-source\config.h" "include\ffmpeg\config.h" -Force
Copy-Item "ffmpeg-6.1.1-source\config_components.h" "include\ffmpeg\config_components.h" -Force
```

**文件说明：**
- `config.h` - FFmpeg 编译配置（定义了 FFmpeg 的各种编译选项和系统特性）
- `config_components.h` - FFmpeg 组件配置（定义了编译包含的编解码器、格式等）

---

## 步骤三：创建和配置文件

### 3.1 创建 `ffversion.h`（必须手动创建）

此文件在 FFmpeg 编译时自动生成，源码中不存在，需要手动创建：

**文件位置：** `include/ffmpeg/libavutil/ffversion.h`

```c
/* Automatically generated by version.sh, do not manually edit! */
#ifndef AVUTIL_FFVERSION_H
#define AVUTIL_FFVERSION_H
#define FFMPEG_VERSION "6.1.1"
#endif /* AVUTIL_FFVERSION_H */
```

### 3.2 关于 `config.h` 和 `config_components.h`

这两个文件是 FFmpeg 编译时生成的配置文件，包含大量的宏定义。在源码目录下执行configure脚本即可生成。

```c
./configure
```

---

## 步骤四：配置 Qt 项目文件

### 4.1 完整的 `qt_ffplay.pro` 配置

```qmake
TEMPLATE = app
CONFIG += c++11
CONFIG += console

SOURCES += \
    ffplay.c

# ========== 输出目录配置 ==========
# 可执行文件直接输出到源码目录的 bin/
DESTDIR = $$PWD/bin

# 检测编译器类型
mingw {
    COMPILER_NAME = mingw
}
msvc {
    COMPILER_NAME = msvc
}
gcc:!mingw {
    COMPILER_NAME = gcc
}

# 检测架构（32位/64位）
contains(QT_ARCH, x86_64) {
    ARCH_NAME = x64
} else:contains(QT_ARCH, i386) {
    ARCH_NAME = x86
} else:contains(QT_ARCH, arm64) {
    ARCH_NAME = arm64
} else {
    # 默认根据指针大小判断
    equals(QT_POINTER_SIZE, 8) {
        ARCH_NAME = x64
    } else {
        ARCH_NAME = x86
    }
}

# 构建目录：build/编译器_架构/debug或release/
BUILD_BASE_DIR = $$PWD/build/$${COMPILER_NAME}_$${ARCH_NAME}

# 临时文件输出到 build/ 目录（按编译器、架构、Debug/Release 分类）
CONFIG(debug, debug|release) {
    BUILD_DIR = $${BUILD_BASE_DIR}/debug
    OBJECTS_DIR = $${BUILD_DIR}/obj
    MOC_DIR     = $${BUILD_DIR}/moc
    RCC_DIR     = $${BUILD_DIR}/rcc
    UI_DIR      = $${BUILD_DIR}/ui
} else {
    BUILD_DIR = $${BUILD_BASE_DIR}/release
    OBJECTS_DIR = $${BUILD_DIR}/obj
    MOC_DIR     = $${BUILD_DIR}/moc
    RCC_DIR     = $${BUILD_DIR}/rcc
    UI_DIR      = $${BUILD_DIR}/ui
}

# ========== 平台配置 ==========
win32 {
    # FFmpeg 相关文件都在 include/ffmpeg 目录
    INCLUDEPATH += $$PWD/include/ffmpeg
    INCLUDEPATH += $$PWD/include/SDL2
    # ffplay.c 在根目录，需要访问 include/ffmpeg 中的文件
    INCLUDEPATH += $$PWD

    # MinGW 编译器配置
    mingw {
        # 定义Windows版本和缺失的常量
        DEFINES += _WIN32_WINNT=0x0600 WC_ERR_INVALID_CHARS=0x00000080
        
        LIBS += -L$$PWD/lib/ffmpeg \
                -L$$PWD/lib/SDL2/x64 \
                -lavformat \
                -lavcodec \
                -lavdevice \
                -lavfilter \
                -lavutil \
                -lpostproc \
                -lswresample \
                -lswscale \
                -lSDL2 \
                -lws2_32 \
                -lSecur32 \
                -lBcrypt \
                -lStrmiids
        
        # 禁用一些警告
        QMAKE_CFLAGS += -Wno-deprecated-declarations -Wno-incompatible-pointer-types
        QMAKE_CXXFLAGS += -Wno-deprecated-declarations
        
        # Debug 模式：生成调试信息，关闭优化
        CONFIG(debug, debug|release) {
            QMAKE_CFLAGS_DEBUG += -g -O0
            QMAKE_CXXFLAGS_DEBUG += -g -O0
        }
    }

    # MSVC 编译器配置
    msvc {
        LIBS += $$PWD/lib/ffmpeg/avformat.lib   \
                $$PWD/lib/ffmpeg/avcodec.lib    \
                $$PWD/lib/ffmpeg/avdevice.lib   \
                $$PWD/lib/ffmpeg/avfilter.lib   \
                $$PWD/lib/ffmpeg/avutil.lib     \
                $$PWD/lib/ffmpeg/postproc.lib   \
                $$PWD/lib/ffmpeg/swresample.lib \
                $$PWD/lib/ffmpeg/swscale.lib    \
                $$PWD/lib/SDL2/x64/SDL2.lib     \
                ws2_32.lib Secur32.lib Bcrypt.lib Strmiids.lib shell32.lib Ole32.lib
        
        # 禁用警告
        QMAKE_CFLAGS += /wd4996 /wd4133 /wd4090 /wd4244 /wd4305 /wd4018
        QMAKE_CXXFLAGS += /wd4996
        
        # Debug 模式：生成完整调试信息
        CONFIG(debug, debug|release) {
            # 完整调试信息，禁用优化
            QMAKE_CFLAGS_DEBUG += /Zi /Od
            QMAKE_CXXFLAGS_DEBUG += /Zi /Od
            
            # PDB 文件输出到 build/编译器_架构/debug 目录
            QMAKE_LFLAGS_DEBUG += /DEBUG:FULL
            QMAKE_LFLAGS_DEBUG += /PDB:$$shell_quote($$shell_path($${BUILD_DIR}/$${TARGET}.pdb))
            QMAKE_LFLAGS_DEBUG += /ILK:$$shell_quote($$shell_path($${BUILD_DIR}/$${TARGET}.ilk))
        } else {
            # Release 模式也保留调试信息（方便定位崩溃）
            QMAKE_LFLAGS_RELEASE += /DEBUG
            QMAKE_LFLAGS_RELEASE += /OPT:REF /OPT:ICF
            QMAKE_LFLAGS_RELEASE += /PDB:$$shell_quote($$shell_path($${BUILD_DIR}/$${TARGET}.pdb))
        }
    }
}
```

### 4.2 配置说明

#### 关键配置项

1. **源文件组织**
   - `ffplay.c` - 保留在根目录（便于修改）
   - 其他 FFmpeg 工具文件 - 在 `include/ffmpeg/` 目录

2. **输出目录**
   - `DESTDIR` - 可执行文件输出到 `bin/`

3. **编译器检测**
   - 自动检测 MinGW、MSVC、GCC
   - 自动检测 x86、x64、arm64 架构

4. **包含路径配置**
   - `include/ffmpeg/` - FFmpeg 工具源文件和配置
   - `include/SDL2/` - SDL2 头文件
   - 根目录 - ffplay.c 的位置

5. **MinGW 特定配置**
   - 定义 `WC_ERR_INVALID_CHARS` 解决老版本 MinGW 的兼容性问题
   - 使用 `-lavformat` 等链接 `.dll.a` 格式的库

6. **MSVC 特定配置**
   - 使用 `.lib` 格式的库
   - 添加 `shell32.lib` 和 `Ole32.lib`（必需）
   - PDB 文件输出到 build 目录

---

## 步骤五：编译和调试

### 5.1 准备 DLL 文件

将所有必需的 DLL 文件拷贝到 `bin/` 目录：

```powershell
# 拷贝 FFmpeg DLL
Copy-Item dll\*.dll bin\ -Force
```

**必需的 DLL 文件：**
- `avcodec-60.dll`
- `avdevice-60.dll`
- `avfilter-9.dll`
- `avformat-60.dll`
- `avutil-58.dll`
- `postproc-57.dll`
- `swresample-4.dll`
- `swscale-7.dll`
- `SDL2.dll`

### 5.2 编译项目

1. 在 Qt Creator 中打开项目
2. 选择构建套件（MinGW 或 MSVC）
3. 选择 Debug 或 Release 模式
4. 点击 **"构建"** → **"构建项目"**

### 5.3 配置运行参数

FFplay 必须指定视频文件才能运行：

1. 点击左侧 **"项目"** 图标
2. 点击 **"运行"** 标签
3. 在 **"命令行参数"** 框中输入视频路径：
   ```
   D:\test.mp4
   ```
   或使用网络视频测试：
   ```
   http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4
   ```

### 5.4 调试

1. 在 `ffplay.c` 的 `main` 函数中设置断点
2. 按 **F5** 或点击 **"开始调试"**
3. 程序应该在断点处停下

---

## 常见问题解决

### 问题 1：`libavutil/ffversion.h: No such file or directory`

**原因：** 该文件是编译时生成的，源码中不存在。

**解决：** 手动创建 `include/ffmpeg/libavutil/ffversion.h`，内容见 [步骤三](#步骤三创建配置文件)。

---

### 问题 2：`WC_ERR_INVALID_CHARS` 未定义（MinGW）

**原因：** MinGW 的 Windows 头文件版本较老。

**解决：** 在 `.pro` 文件中添加定义：
```qmake
DEFINES += WC_ERR_INVALID_CHARS=0x00000080
```

---

### 问题 3：`CommandLineToArgvW` 链接错误（MSVC）

**原因：** 缺少 `shell32.lib` 库。

**解决：** 在 MSVC 配置中添加：
```qmake
LIBS += shell32.lib Ole32.lib
```

---

### 问题 4：程序一启动就退出

**原因：** 没有指定视频文件参数。

**解决：** 在运行配置中添加命令行参数（见 [5.3 配置运行参数](#53-配置运行参数)）。

---

### 问题 5：断点不停止

**可能原因：**
1. 使用了 Release 模式（应该用 Debug）
2. 没有配置命令行参数，程序直接退出
3. 编译器优化过度

**解决：**
1. 确保选择 Debug 模式
2. 配置命令行参数
3. 检查 `.pro` 文件中的调试配置

---

### 问题 6：找不到 DLL 文件

**原因：** DLL 不在可执行文件目录或系统 PATH 中。

**解决：** 将所有 DLL 拷贝到 `bin/` 目录。

---

### 问题 7：源码目录中找不到 `config.h` 或 `config_components.h`

**原因：** 这两个文件是 FFmpeg 编译时生成的，未编译的纯源码中不存在。

**解决方案：**

**方案1：** 从已编译的 FFmpeg 源码获取
- 如果有编译过的 FFmpeg 源码，从那里拷贝这两个文件

**方案2：** 在源码目录下执行 configure 脚本生成
```bash
cd ffmpeg-6.1.1-source
./configure
```
然后拷贝到项目：
```powershell
Copy-Item "ffmpeg-6.1.1-source\config.h" "include\ffmpeg\config.h" -Force
Copy-Item "ffmpeg-6.1.1-source\config_components.h" "include\ffmpeg\config_components.h" -Force
```

---

## 文件清单

### 必须从源码拷贝的文件（共 11 个）

| 序号 | 源文件路径 | 目标路径 | 说明 | 备注 |
|-----|-----------|---------|------|------|
| 1 | `fftools/cmdutils.c` | `include/ffmpeg/cmdutils.c` | 命令行工具核心 | ✅ 源码文件 |
| 2 | `fftools/cmdutils.h` | `include/ffmpeg/cmdutils.h` | 命令行工具头文件 | ✅ 源码文件 |
| 3 | `fftools/opt_common.c` | `include/ffmpeg/opt_common.c` | 选项处理实现 | ✅ 源码文件 |
| 4 | `fftools/opt_common.h` | `include/ffmpeg/opt_common.h` | 选项处理头文件 | ✅ 源码文件 |
| 5 | `fftools/ffplay.c` | `ffplay.c` | FFplay 主程序 | ✅ 源码文件（**根目录**） |
| 6 | `fftools/fopen_utf8.h` | `include/ffmpeg/fopen_utf8.h` | UTF-8 文件支持 | ✅ 源码文件 |
| 7 | `compat/va_copy.h` | `include/ffmpeg/compat/va_copy.h` | va_list 兼容 | ✅ 源码文件 |
| 8 | `compat/w32dlfcn.h` | `include/ffmpeg/compat/w32dlfcn.h` | Windows DLL 兼容 | ✅ 源码文件 |
| 9 | `compat/w32pthreads.h` | `include/ffmpeg/compat/w32pthreads.h` | Windows 线程兼容 | ✅ 源码文件 |
| 10 | `config.h` | `include/ffmpeg/config.h` | FFmpeg 编译配置 | ⚠️ 编译生成 |
| 11 | `config_components.h` | `include/ffmpeg/config_components.h` | FFmpeg 组件配置 | ⚠️ 编译生成 |

### 需要手动创建或生成的文件（3 个）

| 序号 | 文件路径 | 说明 | 备注 |
|-----|---------|------|------|
| 1 | `include/ffmpeg/libavutil/ffversion.h` | 版本信息头文件 | ⚠️ 编译生成，需手动创建 |
| 2 | `include/ffmpeg/config.h`（如果源码中没有） | FFmpeg 编译配置 | ⚠️ 运行 configure 生成 |
| 3 | `include/ffmpeg/config_components.h`（如果源码中没有） | 组件配置 | ⚠️ 运行 configure 生成 |

**图例说明：**
- ✅ **源码文件** - 可直接从未编译的源码中获取
- ⚠️ **编译生成** - FFmpeg 编译时自动生成，需从已编译的源码获取或手动创建

---

## 编译器选择建议

### MinGW（推荐）

**优点：**
- FFmpeg 官方对 GCC/MinGW 支持更好
- 开源免费
- 编译错误通常更容易解决

**缺点：**
- 某些 Windows API 支持较老

### MSVC

**优点：**
- 更好的 Windows API 支持
- 调试体验更好
- 生成的代码可能更优化

**缺点：**
- 需要更多的库文件配置
- 某些 C99/C11 特性支持不如 GCC

**推荐：** 优先使用 **MinGW 64-bit**，如果遇到问题再尝试 MSVC。

---

## 总结

本指南涵盖了将 FFmpeg 6.1.1 的 ffplay.c 移植到 Qt 工程的完整流程：

### 文件需求

1. ✅ **从源码拷贝 9 个文件**
   - 6 个 fftools 核心文件
   - 3 个 compat 兼容性头文件

2. ⚠️ **处理 3 个编译生成的文件**
   - `ffversion.h` - 必须手动创建
   - `config.h` - 从已编译源码获取或使用最小配置
   - `config_components.h` - 从已编译源码获取或使用最小配置

### 项目配置

3. ✅ 配置 Qt 项目文件
   - 支持 MinGW 和 MSVC 双编译器
   - 自动检测架构（x86/x64）
   - 清晰的目录结构（bin/build 分离）

4. ✅ 完整的调试支持
   - Debug 模式完整调试信息
   - PDB 文件分离到 build 目录

### 故障排查

5. ✅ 常见问题解决方案
   - 7 个常见问题及详细解决步骤
   - 编译、链接、运行问题全覆盖

按照本指南操作，您应该能够成功编译并运行 FFplay，并且可以在 Qt Creator 中进行调试和开发。

### 关键提示

- 📌 `config.h` 和 `config_components.h` 是编译生成文件，需特别注意获取途径
- 📌 推荐使用 **MinGW 64-bit** 编译器
- 📌 必须配置命令行参数才能运行 ffplay
- 📌 所有 DLL 文件必须在 `bin/` 目录中

---

**文档版本：** 1.0  
**最后更新：** 2025-11-22  
**适用于：** FFmpeg 6.1.1 + Qt 5.12+

