# =============================================================================
# Renderer —— 构建脚本
# =============================================================================
# 位置：白银纪元/Engine/EngineRenderer
# 依赖：EngineFoundation（引擎核心层）与 EngineSystem（系统层）
#       —— 一律链接「已经构建好的」静态库；依赖方向 Foundation → System → Renderer
# 产物：EngineRenderer 静态库（本层自有源码 ＋ 就地编入的 SDL3 与 VulkanMemoryAllocator）
# 用法（本层独立配置与构建；Debug / Release 用两棵独立构建树）：
#     Debug  ：cmake -S <本目录> -B <本目录>/out/build/x64-Debug   -G Ninja -DCMAKE_BUILD_TYPE=Debug
#     Release：cmake -S <本目录> -B <本目录>/out/build/x64-Release -G Ninja -DCMAKE_BUILD_TYPE=Release
#     随后  ：cmake --build <对应构建树目录>
# 前置条件：先按顺序构建 EngineFoundation、EngineSystem 的**对应构建类型**静态库（见第 1 / 1.4 节的双树映射）。
# 包含路径：项目内部一律写「项目根相对全路径」——
#           本层头   Engine/EngineRenderer/...
#           引擎头   Engine/EngineFoundation/...、Engine/EngineSystem/...
#           编译包含根 = [下层头快照根, ..., 项目根]（快照在前，见第 1.5 节，次序不可颠倒）
# 层间契约：
#     cmake/接入下层.cmake    通用接入函数 byjy_jieru_xiaceng()，逐字节与其他四层相同
#     cmake/对外接口.cmake    本层对外自描述（前缀 BYJY_RENDERER，已并入 EngineSystem 对外面）
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

project(EngineRenderer LANGUAGES C CXX)

