TEMPLATE = app
CONFIG += c++11
CONFIG += console

SOURCES += \
    main.c \
    ffplay.c \
    ff_queue.c \
    ff_decoder.c \
    ff_clock.c \
    cmdutils.c \
    opt_common.c

HEADERS += \
    ff_define.h \
    ff_types.h \
    ff_queue.h \
    ff_decoder.h \
    ff_clock.h \
    ffplay.h

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
    INCLUDEPATH += $$PWD/include/ffmpeg
    INCLUDEPATH += $$PWD/include/SDL2
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
        
        # 使用 C11 标准编译 C 文件 (MSVC 2019 16.8+ 支持)
        QMAKE_CFLAGS += /std:c11
        
        # 设置源文件编码为 UTF-8
        QMAKE_CFLAGS += /utf-8
        QMAKE_CXXFLAGS += /utf-8
        
        # 禁用警告
        QMAKE_CFLAGS += /wd4996 /wd4133 /wd4090 /wd4244 /wd4305 /wd4018 /wd4267 /wd4819
        QMAKE_CXXFLAGS += /wd4996 /wd4819
        
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
