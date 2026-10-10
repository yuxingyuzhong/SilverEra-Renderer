# =============================================================================
# Vulkan SDK 定位 —— 渲染层专用（探测口径单点收口）
# -----------------------------------------------------------------------------
# 位置：Application/Renderer/cmake/VulkanSDK探测.cmake
#
# 作用
#     解析出 Vulkan SDK 根目录与其头目录 / 导入库路径，供两处共用同一口径，
#     避免「构建脚本」与「对外接口」各写一套探测逻辑而悄悄漂移：
#       - Application/Renderer/CMakeLists.txt   编译本层时用
#       - Application/Renderer/cmake/对外接口.cmake  向上层传递 vulkan-1.lib 时用
#
# 探测顺序（不猜路径、不静默降级）
#     ① 环境变量 VULKAN_SDK 非空，且 <该目录>/Include/vulkan/vulkan.h 存在 → 采用
#     ② 否则枚举 C:/VulkanSDK/*，自然序倒排后取第一个「同时具备
#        Include/vulkan/vulkan.h 与 Lib/vulkan-1.lib」的目录
#     ③ 都不满足 → FATAL_ERROR，打印排查提示
#
# 产物变量
#     BYJY_VULKAN_SDK_ROOT    SDK 根目录（正斜杠形式）
#     BYJY_VULKAN_INCLUDE_DIR SDK 头目录（<根>/Include）
#     BYJY_VULKAN_LIB         SDK 导入库（<根>/Lib/vulkan-1.lib）
#
# 备注：本文件被「对外接口.cmake」间接 include，而对外接口会被多个上层读入，
#       故以产物变量是否已定义作为重复进入的短路条件，重复 include 无副作用。
# =============================================================================

if(NOT DEFINED BYJY_VULKAN_SDK_ROOT)

    # 候选 SDK 根目录
    set(_byjy_vk_root "")

    # ---------- ① 环境变量优先 ----------
    set(_byjy_vk_from_env "$ENV{VULKAN_SDK}")
    if(_byjy_vk_from_env AND EXISTS "${_byjy_vk_from_env}/Include/vulkan/vulkan.h")
        set(_byjy_vk_root "${_byjy_vk_from_env}")
        message(STATUS "Vulkan SDK：采用环境变量 VULKAN_SDK = ${_byjy_vk_root}")
    endif()

    # ---------- ② 回退探测 C:/VulkanSDK/<版本> ----------
    if(NOT _byjy_vk_root AND EXISTS "C:/VulkanSDK")
        file(GLOB _byjy_vk_candidates LIST_DIRECTORIES true "C:/VulkanSDK/*")
        # 自然序倒排：版本号最大的排在最前（避免字典序把 1.10 排到 1.4 之前）
        list(SORT _byjy_vk_candidates COMPARE NATURAL ORDER DESCENDING)

        foreach(_byjy_vk_candidate IN LISTS _byjy_vk_candidates)
            # 只认同时具备头与导入库的完整安装目录
            if(IS_DIRECTORY "${_byjy_vk_candidate}"
               AND EXISTS "${_byjy_vk_candidate}/Include/vulkan/vulkan.h"
               AND EXISTS "${_byjy_vk_candidate}/Lib/vulkan-1.lib")
                set(_byjy_vk_root "${_byjy_vk_candidate}")
                message(STATUS "Vulkan SDK：回退探测命中 ${_byjy_vk_root}")
                break()
            endif()
        endforeach()
    endif()

    # ---------- ③ 都没有：硬失败 ----------
    if(NOT _byjy_vk_root)
        message(FATAL_ERROR
            "渲染层：未找到可用的 Vulkan SDK\n"
            "  已尝试：① 环境变量 VULKAN_SDK（当前值：\"$ENV{VULKAN_SDK}\"）\n"
            "          ② 枚举 C:/VulkanSDK/* 取版本最大且含 Include/vulkan/vulkan.h ＋ Lib/vulkan-1.lib 的目录\n"
            "  请安装 Vulkan SDK，或把 VULKAN_SDK 指向 SDK 根目录后重新配置。\n"
            "  排查要点：目录内应同时存在 Include/vulkan/vulkan.h 与 Lib/vulkan-1.lib。")
    endif()

    # 统一为正斜杠形式，避免中文路径与反斜杠转义在各处反复换算
    file(TO_CMAKE_PATH "${_byjy_vk_root}" BYJY_VULKAN_SDK_ROOT)
    set(BYJY_VULKAN_INCLUDE_DIR "${BYJY_VULKAN_SDK_ROOT}/Include")
    set(BYJY_VULKAN_LIB "${BYJY_VULKAN_SDK_ROOT}/Lib/vulkan-1.lib")

endif()
