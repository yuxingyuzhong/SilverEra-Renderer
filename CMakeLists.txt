# =============================================================================
# Renderer —— 构建脚本
# =============================================================================
# 位置：白银纪元/Application/Renderer
# 依赖：EngineCore（引擎核心层）—— 一律链接「已经构建好的」静态库
# 产物：RendererCore 静态库（本层自有源码 ＋ 就地编入的 SDL3 与 VulkanMemoryAllocator）
# 用法（本层独立配置与构建；Debug / Release 用两棵独立构建树）：
#     Debug  ：cmake -S <本目录> -B <本目录>/out/build/x64-Debug   -G Ninja -DCMAKE_BUILD_TYPE=Debug
#     Release：cmake -S <本目录> -B <本目录>/out/build/x64-Release -G Ninja -DCMAKE_BUILD_TYPE=Release
#     随后  ：cmake --build <对应构建树目录>
# 前置条件：先构建 EngineCore 的**对应构建类型**静态库（见第 1 节的双树映射）。
# 包含路径：项目内部一律写「项目根相对全路径」——
#           本层头   Application/Renderer/...
#           引擎头   Engine/EngineCore/...
#           编译包含根 = [下层头快照根, ..., 项目根]（快照在前，见第 3.5 节，次序不可颠倒）
# 层间契约：
#     cmake/接入下层.cmake    通用接入函数 byjy_jieru_xiaceng()，逐字节与其他四层相同
#     cmake/对外接口.cmake    本层对外自描述（前缀 BYJY_RENDERER，已并入 EngineCore 对外面）
#     cmake/VulkanSDK探测.cmake  Vulkan SDK 定位（与对外接口共用同一口径）
# 说明：
#     - 本层不产出可执行文件。可执行文件只在 Test 层产生（EngineTests.exe）；
#       渲染层的行为验证自后续轮次起在 Test 层加用例。
#     - SDL3 / VMA 不使用其自带构建系统，直接编入本层静态库（无独立的 SDL3.lib / VMA.lib）。
#     - SDL3 源码经「就地裁剪」，编译集合取自官方 VisualC/SDL/SDL.vcxproj 的
#       ClCompile / ResourceCompile 条目，清单落在 cmake/SDL3源文件列表.cmake。
#       不能对 SDL3/src 用 GLOB：该目录下有几个 .c 是被 #include 引入的实现体
#       （见 cmake/SDL3源文件列表.cmake 的说明），GLOB 会把它们重复编成独立 TU。
# =============================================================================
cmake_minimum_required(VERSION 3.20)

if(MSVC)
    add_compile_options(/WX-)
endif()

project(Renderer LANGUAGES C CXX)

