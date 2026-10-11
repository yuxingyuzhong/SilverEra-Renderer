// =============================================================================
// VMA 实现单元
// =============================================================================
// 位置：Application/Renderer/src/render/Memory/core/VMA实现.cpp
//
// 作用
//     VulkanMemoryAllocator 是「头文件即实现」形态：全部函数体都写在
//     include/vk_mem_alloc.h 内，由 VMA_IMPLEMENTATION 宏开启展开。
//     全项目必须有且只有一个编译单元定义该宏，否则会多重定义；此处即那唯一一处。
//
// 说明
//     本单元不写任何代码，只负责「就近」展开实现 —— 将来换用 volk 之类的加载器时，
//     VMA_STATIC_VULKAN_FUNCTIONS / VMA_DYNAMIC_VULKAN_FUNCTIONS 只需改这里。
//     vk_mem_alloc.h 需要的 Vulkan 头（<vulkan/vulkan.h>）包含路径由
//     CMakeLists.txt 从 Vulkan SDK 探测结果里私有引入。
// =============================================================================

//开启 VMA 实现展开
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
