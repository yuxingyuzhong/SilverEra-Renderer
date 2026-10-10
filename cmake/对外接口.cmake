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
#     SDL3 / VMA / Vulkan 的包含路径都是本层「实现细节」，不出现在对外面上：
#     本层公共头不暴露 SDL、Vulkan、VMA 的任何类型（源文件里以私有包含根引入）。
#
# 链接与系统库为什么比 EngineCore 面多
#     RendererCore 内含 SDL3、VMA 与本层渲染代码的符号，最终链接方（Test 层的
#     EngineTests.exe）必须一并带上：
#       - Lib/vulkan-1.lib        本层直接调用 Vulkan 入口点
#       - setupapi/winmm/imm32/version   SDL3 在 Windows 上的额外系统库，
#                                      口径取自官方 VisualC/SDL/SDL.vcxproj 的 AdditionalDependencies
#       - ole32/oleaut32/advapi32/uuid  SDL3 的 COM / 注册表 / GUID 依赖
# =============================================================================

include("${CMAKE_CURRENT_LIST_DIR}/../../../Engine/EngineCore/cmake/对外接口.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/VulkanSDK探测.cmake")

get_filename_component(BYJY_RENDERER_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(BYJY_RENDERER_OUT_LIB "RendererCore")
set(BYJY_RENDERER_OUT_LIBDIR "${BYJY_RENDERER_ROOT}")

set(BYJY_RENDERER_OUT_INC
    ${BYJY_ENGINE_CORE_OUT_INC}
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
