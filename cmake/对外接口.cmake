# =============================================================================
# Renderer —— 对外接口（对外自描述）
# -----------------------------------------------------------------------------
# 位置：Engine/EngineRenderer/cmake/对外接口.cmake
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
# 并入下层 —— 直接并入 EngineSystem，而不是 Engine 聚合面
#     本层对外面 = 本层自有面 + EngineSystem 对外面（后者又已并入 EngineFoundation 对外面）。
#     并入方式就是下面第一句 include：它把 BYJY_ENGINE_SYSTEM_* 与 BYJY_ENGINE_FOUNDATION_*
#     赋值进来，本层再在自己的列表里直接引用。纯数据赋值，重复 include 无副作用。
#
#     依赖方向：EngineFoundation → EngineSystem → EngineRenderer → 应用层。
#     渲染层需要系统层的「场景中立数据」（关卡数据 / 场景清单 / 相机光照雾纯数据类型与
#     文本解析），故接入系统层对外面；系统层不引用渲染层任何头或符号，方向单向。
#     不接 Engine 聚合面：聚合面会把「渲染层根本不需要的东西」也挂成硬依赖。
#
# 包含目录
#     本层自有包含由「项目根」统一承担（项目内部一律写项目根相对全路径：
#     Engine/EngineRenderer/...），它已在 EngineSystem（及 EngineFoundation）对外面的首项里，
#     故此处只承接即可。
#     另向本层公共扩展头与「接入 ImGui Vulkan 后端」放开 Vulkan SDK 与 SDL3 公共头
#     （Vulkan SDK / SDL3/include / SDL3/include/build_config）：
#       - 公共主头 渲染器.h 仍保持零 Vulkan / SDL 类型（Vulkan-free）；
#       - 公共扩展头 渲染器_UI通道.h 含 Vulkan / SDL 类型，上层（Test 层）要 include 它、
#         以及编译 imgui_impl_vulkan.cpp / imgui_impl_sdl3.cpp，必须先拿到这些头。
#     这是设计文档 §10.1 预留的路线①（「把 Vulkan SDK 包含根提升为 PUBLIC」）的落地。
#
# 私有包含根（对外面的「补集」，本层源码注释中「见 cmake/对外接口.cmake 的私有包含根」即指本节）
#     下面这些目录只对「本层自身编译」可见，由 Engine/EngineRenderer/CMakeLists.txt 的
#     target_include_directories(... PRIVATE ...) 段声明，不进 BYJY_RENDERER_OUT_INC，
#     因此层外拿不到它们：
#       SDL3/src                       SDL3 内部头（仅本层个别实现用到）
#       external/VulkanMemoryAllocator/include   VMA 头（vk_mem_alloc.h）
#       external/                     第三方顶层（含 glm）
#     声明落点见 Engine/EngineRenderer/CMakeLists.txt 的 PRIVATE 段。
#
# 链接与系统库为什么比 EngineFoundation 面多
#     EngineRenderer 内含 SDL3、VMA 与本层渲染代码的符号，最终链接方（Test 层的
#     EngineTests.exe）必须一并带上：
#       - Lib/vulkan-1.lib        本层直接调用 Vulkan 入口点
#       - setupapi/winmm/imm32/version   SDL3 在 Windows 上的额外系统库，
#                                      口径取自官方 VisualC/SDL/SDL.vcxproj 的 AdditionalDependencies
#       - ole32/oleaut32/advapi32/uuid  SDL3 的 COM / 注册表 / GUID 依赖
# =============================================================================

include("${CMAKE_CURRENT_LIST_DIR}/../../EngineSystem/cmake/对外接口.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/VulkanSDK探测.cmake")

get_filename_component(BYJY_RENDERER_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(BYJY_RENDERER_OUT_LIB "EngineRenderer")
set(BYJY_RENDERER_OUT_LIBDIR "${BYJY_RENDERER_ROOT}")

set(BYJY_RENDERER_OUT_INC
    ${BYJY_ENGINE_SYSTEM_OUT_INC}
    # 公共扩展头与上层接入 ImGui Vulkan 后端所需的包含根（见上方「包含目录」节说明）
    "${BYJY_VULKAN_INCLUDE_DIR}"
    "${BYJY_RENDERER_ROOT}/external/SDL3/include"
    "${BYJY_RENDERER_ROOT}/external/SDL3/include/build_config"
    # VMA 头（vk_mem_alloc.h）：公共扩展头 渲染器_UI通道.h 经 帧上下文.h 传递到 交换链.h，
    # 上层 include 该扩展头时需能解析到 vk_mem_alloc.h，故并入对外面
    "${BYJY_RENDERER_ROOT}/external/VulkanMemoryAllocator/include"
)

set(BYJY_RENDERER_OUT_DEF  "${BYJY_ENGINE_SYSTEM_OUT_DEF}")

set(BYJY_RENDERER_OUT_LINK
    ${BYJY_ENGINE_SYSTEM_OUT_LINK}
    "${BYJY_VULKAN_LIB}"
)

# 系统库由本层统一提供：本层就地编入 SDL3，最终链接方（Test / Game）必然需要
# 下列 Win32 库；EngineFoundation 不再代持这些依赖。
#   user32 / gdi32 / shell32           窗口、绘制、Shell 基础
#   setupapi / winmm / imm32 / version  SDL3 在 Windows 上的额外依赖（口径同官方 vcxproj）
#   ole32 / oleaut32 / advapi32 / uuid SDL3 的 COM / 注册表 / GUID 依赖
set(BYJY_RENDERER_OUT_SYS
    user32
    gdi32
    shell32
    setupapi
    winmm
    imm32
    version
    ole32
    oleaut32
    advapi32
    uuid
)