# =============================================================================
# 归档响应文件 BOM 包装器（MSVC + Ninja 专用）
# =============================================================================
# 背景：Ninja 写出的归档响应文件（*.rsp）是 UTF-8 无 BOM，MSVC 的 lib.exe 在没有
#       BOM 时按系统 ANSI 代码页解读其内容，本层自有源码的中文对象名（如
#       VMA实现.cpp.obj）会被解成乱码，归档阶段报 LNK1181。
# 做法：把归档命令换成「先给响应文件补 BOM，再调用真实归档器」的包装脚本。
# 说明：脚本与 EngineFoundation 共用同一份（构建工具，非层间契约），避免两份副本漂移；
#       CMAKE_CXX_CREATE_STATIC_LIBRARY 必须在 project() 之后设置，否则会被
#       Platform/Windows-MSVC.cmake 的默认值覆盖回去；只对 Ninja 生成器生效，
#       不影响 Visual Studio 生成器（其归档由 MSBuild 的 Lib 任务完成）。
# =============================================================================
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    # 此处尚未执行下方的 get_filename_component，故用 CMAKE_CURRENT_SOURCE_DIR 定位
    # 本层位于 Engine/EngineRenderer：项目根在本目录上溯两级
    set(BYJY_AR_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/EngineFoundation/cmake/ar_rsp_bom.ps1")
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

# 本层根目录；项目根 = Engine/EngineRenderer 上溯两级（项目内部包含路径的基准）
get_filename_component(PROJECT_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
get_filename_component(BYJY_PROJECT_ROOT "${PROJECT_ROOT_DIR}/../.." ABSOLUTE)

# =============================================================================
# 0. 构建类型（Debug / Release 双构建树）
# =============================================================================
# 与 EngineFoundation / EngineSystem 同一套约定：CMAKE_BUILD_TYPE 决定本层产出的
# EngineRenderer.lib 是 Debug(/MDd) 还是 Release(/MD)，构建树目录也按类型分开。
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
# 1. 接入下层 —— 版本解析（版本锁）＋ 链接已构建好的静态库
# =============================================================================
# 本层只消费下层「已经构建好的」静态库：下层源码不进入本次构建，也没有源码回退路径。
#
# 为什么只接 EngineFoundation 与 EngineSystem 两层、不接 Engine 聚合面（Game / Test 接的是聚合面）：
#   渲染层需要的下层能力只有两类 —— EngineFoundation 的通用基础件，以及 EngineSystem 的
#   场景中立数据（关卡数据 / 场景清单 / 相机光照雾纯数据类型与文本解析）。
#   接聚合面等于把「谁都不需要的东西」也挂成硬依赖，放大耦合。此处按层取最窄依赖。
#
# 版本解析（cmake/版本工具.cmake，两层同口径）：
#   ① 覆盖变量 BYJY_ENGINE_<层>_LIB_PATH 非空 → 用它（手工接另一棵树的库）
#   ② 否则读 Engine/版本锁.cmake：
#        锁空   → 跟随最新：<层>/out/build/x64-<当前配置>
#        锁非空 → 锁定版本：Engine/版本缓存/<层>/x64-<配置>/<版本哈希>
#   ③ 锁指定但缓存缺失 → FATAL_ERROR
#
# 说明：原先「下层依赖/ 冻结副本」的机制已由版本锁取代 ——
#   锁空即跟随最新（等价于不冻结），锁非空即停在旧版（等价于冻结，但按版本寻址、
#   可多版本并存，且头与库天然同版次，不会出现「冻结快照缺锚点」的校验空洞）。
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/接入下层.cmake")
include("${BYJY_PROJECT_ROOT}/Engine/EngineFoundation/cmake/版本工具.cmake")

set(BYJY_ENGINE_FOUNDATION_LIB_PATH "" CACHE FILEPATH
    "EngineFoundation 静态库路径；留空则按版本锁解析（锁空 = 跟随最新构建树）")
set(BYJY_ENGINE_SYSTEM_LIB_PATH "" CACHE FILEPATH
    "EngineSystem 静态库路径；留空则按版本锁解析（锁空 = 跟随最新构建树）")

# 双配置解析（两层）：Debug / Release 各算一份。多配置生成器（VS）下由
# IMPORTED_LOCATION_<CONFIG> 与 $<CONFIG> 包含根分别选取；单配置生成器（Ninja）只用当前配置那份。
if("${BYJY_ENGINE_FOUNDATION_LIB_PATH}" STREQUAL "")
    byjy_banben_jiexi_shuang(EngineFoundation engine_foundation
        "${BYJY_PROJECT_ROOT}/Engine/EngineFoundation"
        BYJY_ENGINE_FOUNDATION_LIB BYJY_ENGINE_FOUNDATION_SNAPSHOT BYJY_ENGINE_FOUNDATION_VERSION)
    if("${CMAKE_BUILD_TYPE}" STREQUAL "Release"
       OR "${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo"
       OR "${CMAKE_BUILD_TYPE}" STREQUAL "MinSizeRel")
        set(BYJY_ENGINE_FOUNDATION_LIB_PATH "${BYJY_ENGINE_FOUNDATION_LIB_Release}")
    else()
        set(BYJY_ENGINE_FOUNDATION_LIB_PATH "${BYJY_ENGINE_FOUNDATION_LIB_Debug}")
    endif()
else()
    set(BYJY_ENGINE_FOUNDATION_LIB_Debug "${BYJY_ENGINE_FOUNDATION_LIB_PATH}")
    set(BYJY_ENGINE_FOUNDATION_LIB_Release "${BYJY_ENGINE_FOUNDATION_LIB_PATH}")
endif()

if("${BYJY_ENGINE_SYSTEM_LIB_PATH}" STREQUAL "")
    byjy_banben_jiexi_shuang(EngineSystem engine_system
        "${BYJY_PROJECT_ROOT}/Engine/EngineSystem"
        BYJY_ENGINE_SYSTEM_LIB BYJY_ENGINE_SYSTEM_SNAPSHOT BYJY_ENGINE_SYSTEM_VERSION)
    if("${CMAKE_BUILD_TYPE}" STREQUAL "Release"
       OR "${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo"
       OR "${CMAKE_BUILD_TYPE}" STREQUAL "MinSizeRel")
        set(BYJY_ENGINE_SYSTEM_LIB_PATH "${BYJY_ENGINE_SYSTEM_LIB_Release}")
    else()
        set(BYJY_ENGINE_SYSTEM_LIB_PATH "${BYJY_ENGINE_SYSTEM_LIB_Debug}")
    endif()
else()
    set(BYJY_ENGINE_SYSTEM_LIB_Debug "${BYJY_ENGINE_SYSTEM_LIB_PATH}")
    set(BYJY_ENGINE_SYSTEM_LIB_Release "${BYJY_ENGINE_SYSTEM_LIB_PATH}")
endif()

# 给导入函数提供分配置库路径：锚点头按 $<CONFIG> 分别注入，必须先于导入设置。
set(BYJY_ENGINE_FOUNDATION_LIB_PATH_DEBUG   "${BYJY_ENGINE_FOUNDATION_LIB_Debug}")
set(BYJY_ENGINE_FOUNDATION_LIB_PATH_RELEASE "${BYJY_ENGINE_FOUNDATION_LIB_Release}")
set(BYJY_ENGINE_SYSTEM_LIB_PATH_DEBUG   "${BYJY_ENGINE_SYSTEM_LIB_Debug}")
set(BYJY_ENGINE_SYSTEM_LIB_PATH_RELEASE "${BYJY_ENGINE_SYSTEM_LIB_Release}")
# 分配置头快照根：VS 构建树的布局与 Ninja 不同，锚点不能靠库路径反推
set(BYJY_ENGINE_FOUNDATION_LIB_PATH_INCLUDE_DEBUG   "${BYJY_ENGINE_FOUNDATION_SNAPSHOT_Debug}")
set(BYJY_ENGINE_FOUNDATION_LIB_PATH_INCLUDE_RELEASE "${BYJY_ENGINE_FOUNDATION_SNAPSHOT_Release}")
set(BYJY_ENGINE_SYSTEM_LIB_PATH_INCLUDE_DEBUG   "${BYJY_ENGINE_SYSTEM_SNAPSHOT_Debug}")
set(BYJY_ENGINE_SYSTEM_LIB_PATH_INCLUDE_RELEASE "${BYJY_ENGINE_SYSTEM_SNAPSHOT_Release}")

byjy_jieru_xiaceng(
    "${BYJY_PROJECT_ROOT}/Engine/EngineFoundation/cmake/对外接口.cmake"
    "BYJY_ENGINE_FOUNDATION"
    "BYJY_ENGINE_FOUNDATION_LIB_PATH"
)

# 多配置：逐配置设置导入库位置（单配置生成器忽略这些属性）
foreach(_byjy_cfg IN ITEMS DEBUG RELEASE)
    set(_byjy_cfg_lib "${BYJY_ENGINE_FOUNDATION_LIB_${_byjy_cfg}}")
    if(NOT "${_byjy_cfg_lib}" STREQUAL "" AND EXISTS "${_byjy_cfg_lib}")
        set_target_properties(EngineFoundation PROPERTIES
            IMPORTED_LOCATION_${_byjy_cfg} "${_byjy_cfg_lib}")
    endif()
endforeach()
if(EXISTS "${BYJY_ENGINE_FOUNDATION_LIB_Release}")
    set_target_properties(EngineFoundation PROPERTIES
        IMPORTED_LOCATION_RELWITHDEBINFO "${BYJY_ENGINE_FOUNDATION_LIB_Release}"
        IMPORTED_LOCATION_MINSIZEREL "${BYJY_ENGINE_FOUNDATION_LIB_Release}")
endif()

# 手工覆盖 / 仅解析出单配置时：快照根由导入目标反推，并补齐成双配置同一份
if(NOT DEFINED BYJY_ENGINE_FOUNDATION_SNAPSHOT_Debug OR "${BYJY_ENGINE_FOUNDATION_SNAPSHOT_Debug}" STREQUAL "")
    byjy_tou_kuaizhao_gen(BYJY_ENGINE_FOUNDATION_SNAPSHOT_Debug EngineFoundation)
endif()
if(NOT DEFINED BYJY_ENGINE_FOUNDATION_SNAPSHOT_Release OR "${BYJY_ENGINE_FOUNDATION_SNAPSHOT_Release}" STREQUAL "")
    set(BYJY_ENGINE_FOUNDATION_SNAPSHOT_Release "${BYJY_ENGINE_FOUNDATION_SNAPSHOT_Debug}")
endif()
set(BYJY_ENGINE_FOUNDATION_SNAPSHOT
    "$<IF:$<CONFIG:Debug>,${BYJY_ENGINE_FOUNDATION_SNAPSHOT_Debug},${BYJY_ENGINE_FOUNDATION_SNAPSHOT_Release}>")

# =============================================================================
# 1.4 解析 EngineSystem
# =============================================================================
# 场景装配 / 粒子宿主 / 资产缓存归本层后，依赖方向为
#   EngineFoundation → EngineSystem → EngineRenderer → 应用层
# 系统层只产出中立数据契约（关卡数据 / 场景清单 / 相机光照雾纯数据类型与文本解析），
# 本层据此创建资源并绘制；系统层不引用本层任何头或符号。
#
byjy_jieru_xiaceng(
    "${BYJY_PROJECT_ROOT}/Engine/EngineSystem/cmake/对外接口.cmake"
    "BYJY_ENGINE_SYSTEM"
    "BYJY_ENGINE_SYSTEM_LIB_PATH"
)

# 多配置：逐配置设置导入库位置（单配置生成器忽略这些属性）
foreach(_byjy_cfg IN ITEMS DEBUG RELEASE)
    set(_byjy_cfg_lib "${BYJY_ENGINE_SYSTEM_LIB_${_byjy_cfg}}")
    if(NOT "${_byjy_cfg_lib}" STREQUAL "" AND EXISTS "${_byjy_cfg_lib}")
        set_target_properties(EngineSystem PROPERTIES
            IMPORTED_LOCATION_${_byjy_cfg} "${_byjy_cfg_lib}")
    endif()
endforeach()
if(EXISTS "${BYJY_ENGINE_SYSTEM_LIB_Release}")
    set_target_properties(EngineSystem PROPERTIES
        IMPORTED_LOCATION_RELWITHDEBINFO "${BYJY_ENGINE_SYSTEM_LIB_Release}"
        IMPORTED_LOCATION_MINSIZEREL "${BYJY_ENGINE_SYSTEM_LIB_Release}")
endif()

# 手工覆盖 / 仅解析出单配置时：快照根由导入目标反推，并补齐成双配置同一份
if(NOT DEFINED BYJY_ENGINE_SYSTEM_SNAPSHOT_Debug OR "${BYJY_ENGINE_SYSTEM_SNAPSHOT_Debug}" STREQUAL "")
    byjy_tou_kuaizhao_gen(BYJY_ENGINE_SYSTEM_SNAPSHOT_Debug EngineSystem)
endif()
if(NOT DEFINED BYJY_ENGINE_SYSTEM_SNAPSHOT_Release OR "${BYJY_ENGINE_SYSTEM_SNAPSHOT_Release}" STREQUAL "")
    set(BYJY_ENGINE_SYSTEM_SNAPSHOT_Release "${BYJY_ENGINE_SYSTEM_SNAPSHOT_Debug}")
endif()
set(BYJY_ENGINE_SYSTEM_SNAPSHOT
    "$<IF:$<CONFIG:Debug>,${BYJY_ENGINE_SYSTEM_SNAPSHOT_Debug},${BYJY_ENGINE_SYSTEM_SNAPSHOT_Release}>")

# 全链一致性（逐配置）：System 构建时用的 Foundation 版本必须与本次解析一致
foreach(_byjy_cfg IN ITEMS Debug Release)
    byjy_banben_hedui("EngineRenderer" EngineSystem "${_byjy_cfg}")
endforeach()

# =============================================================================
# 1.5 下层头快照接入
# =============================================================================
# 本层按「与下层静态库同版次」的头文件编译：把两层快照根作为「本层自身的包含目录」列出，
# 并排在项目根之前 —— 目标自身的 -I 恒在导入目标传递来的 /external:I 之前，
# 因此 Engine/... 必然先命中快照，不会落到源码树上的同名头。
# 快照根已在第 1 / 1.4 节由版本解析给出（锁空 = 构建树快照，锁非空 = 版本缓存快照）。
# 目录缺失即硬失败（不静默退回源码树）。
# =============================================================================
# 双配置逐份校验：目录缺失即硬失败（不静默退回源码树）；同时做头完整性兜底。
# 对应配置的下层构建树 / 缓存条目不存在时跳过该配置（单配置构建通常只建一棵树）。
foreach(_byjy_cfg IN ITEMS Debug Release)
    foreach(_byjy_pair IN ITEMS "EngineFoundation|BYJY_ENGINE_FOUNDATION_SNAPSHOT" "EngineSystem|BYJY_ENGINE_SYSTEM_SNAPSHOT")
        string(REPLACE "|" ";" _byjy_parts "${_byjy_pair}")
        list(GET _byjy_parts 0 _byjy_layer)
        list(GET _byjy_parts 1 _byjy_base)
        set(_byjy_snap "${${_byjy_base}_${_byjy_cfg}}")
        if(NOT IS_DIRECTORY "${_byjy_snap}")
            if(CMAKE_CONFIGURATION_TYPES)
                message(FATAL_ERROR
                    "Renderer：${_byjy_layer} 的 ${_byjy_cfg} 头快照目录不存在：${_byjy_snap}\n"
                    "  多配置生成器要求两套快照齐备；请先构建 ${_byjy_layer} 的 ${_byjy_cfg} 树。")
            endif()
            continue()
        endif()
        byjy_jiaoyan_xiacengtou("EngineRenderer(${_byjy_cfg})" "${_byjy_snap}" "${_byjy_layer}"
            "${PROJECT_ROOT_DIR}/src" "${PROJECT_ROOT_DIR}/common")
    endforeach()
endforeach()

# 本层各目标的包含根：下层快照在前、项目根在后（供 Engine/EngineRenderer/... 全路径）。
set(BYJY_RENDERER_INC
    "${BYJY_ENGINE_FOUNDATION_SNAPSHOT}"
    "${BYJY_ENGINE_SYSTEM_SNAPSHOT}"
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
#       脚本与 EngineFoundation 共用同一份（构建工具，非层间契约），避免多份副本漂移。
include("${BYJY_PROJECT_ROOT}/Engine/EngineFoundation/cmake/生成源文件列表.cmake")
byjy_source_list_sync(
    "${PROJECT_ROOT_DIR}/源文件列表.cmake"
    "${PROJECT_ROOT_DIR}"
    EngineRenderer
    RENDERER_SOURCES
    RENDERER_HEADERS
    "Engine/EngineRenderer"
)

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
add_library(EngineRenderer STATIC ${SDL3_SOURCES})
include("${PROJECT_ROOT_DIR}/源文件列表.cmake")

# =============================================================================
# 3.2 版次锚点（头 / 库同版次由链接期符号强制）
# =============================================================================
# 与 EngineFoundation 同一套机制：对外头集合 + 构建类型 + 运行库口径的哈希编进符号名，
# 生成 版本锚点.h/.cpp；cpp 编入本库，h 随本层头快照发布。
# 上层（Test / Game）编译时由导入目标强制包含该头，头库错配即 LNK2019。
# 说明：哈希目录与本层头快照的镜像范围严格一致（见第 3.5 节 MIRROR_DIRS）。
# =============================================================================
set(BYJY_RENDERER_ANCHOR_REL "Engine/EngineRenderer/版本锚点.h")
set(BYJY_RENDERER_ANCHOR_DIR "${CMAKE_BINARY_DIR}/生成")
set(BYJY_RENDERER_ANCHOR_H "${BYJY_RENDERER_ANCHOR_DIR}/版本锚点.h")
set(BYJY_RENDERER_ANCHOR_CPP "${BYJY_RENDERER_ANCHOR_DIR}/版本锚点.cpp")

add_custom_command(
    OUTPUT "${BYJY_RENDERER_ANCHOR_H}" "${BYJY_RENDERER_ANCHOR_CPP}"
    COMMAND "${CMAKE_COMMAND}"
        "-DSRC_ROOT=${BYJY_PROJECT_ROOT}"
        "-DMIRROR_DIRS=Engine/EngineRenderer/common;Engine/EngineRenderer/src"
        "-DBUILD_TYPE=$<CONFIG>"
        "-DOUT_H=${BYJY_RENDERER_ANCHOR_H}"
        "-DOUT_CPP=${BYJY_RENDERER_ANCHOR_CPP}"
        "-DLAYER_TOKEN=renderer"
        -P "${BYJY_PROJECT_ROOT}/Engine/EngineFoundation/cmake/生成版本锚点.cmake"
    DEPENDS ${RENDERER_HEADERS} "${BYJY_PROJECT_ROOT}/Engine/EngineFoundation/cmake/生成版本锚点.cmake"
    COMMENT "生成 Renderer 版次锚点"
    VERBATIM
)
set_source_files_properties("${BYJY_RENDERER_ANCHOR_CPP}" PROPERTIES GENERATED TRUE)
target_sources(EngineRenderer PRIVATE "${BYJY_RENDERER_ANCHOR_CPP}")

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
target_include_directories(EngineRenderer
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
target_include_directories(EngineRenderer PRIVATE "${PROJECT_ROOT_DIR}")

if(BYJY_RENDERER_OUT_DEF)
    target_compile_definitions(EngineRenderer PUBLIC ${BYJY_RENDERER_OUT_DEF})
endif()

# 链接下层静态库：包含路径 / 编译定义 / 第三方库由导入目标传递过来
# 顺序即依赖顺序：Renderer 消费 System，System 消费 Foundation。
target_link_libraries(EngineRenderer PUBLIC
    EngineFoundation
    EngineSystem
    ${BYJY_RENDERER_OUT_LINK}
    ${BYJY_RENDERER_OUT_SYS}
)

# 编译选项与下层保持一致
if(MSVC)
    target_compile_options(EngineRenderer PRIVATE /MP /utf-8)
    target_compile_definitions(EngineRenderer PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(EngineRenderer PRIVATE -Wall -Wextra -pedantic)
    target_compile_definitions(EngineRenderer PRIVATE _GNU_SOURCE)
endif()

# glm 适配宏（设计 §2.5 第 4 点）：本层私有，绝不进公共头，否则污染上层
#   GLM_FORCE_DEPTH_ZERO_TO_ONE —— Vulkan 深度范围 [0,1]（K1）
#   GLM_FORCE_RADIANS           —— 角度一律用弧度
# 注：glm 的包含根是 external/（代码写 #include <glm/glm.hpp>），已在上面 PRIVATE 里
target_compile_definitions(EngineRenderer PRIVATE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
)

# 静态库输出到构建目录 lib/（与下层一致；显式给每配置指定，避免 VS 生成器再追加 Debug/Release 子目录，
# 使上层 byjy_jieru_xiaceng() 的 out/build/x64-<树>/lib/<库名>.lib 探测口径保持有效）
set_target_properties(EngineRenderer PROPERTIES
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
# 路径形态：镜像目录相对「项目根」保留完整层级 —— 快照根/Engine/EngineRenderer/...
# 脚本复用 EngineFoundation 的同名通用脚本（纯路径操作，与本层无耦合）。
# =============================================================================
set(BYJY_RENDERER_INCLUDE_SNAPSHOT_DIR "${CMAKE_BINARY_DIR}/include")
# 多配置生成器（VS）：一棵树装两个配置，快照必须按配置分目录，否则两配置互相覆盖
if(CMAKE_CONFIGURATION_TYPES)
    set(BYJY_RENDERER_INCLUDE_SNAPSHOT_DIR "${CMAKE_BINARY_DIR}/include/$<CONFIG>")
endif()

add_custom_target(RendererIncludeSnapshot ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DSRC_ROOT=${BYJY_PROJECT_ROOT}"
        "-DDST_ROOT=${BYJY_RENDERER_INCLUDE_SNAPSHOT_DIR}"
        "-DMIRROR_DIRS=Engine/EngineRenderer/common;Engine/EngineRenderer/src"
        "-DANCHOR_SRC=${BYJY_RENDERER_ANCHOR_H}"
        "-DANCHOR_REL=${BYJY_RENDERER_ANCHOR_REL}"
        -P "${BYJY_PROJECT_ROOT}/Engine/EngineFoundation/cmake/导出头快照.cmake"
    COMMENT "导出 Renderer 对外头快照 → ${BYJY_RENDERER_INCLUDE_SNAPSHOT_DIR}"
    VERBATIM
)

# 快照必须在本层库构建成功之后导出，避免「新头 + 旧库」的中间状态。
add_dependencies(RendererIncludeSnapshot EngineRenderer)

# =============================================================================
# 3.6 版本缓存发布（版本锁机制的供方）
# =============================================================================
# 把本层刚构建好的对外头快照与 EngineRenderer.lib 按版本哈希存入共享缓存，
# 供上层（Test / Game）「锁定到本版本」时取用。依赖版本.cmake 一并拷入，
# 让上层能做全链一致性校验（例如 Renderer 构建时用的是哪个版本的 System）。
# =============================================================================
byjy_banben_jilu("${CMAKE_BINARY_DIR}")

add_custom_target(RendererVersionPublish ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DSRC_BUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DSNAPSHOT_ROOT=${BYJY_RENDERER_INCLUDE_SNAPSHOT_DIR}"
        "-DLIB_FILE=$<TARGET_FILE:EngineRenderer>"
        "-DLAYER_NAME=EngineRenderer"
        "-DLAYER_TOKEN=renderer"
        "-DCONFIG_TREE=x64-$<CONFIG>"
        "-DCACHE_ROOT=${BYJY_BANBEN_CACHE_ROOT}"
        "-DPROJECT_ROOT=${BYJY_PROJECT_ROOT}"
        "-DEXTRA_DIR=shaders"
        "-DKEEP=8"
        -P "${BYJY_PROJECT_ROOT}/Engine/EngineFoundation/cmake/发布版本缓存.cmake"
    COMMENT "发布 EngineRenderer 版本缓存"
    VERBATIM
)
add_dependencies(RendererVersionPublish RendererIncludeSnapshot)

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
    add_dependencies(EngineRenderer RendererShaders)
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
