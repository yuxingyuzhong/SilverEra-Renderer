#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"

/*
管线公共语义类型（Vulkan-free）
本头与 Mesh 模块的「网格数据.h」同构：只放渲染层公共面需要暴露的引擎语义类型，
不含任何 Vulkan / VMA / SDL3 / glm 类型；由门面公共头（渲染器.h）与管线模块共同引用，
避免公共头为了一个枚举而引入层内实现细节头（管线.h 含 Vulkan 类型）。
*/
namespace engine
{
	/*
	混合档
	决定图形管线颜色附件的混合状态；由门面（blend_mode_set）设置、管线模块消费。
	切档会触发一次图形管线重建（等价于「格式变化」路径）。
	*/
	enum class Blend_Mode
	{
		//不透明：忽略源 alpha，直接写全通道（blendEnable = VK_FALSE）
		Opaque,
		//源 alpha 混合：src.rgb * src.a + dst.rgb * (1 - src.a)，alpha 通道同源同参
		Alpha,
	};
}