# =============================================================================
# 归档响应文件 BOM 包装器（MSVC + Ninja 专用）
# =============================================================================
# 背景：Ninja 写出的归档响应文件（*.rsp）是 UTF-8 无 BOM，MSVC 的 lib.exe 在没有
#       BOM 时按系统 ANSI 代码页解读其内容，本层自有源码的中文对象名（如
#       VMA实现.cpp.obj）会被解成乱码，归档阶段报 LNK1181。
# 做法：把归档命令换成「先给响应文件补 BOM，再调用真实归档器」的包装脚本。
# 说明：脚本与 EngineCore 共用同一份（构建工具，非层间契约），避免两份副本漂移；
#       CMAKE_CXX_CREATE_STATIC_LIBRARY 必须在 project() 之后设置，否则会被
#       Platform/Windows-MSVC.cmake 的默认值覆盖回去；只对 Ninja 生成器生效，
#       不影响 Visual Studio 生成器（其归档由 MSBuild 的 Lib 任务完成）。
# =============================================================================
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    # 此处尚未执行下方的 get_filename_component，故用 CMAKE_CURRENT_SOURCE_DIR 定位
    set(BYJY_AR_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/EngineCore/cmake/ar_rsp_bom.ps1")
    find_program(BYJY_POWERSHELL_EXECUTABLE NAMES powershell powershell.exe)

    # 包装脚本与 PowerShell 都就位时才改写归档命令
    if(EXISTS "${BYJY_AR_WRAPPER}" AND BYJY_POWERSHELL_EXECUTABLE)
        set(CMAKE_CXX_CREATE_STATIC_LIBRARY
            "${BYJY_POWERSHELL_EXECUTABLE} -NoProfile -ExecutionPolicy Bypass -File ${BYJY_AR_WRAPPER} <CMAKE_AR> /nologo <LINK_FLAGS> /out:<TARGET> <OBJECTS> ")
        message(STATUS "归档响应文件 BOM 包装器已启用：${BYJY_AR_WRAPPER}")
    else()
        message(WARNING "未找到归档包装器或 PowerShell，中文对象名的归档可能报 LNK1181：${BYJY_AR_WRAPPER}")
    endif()
endif()

# 强制使用 C++20 标准（与下层保持一致）
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 本层根目录；项目根 = Engine/EngineSystem/src/core/Renderer 上溯五级（项目内部包含路径的基准）
get_filename_component(PROJECT_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
get_filename_component(BYJY_PROJECT_ROOT "${PROJECT_ROOT_DIR}/../../../../.." ABSOLUTE)

# =============================================================================
# 0. 构建类型（Debug / Release 双构建树）
# =============================================================================
# 与 EngineCore / EngineSystem 同一套约定：CMAKE_BUILD_TYPE 决定本层产出的
# RendererCore.lib 是 Debug(/MDd) 还是 Release(/MD)，构建树目录也按类型分开。
# 省略 -DCMAKE_BUILD_TYPE 时按 Debug 处理；非法取值直接报错，避免静默生成意外的库。
if(NOT CMAKE_CONFIGURATION_TYPES)
    if("${CMAKE_BUILD_TYPE}" STREQUAL "")
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型（Debug / Release）" FORCE)
        message(STATUS "未指定 CMAKE_BUILD_TYPE，按 Debug 处理（构建树：${CMAKE_BINARY_DIR}）")
    endif()

    set(BYJY_VALID_BUILD_TYPES Debug Release RelWithDebInfo MinSizeRel)
    if(NOT CMAKE_BUILD_TYPE IN_LIST BYJY_VALID_BUILD_TYPES)
        message(FATAL_ERROR
            "Renderer：不支持的 CMAKE_BUILD_TYPE = \"${CMAKE_BUILD_TYPE}\"\n"
            "  合法取值：${BYJY_VALID_BUILD_TYPES}\n"
            "  双构建树示例：\n"
            "      cmake -S \"${CMAKE_CURRENT_SOURCE_DIR}\" -B \"${CMAKE_CURRENT_SOURCE_DIR}/out/build/x64-Debug\"   -G Ninja -DCMAKE_BUILD_TYPE=Debug\n"
            "      cmake -S \"${CMAKE_CURRENT_SOURCE_DIR}\" -B \"${CMAKE_CURRENT_SOURCE_DIR}/out/build/x64-Release\" -G Ninja -DCMAKE_BUILD_TYPE=Release")
    endif()

    message(STATUS "构建类型：${CMAKE_BUILD_TYPE}（构建树：${CMAKE_BINARY_DIR}）")
endif()

# =============================================================================
# 0.5 Vulkan SDK 定位（编译本层需要）
# =============================================================================
# 探测口径收口在 cmake/VulkanSDK探测.cmake：优先环境变量 VULKAN_SDK，
# 未设则回退 C:/VulkanSDK/<版本最大者>；都没有则 FATAL_ERROR（不猜路径、不静默降级）。
# 同一份探测也被 cmake/对外接口.cmake 使用，保证「本层编译」与「上层链接」看到同一个 SDK。
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/VulkanSDK探测.cmake")

# =============================================================================
# 1. 接入 EngineCore —— 链接已构建好的静态库（按本层构建类型同型映射）
# =============================================================================
# 本层只消费下层「已经构建好的」静态库：下层源码不进入本次构建，也没有源码回退路径。
#
# 为什么接 EngineCore 单层、不接 Engine 聚合面（Game / Test 接的是聚合面）：
#   渲染层与实体 / 属性 / 效应（EngineSystem）无关，公共头也不暴露其类型。接聚合面
#   只会把 EngineSystem.lib 变成渲染层的链接依赖，放大耦合，并让「EngineSystem 未跟上
#   EngineCore 重构」这类下层问题直接堵死渲染层构建。此处取最窄依赖，且完全可逆：
#   以后需要 EngineSystem 时，把下面两处换回 Engine 聚合面并同步库列表即可。
#
# 库定位顺序由 cmake/接入下层.cmake 的 byjy_jieru_xiaceng() 统一实现：
#   ① 覆盖变量非空 → 用它；文件不存在则直接报错
#   ② 留空 → 先看 <库所在层目录>/out/build/x64-Debug/lib/<库名>.lib；
#            没有则扫 <库所在层目录>/out/build/*/lib/<库名>.lib，取时间戳最新的一份
#   ③ 都没有 → FATAL_ERROR，并提示先构建产出该库的下层
#
# 但「② 固定优先 Debug」对双构建树是不合适的：本层的 Release 树若拿到 Debug 版下层库，
# 就会出现 /MD 的库配 /MDd 的下层库（运行库不一致 → LNK4098，严重时堆损坏）。
# 因此这里**不由下层函数猜**，而是在本层按「当前构建配置」显式映射（不改共享的
# byjy_jieru_xiaceng()，映射逻辑只属于本层）：
#     Debug                                  → 冻结快照 下层依赖/EngineCore/x64-Debug
#     Release（含 RelWithDebInfo/MinSizeRel） → 冻结快照 下层依赖/EngineCore/x64-Release
# 快照缺失时回退到 Engine/EngineCore/out/build/x64-<Debug|Release>。
# 仍可用 -DBYJY_ENGINE_CORE_LIB_PATH=... 手工覆盖（例如临时接另一棵树的库）。
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/接入下层.cmake")

set(BYJY_ENGINE_CORE_LIB_PATH "" CACHE FILEPATH
    "EngineCore 静态库路径；留空则按构建配置自动取冻结快照（x64-Debug / x64-Release）")

# 双配置快照根与库：Debug / Release 各算一份，随后由「导入目标的分配置属性」与
# $<CONFIG> 表达式按当前配置选取。
#
# 为什么不能只看 CMAKE_BUILD_TYPE：
#   Visual Studio 是多配置生成器，配置期 CMAKE_BUILD_TYPE 为空，且同一棵构建树里
#   Debug / Release 共用一份生成的工程；旧口径把空值当 Release，导致 Debug 构建
#   实际编译 / 链接 Release 版 EngineCore（/MD 库混入 /MDd 目标，运行库不一致）。
set(BYJY_ENGINE_CORE_AUTO TRUE)
set(BYJY_ENGINE_CORE_LIB_DEBUG "")
set(BYJY_ENGINE_CORE_LIB_RELEASE "")
set(BYJY_ENGINE_CORE_SNAPSHOT_DEBUG "")
set(BYJY_ENGINE_CORE_SNAPSHOT_RELEASE "")

if("${BYJY_ENGINE_CORE_LIB_PATH}" STREQUAL "")
    foreach(_byjy_cfg IN ITEMS DEBUG RELEASE)
        # 配置名 → 下层构建树目录名
        if("${_byjy_cfg}" STREQUAL "DEBUG")
            set(_byjy_tree "x64-Debug")
        else()
            set(_byjy_tree "x64-Release")
        endif()

        # 优先使用本层内部的「冻结快照」（第八十六轮）
        # 快照由工作站 刷新渲染层下层快照.ps1 生成：下层依赖/EngineCore/x64-<tree>/{include,lib}；
        # 存在即锁定使用，使引擎核心的并行开发（源码 / 构建树变动）不影响渲染层构建。
        set(_byjy_snap_tree "${CMAKE_CURRENT_SOURCE_DIR}/下层依赖/EngineCore/${_byjy_tree}")
        set(_byjy_snap_lib "${_byjy_snap_tree}/lib/EngineCore.lib")
        set(_byjy_snap_inc "${_byjy_snap_tree}/include")

        if(EXISTS "${_byjy_snap_lib}" AND IS_DIRECTORY "${_byjy_snap_inc}")
            set(BYJY_ENGINE_CORE_LIB_${_byjy_cfg} "${_byjy_snap_lib}")
            set(BYJY_ENGINE_CORE_SNAPSHOT_${_byjy_cfg} "${_byjy_snap_inc}")
            message(STATUS "Renderer：${_byjy_cfg} 使用冻结下层快照 → ${_byjy_snap_tree}")
        else()
            # 快照缺失 → 回退该配置的下层构建树（库与头仍同配置），并提示先刷新快照
            set(_byjy_build_tree "${BYJY_PROJECT_ROOT}/Engine/EngineCore/out/build/${_byjy_tree}")
            set(BYJY_ENGINE_CORE_LIB_${_byjy_cfg} "${_byjy_build_tree}/lib/EngineCore.lib")
            set(BYJY_ENGINE_CORE_SNAPSHOT_${_byjy_cfg} "${_byjy_build_tree}/include")
            message(WARNING
                "Renderer：未找到 ${_byjy_cfg} 冻结快照 ${_byjy_snap_lib}，回退到下层构建树 ${_byjy_tree}。\n"
                "          如需冻结依赖，请先运行工作站 刷新渲染层下层快照.ps1 后重新配置。")
        endif()
    endforeach()

    # 单配置生成器（Ninja）只认 IMPORTED_LOCATION：按其构建类型挑一份；
    # 未指定 CMAKE_BUILD_TYPE 时沿用旧口径按 Release 处理。
    if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        set(BYJY_ENGINE_CORE_LIB_PATH "${BYJY_ENGINE_CORE_LIB_DEBUG}")
    else()
        set(BYJY_ENGINE_CORE_LIB_PATH "${BYJY_ENGINE_CORE_LIB_RELEASE}")
    endif()
else()
    # 手工覆盖：两配置共用同一份库，快照根由该库所在构建树反推
    set(BYJY_ENGINE_CORE_AUTO FALSE)
    get_filename_component(_byjy_override_build "${BYJY_ENGINE_CORE_LIB_PATH}/../.." ABSOLUTE)
    set(BYJY_ENGINE_CORE_SNAPSHOT_DEBUG "${_byjy_override_build}/include")
    set(BYJY_ENGINE_CORE_SNAPSHOT_RELEASE "${_byjy_override_build}/include")
    message(WARNING "Renderer：使用手工覆盖的 EngineCore 库 → ${BYJY_ENGINE_CORE_LIB_PATH}")
endif()

byjy_jieru_xiaceng(
    "${BYJY_PROJECT_ROOT}/Engine/EngineCore/cmake/对外接口.cmake"
    "BYJY_ENGINE_CORE"
    "BYJY_ENGINE_CORE_LIB_PATH"
)

# 多配置生成器按 $<CONFIG> 取库：Debug → x64-Debug，其余（Release / RelWithDebInfo /
# MinSizeRel，均为 /MD）→ x64-Release。单配置生成器继续用上面的 IMPORTED_LOCATION。
if(BYJY_ENGINE_CORE_AUTO)
    set_target_properties(EngineCore PROPERTIES
        IMPORTED_LOCATION_DEBUG "${BYJY_ENGINE_CORE_LIB_DEBUG}"
        IMPORTED_LOCATION_RELEASE "${BYJY_ENGINE_CORE_LIB_RELEASE}"
        IMPORTED_LOCATION_RELWITHDEBINFO "${BYJY_ENGINE_CORE_LIB_RELEASE}"
        IMPORTED_LOCATION_MINSIZEREL "${BYJY_ENGINE_CORE_LIB_RELEASE}"
    )
endif()

# =============================================================================
# 1.5 下层头快照接入
# =============================================================================
# 本层按「与下层静态库同版次」的头文件编译：把 EngineCore 的头快照根作为「本层自身的
# 包含目录」列出，并排在项目根之前 —— 目标自身的 -I 恒在导入目标传递来的 /external:I
# 之前，因此 Engine/... 必然先命中快照，不会落到源码树上的同名头。
# 头快照与库必须同配置：快照根用 $<CONFIG> 选取（Debug → x64-Debug，其余 → x64-Release）。
# 目录缺失即硬失败（不静默退回源码树）。
# =============================================================================
foreach(_byjy_inc IN ITEMS
        "${BYJY_ENGINE_CORE_SNAPSHOT_DEBUG}" "${BYJY_ENGINE_CORE_SNAPSHOT_RELEASE}")
    if(NOT IS_DIRECTORY "${_byjy_inc}")
        message(FATAL_ERROR
            "Renderer：EngineCore 头快照目录不存在：${_byjy_inc}\n"
            "  该目录由下层构建时导出。请先构建 EngineCore 对应配置树：\n"
            "      cmake --build \"${BYJY_PROJECT_ROOT}/Engine/EngineCore/out/build/x64-<Debug|Release>\" --config <Debug|Release>\n"
            "  或运行工作站 刷新渲染层下层快照.ps1 后重新配置。")
    endif()
endforeach()

# 本层各目标的包含根：下层快照在前、项目根在后（供 Application/Renderer/... 全路径）。
set(BYJY_RENDERER_INC
    "$<IF:$<CONFIG:Debug>,${BYJY_ENGINE_CORE_SNAPSHOT_DEBUG},${BYJY_ENGINE_CORE_SNAPSHOT_RELEASE}>"
    "${BYJY_PROJECT_ROOT}"
)

# =============================================================================
# 2. 源文件收集
# =============================================================================
# 本层自有源码：src/（模块）与 common/（预编译头）
file(GLOB_RECURSE RENDERER_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_ROOT_DIR}/src/*.cpp"
    "${PROJECT_ROOT_DIR}/src/*.c"
)

# 递归收集本层头文件：仅用于让 IDE 与生成系统建立文件索引（不参与编译）
file(GLOB_RECURSE RENDERER_HEADERS CONFIGURE_DEPENDS
    "${PROJECT_ROOT_DIR}/src/*.h"
    "${PROJECT_ROOT_DIR}/src/*.hpp"
    "${PROJECT_ROOT_DIR}/common/*.h"
    "${PROJECT_ROOT_DIR}/common/*.hpp"
)

# 就地编入的外部库：SDL3（已按官方 Windows 清单裁剪，编译集合见下）、VMA
set(BYJY_SDL3_DIR "${PROJECT_ROOT_DIR}/external/SDL3")
set(BYJY_VMA_DIR  "${PROJECT_ROOT_DIR}/external/VulkanMemoryAllocator")

# SDL3 编译集合：显式清单，不用 GLOB（理由见 cmake/SDL3源文件列表.cmake 顶部说明）。
# 清单里是相对本层根目录的路径，此处补成绝对路径交给 add_library。
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/SDL3源文件列表.cmake")
set(SDL3_SOURCES "")
foreach(_byjy_sdl_src IN LISTS BYJY_SDL3_SOURCES)
    list(APPEND SDL3_SOURCES "${PROJECT_ROOT_DIR}/${_byjy_sdl_src}")
endforeach()

# 导出编译数据库（供 IDE / 语言工具链读取本层包含路径与宏）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 任务<1>：把被 GLOB 的目录登记为配置依赖 —— 目录内容变化（增删文件）时，
# 生成系统自动重跑配置并重扫 GLOB，不必手动「重新生成缓存」才刷新目录视图。
# SDL3 已改为显式清单（非 GLOB），故不在此登记。
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_ROOT_DIR}/src"
    "${PROJECT_ROOT_DIR}/common"
)

if(NOT RENDERER_SOURCES)
    message(FATAL_ERROR
        "未在 '${PROJECT_ROOT_DIR}/src/' 下找到任何 .cpp 或 .c 文件！\n"
        "请检查目录结构是否完整。"
    )
endif()

if(NOT SDL3_SOURCES)
    message(FATAL_ERROR
        "SDL3 编译清单为空！\n"
        "请检查 cmake/SDL3源文件列表.cmake 与 external/SDL3 是否完整。"
    )
endif()

# 排除构建目录下的文件（防止把生成的临时文件误编译进去）
string(REPLACE "\\" "/" SAFE_BINARY_DIR "${CMAKE_BINARY_DIR}")
string(REGEX REPLACE "([.+*?^$()])" "\\\\\\1" SAFE_BINARY_REGEX "${SAFE_BINARY_DIR}")
list(FILTER RENDERER_SOURCES EXCLUDE REGEX "^${SAFE_BINARY_REGEX}/")
list(FILTER RENDERER_HEADERS EXCLUDE REGEX "^${SAFE_BINARY_REGEX}/")

# 任务<2>：把本层 GLOB 扫描结果（RENDERER_SOURCES / RENDERER_HEADERS）同步进显式
# 清单 源文件列表.cmake（仅在路径集合有差异时写回），清单随后以 target_sources
# 提供全部本层源与头。
# 动机：GLOB 工程里构建脚本不含任何「具体文件名」，VS 的文件视图改名 / 迁移操作
#       找不到可改写的引用，于是既不触发 CMake 重新配置，VS 的模型与错误列表也
#       停在旧版次。清单把扫描结果显式化，为该操作提供可改写的引用。
# 范围：只覆盖本层自有源码；SDL3 编译集合是手写清单、非 GLOB，保持原机制不变。
# 说明：清单被 include，因此它本身也是配置依赖 —— 被改写即触发重新配置。
#       脚本与 EngineCore 共用同一份（构建工具，非层间契约），避免多份副本漂移。
include("${BYJY_PROJECT_ROOT}/Engine/EngineCore/cmake/生成源文件列表.cmake")
byjy_source_list_sync(
    "${PROJECT_ROOT_DIR}/源文件列表.cmake"
    "${PROJECT_ROOT_DIR}"
    RendererCore
    RENDERER_SOURCES
    RENDERER_HEADERS
    "Engine/EngineSystem/src/core/Renderer"
)

message(STATUS "Renderer 本层源文件: ${RENDERER_SOURCES}")
list(LENGTH SDL3_SOURCES SDL3_SOURCE_COUNT)
message(STATUS "Renderer 就地编入 SDL3 源文件数量: ${SDL3_SOURCE_COUNT}")

# =============================================================================
# 3. 创建渲染层静态库
# =============================================================================
# 本层的 PUBLIC 面直接取自 cmake/对外接口.cmake，让它成为唯一真源：包含目录 /
# 编译定义 / 链接库漏写或写错都会在构建期立刻暴露，不会与声明悄悄漂移。
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/对外接口.cmake")

# 本层自有源与头不在此处传参：由 源文件列表.cmake 的 target_sources 提供，
# 理由见清单头部说明（只有 target_sources 里的字面文件名，其 File API backtrace 才
# 落在清单文件上，VS 的改名 / 迁移操作才改写得到引用）。
# SDL3 编译集合是手写清单、非 GLOB，且不属「文件视图改名」的对象，仍以变量形式加入。
add_library(RendererCore STATIC ${SDL3_SOURCES})
include("${PROJECT_ROOT_DIR}/源文件列表.cmake")

# 包含根分两级：
#   PUBLIC  —— 上层使用本层公共头所需的路径（下层快照 ＋ 项目根 ＋ 对外面声明）
#   PRIVATE —— 本层实现细节：SDL3、VMA、Vulkan SDK。三者都不出现在公共头里，
#              因此不能被上层看到，否则会污染上层的包含路径与宏环境。
#
# 注意：SDL3 源码内部以相对路径 "./khronos/vulkan/vulkan.h" 引用自带的 Khronos 头，
#       故这里**不**把 external/SDL3/src/video/khronos 加进包含路径 —— 否则会遮蔽
#       Vulkan SDK 的 <vulkan/vulkan.hpp>，本层与上层都将拿到 SDL 那份精简头。
#
# 注意：include/build_config 必须进包含路径。src/SDL_internal.h 写的是
#       #include "SDL_build_config.h"，而该头位于 include/build_config/ 下
#       （源码树自带的平台派发头，按 SDL_PLATFORM_* 再派发到
#       SDL_build_config_windows.h）。SDL 官方构建也是把该目录加进包含路径，
#       并非 CMake 现场生成 —— 因此不需要（也不应）定义 USING_GENERATED_CONFIG_H。
target_include_directories(RendererCore
    PUBLIC
        ${BYJY_RENDERER_INC}
        ${BYJY_RENDERER_OUT_INC}
    PRIVATE
        "${BYJY_SDL3_DIR}/include"
        "${BYJY_SDL3_DIR}/include/build_config"
        "${BYJY_SDL3_DIR}/src"
        "${BYJY_VMA_DIR}/include"
        "${BYJY_VULKAN_INCLUDE_DIR}"
        "${PROJECT_ROOT_DIR}/external"
)
# 层内实现文件（.cpp）跨目录包含写「本层根相对」src/...；头文件仍写项目根全路径。
target_include_directories(RendererCore PRIVATE "${PROJECT_ROOT_DIR}")

if(BYJY_RENDERER_OUT_DEF)
    target_compile_definitions(RendererCore PUBLIC ${BYJY_RENDERER_OUT_DEF})
endif()

# 链接下层静态库：包含路径 / 编译定义 / 第三方库由导入目标传递过来
target_link_libraries(RendererCore PUBLIC
    EngineCore
    ${BYJY_RENDERER_OUT_LINK}
    ${BYJY_RENDERER_OUT_SYS}
)

# 编译选项与下层保持一致
if(MSVC)
    target_compile_options(RendererCore PRIVATE /MP /utf-8)
    target_compile_definitions(RendererCore PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(RendererCore PRIVATE -Wall -Wextra -pedantic)
    target_compile_definitions(RendererCore PRIVATE _GNU_SOURCE)
endif()

# glm 适配宏（设计 §2.5 第 4 点）：本层私有，绝不进公共头，否则污染上层
#   GLM_FORCE_DEPTH_ZERO_TO_ONE —— Vulkan 深度范围 [0,1]（K1）
#   GLM_FORCE_RADIANS           —— 角度一律用弧度
# 注：glm 的包含根是 external/（代码写 #include <glm/glm.hpp>），已在上面 PRIVATE 里
target_compile_definitions(RendererCore PRIVATE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
)

# 静态库输出到构建目录 lib/（与下层一致；显式给每配置指定，避免 VS 生成器再追加 Debug/Release 子目录，
# 使上层 byjy_jieru_xiaceng() 的 out/build/x64-<树>/lib/<库名>.lib 探测口径保持有效）
set_target_properties(RendererCore PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# =============================================================================
# 3.5 对外头快照导出（供上层按「与本层静态库同版次」的头编译）
# =============================================================================
# 镜像范围与本层公共头实际暴露的面一一对应：common、src。
# 第三方（external/SDL3、external/VulkanMemoryAllocator）在外对面上根本不出现，
# 不经过快照解析，故不需镜像。
# 路径形态：镜像目录相对「项目根」保留完整层级 —— 快照根/Application/Renderer/...
# 脚本复用 EngineCore 的同名通用脚本（纯路径操作，与本层无耦合）。
# =============================================================================
set(BYJY_RENDERER_INCLUDE_SNAPSHOT_DIR "${CMAKE_BINARY_DIR}/include")

add_custom_target(RendererIncludeSnapshot ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DSRC_ROOT=${BYJY_PROJECT_ROOT}"
        "-DDST_ROOT=${BYJY_RENDERER_INCLUDE_SNAPSHOT_DIR}"
        "-DMIRROR_DIRS=Engine/EngineSystem/src/core/Renderer/common;Engine/EngineSystem/src/core/Renderer/src"
        -P "${BYJY_PROJECT_ROOT}/Engine/EngineCore/cmake/导出头快照.cmake"
    COMMENT "导出 Renderer 对外头快照 → ${BYJY_RENDERER_INCLUDE_SNAPSHOT_DIR}"
    VERBATIM
)

# =============================================================================
# 4. 着色器编译（glslc 离线编译 GLSL → SPIR-V）
# =============================================================================
# 源着色器：assets/shaders/*.vert / *.frag（文件名可为中文；glslc 已实测支持 UTF-8 路径）
# 产物   ：<构建树>/shaders/*.spv —— 位于 out/ 下，随 .gitignore 天然不入库
# glslc  ：优先 <Vulkan SDK>/Bin/glslc.exe，其次 PATH；**找不到只告警**，本层静态库
#          仍可构建（与设计 §2.4「找不到 glslc 不硬失败」口径一致）
# 说明   ：本层不产出可执行文件，着色器不属库产物；此处编译意在让「构建本层」这一步
#          就能验证着色器语法，并让 Test 层按既有「已知构建树相对路径」口径取用
#          （设计 §8.2 第 4 点：Test 把本目录拷到自身输出目录）。
# =============================================================================
set(BYJY_RENDERER_SHADER_SRC_DIR "${PROJECT_ROOT_DIR}/assets/shaders")
set(BYJY_RENDERER_SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders")

# glslc 定位（留空则自动探测）
set(BYJY_GLSLC_EXECUTABLE "" CACHE FILEPATH "glslc 可执行文件路径；留空则自动探测")
if("${BYJY_GLSLC_EXECUTABLE}" STREQUAL "")
    if(EXISTS "${BYJY_VULKAN_SDK_ROOT}/Bin/glslc.exe")
        set(BYJY_GLSLC_EXECUTABLE "${BYJY_VULKAN_SDK_ROOT}/Bin/glslc.exe")
    else()
        find_program(BYJY_GLSLC_EXECUTABLE NAMES glslc glslc.exe)
    endif()
endif()

if(BYJY_GLSLC_EXECUTABLE)
    file(GLOB BYJY_SHADER_SOURCES CONFIGURE_DEPENDS
        "${BYJY_RENDERER_SHADER_SRC_DIR}/*.vert"
        "${BYJY_RENDERER_SHADER_SRC_DIR}/*.frag"
        "${BYJY_RENDERER_SHADER_SRC_DIR}/*.comp"
    )
    # 着色器目录内容变化时自动重跑配置并重扫 GLOB
    if(EXISTS "${BYJY_RENDERER_SHADER_SRC_DIR}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${BYJY_RENDERER_SHADER_SRC_DIR}")
    endif()

    # 逐源文件生成「源 → 同名 .spv」的编译命令，产物名带原扩展名（如 场景.vert.spv）
    set(BYJY_SHADER_OUTPUTS "")
    foreach(_byjy_shader IN LISTS BYJY_SHADER_SOURCES)
        get_filename_component(_byjy_shader_name "${_byjy_shader}" NAME)
        set(_byjy_shader_out "${BYJY_RENDERER_SHADER_OUT_DIR}/${_byjy_shader_name}.spv")
        add_custom_command(
            OUTPUT "${_byjy_shader_out}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${BYJY_RENDERER_SHADER_OUT_DIR}"
            COMMAND "${BYJY_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -o "${_byjy_shader_out}" "${_byjy_shader}"
            DEPENDS "${_byjy_shader}"
            COMMENT "编译着色器 ${_byjy_shader_name} → ${_byjy_shader_name}.spv"
            VERBATIM
        )
        list(APPEND BYJY_SHADER_OUTPUTS "${_byjy_shader_out}")
    endforeach()

    add_custom_target(RendererShaders ALL DEPENDS ${BYJY_SHADER_OUTPUTS})
    add_dependencies(RendererCore RendererShaders)
    message(STATUS "Renderer：glslc = ${BYJY_GLSLC_EXECUTABLE}")
    message(STATUS "Renderer：源着色器 ${BYJY_RENDERER_SHADER_SRC_DIR} → ${BYJY_RENDERER_SHADER_OUT_DIR}")
else()
    message(WARNING
        "Renderer：未找到 glslc，跳过着色器编译（本层静态库仍可构建；依赖着色器的用例将失败）。\n"
        "  请把 VULKAN_SDK 指向 Vulkan SDK 根目录，或 -DBYJY_GLSLC_EXECUTABLE=<glslc 路径>")
endif()

# =============================================================================
# 5. 可执行文件：本层不产出
# =============================================================================
# 层级契约：各层只产出静态库，唯一可执行文件由 Application/Test 产出（EngineTests.exe）。
# 渲染层的行为验证自后续轮次起在 Test 层加用例，故本层不设 add_executable。
