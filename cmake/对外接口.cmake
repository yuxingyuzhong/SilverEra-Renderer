# =============================================================================
# Renderer —— 对外接口（对外自描述）
# -----------------------------------------------------------------------------
# 位置：Application/Renderer/cmake/对外接口.cmake
#
# 作用
#     声明「渲染层向上层提供什么」。上层通过本文件读取本层对外面，据此建立
#     IMPORTED 静态库目标（不使用 add_subdirectory 回退编源码）。
#
# 契约（变量名与含义是层间契约的一部分，改名必须同步上层）
#     BYJY_<P>_OUT_LIB    本层静态库名（多个用分号分隔）；不产出库时留空
#     BYJY_<P>_OUT_LIBDIR 与 OUT_LIB 一一对应的「该库所在层目录」
#     BYJY_<P>_OUT_INC    使用本层公共头文件所需的包含目录
#     BYJY_<P>_OUT_DEF    使用本层公共头文件所需的编译定义
#     BYJY_<P>_OUT_LINK   本层对外传递的第三方库（文件路径）
#     BYJY_<P>_OUT_SYS    本层对外传递的系统库
#     本层前缀 <P> = RENDERER
#
# 并入下层 —— 直接并入 EngineCore，而不是 Engine 聚合面
#     本层对外面 = 本层自有面 + EngineCore 对外面。并入方式就是下面第一句 include：
#     它把 BYJY_ENGINE_CORE_* 赋值进来，本层再在自己的列表里直接引用。
#     纯数据赋值，重复 include 无副作用。
#
#     为什么走 EngineCore 单层、不走 Engine 聚合面（Game / Test 走的是聚合面）：
#     渲染层与实体 / 属性 / 效应（EngineSystem）没有任何关系，公共头里也不会出现
#     EngineSystem 的类型。若接 Engine 聚合面，就会白白把 EngineSystem.lib 拖成
#     渲染层的链接依赖，既放大了耦合面，也让「EngineSystem 未跟进 EngineCore 重构」
#     这类下层问题直接堵死渲染层的构建。直接接 EngineCore 是最窄的依赖，且完全可逆：
#     以后渲染层真要用到实体 / 属性 / 效应，把 include 目标换回 Engine 聚合面、
#     同步 CMakeLists.txt 第 1 节的库映射即可。
#
# 包含目录
#     本层自有包含由「项目根」统一承担（项目内部一律写项目根相对全路径：
#     Application/Renderer/...），它已在 EngineCore 对外面的首项里，故此处只承接即可。
#     另向本层公共扩展头与「接入 ImGui Vulkan 后端」放开 Vulkan SDK 与 SDL3 公共头
#     （Vulkan SDK / SDL3/include / SDL3/include/build_config）：
#       - 公共主头 渲染器.h 仍保持零 Vulkan / SDL 类型（Vulkan-free）；
#       - 公共扩展头 渲染器_UI通道.h 含 Vulkan / SDL 类型，上层（Test 层）要 include 它、
#         以及编译 imgui_impl_vulkan.cpp / imgui_impl_sdl3.cpp，必须先拿到这些头。
#     这是设计文档 §10.1 预留的路线①（「把 Vulkan SDK 包含根提升为 PUBLIC」）的落地。
#
# 私有包含根（对外面的「补集」，本层源码注释中「见 cmake/对外接口.cmake 的私有包含根」即指本节）
#     下面这些目录只对「本层自身编译」可见，由 Application/Renderer/CMakeLists.txt 的
#     target_include_directories(... PRIVATE ...) 段声明，不进 BYJY_RENDERER_OUT_INC，
#     因此层外拿不到它们：
#       SDL3/src                       SDL3 内部头（仅本层个别实现用到）
#       external/VulkanMemoryAllocator/include   VMA 头（vk_mem_alloc.h）
#       external/                     第三方顶层（含 glm）
#     声明落点见 Application/Renderer/CMakeLists.txt 的 PRIVATE 段。
#
# 链接与系统库为什么比 EngineCore 面多
#     RendererCore 内含 SDL3、VMA 与本层渲染代码的符号，最终链接方（Test 层的
#     EngineTests.exe）必须一并带上：
#       - Lib/vulkan-1.lib        本层直接调用 Vulkan 入口点
#       - setupapi/winmm/imm32/version   SDL3 在 Windows 上的额外系统库，
#                                      口径取自官方 VisualC/SDL/SDL.vcxproj 的 AdditionalDependencies
#       - ole32/oleaut32/advapi32/uuid  SDL3 的 COM / 注册表 / GUID 依赖
# =============================================================================

include("${CMAKE_CURRENT_LIST_DIR}/../../../../../../Engine/EngineCore/cmake/对外接口.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/VulkanSDK探测.cmake")

get_filename_component(BYJY_RENDERER_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(BYJY_RENDERER_OUT_LIB "RendererCore")
set(BYJY_RENDERER_OUT_LIBDIR "${BYJY_RENDERER_ROOT}")

set(BYJY_RENDERER_OUT_INC
    ${BYJY_ENGINE_CORE_OUT_INC}
    # 公共扩展头与上层接入 ImGui Vulkan 后端所需的包含根（见上方「包含目录」节说明）
    "${BYJY_VULKAN_INCLUDE_DIR}"
    "${BYJY_RENDERER_ROOT}/external/SDL3/include"
    "${BYJY_RENDERER_ROOT}/external/SDL3/include/build_config"
    # VMA 头（vk_mem_alloc.h）：公共扩展头 渲染器_UI通道.h 经 帧上下文.h 传递到 交换链.h，
    # 上层 include 该扩展头时需能解析到 vk_mem_alloc.h，故并入对外面
    "${BYJY_RENDERER_ROOT}/external/VulkanMemoryAllocator/include"
)

set(BYJY_RENDERER_OUT_DEF  ${BYJY_ENGINE_CORE_OUT_DEF})

set(BYJY_RENDERER_OUT_LINK
    ${BYJY_ENGINE_CORE_OUT_LINK}
    "${BYJY_VULKAN_LIB}"
)

set(BYJY_RENDERER_OUT_SYS
    ${BYJY_ENGINE_CORE_OUT_SYS}
    setupapi
    winmm
    imm32
    version
    ole32
    oleaut32
    advapi32
    uuid
)
