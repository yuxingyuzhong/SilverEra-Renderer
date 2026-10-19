#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"

/*
纹理公共语义类型（Vulkan-free）
本头与 Mesh 模块的「网格数据.h」、Pipeline 模块的「管线数据.h」同构：
只放渲染层公共面需要暴露的引擎语义类型，不含任何 Vulkan / VMA / SDL3 类型；
由门面公共头（渲染器.h）与纹理模块共同引用，避免公共头引入层内实现细节头（纹理.h 含 Vulkan）。
*/
namespace engine
{
	//纹理寻址模式（用于 U / V / W 三轴同值）
	enum class Texture_Address_Mode
	{
		//重复（越界后按整数周期回绕）
		Repeat,
		//边缘钳制（越界取边缘纹素）
		Clamp_To_Edge,
		//镜像重复（越界后镜像回绕）
		Mirrored_Repeat,
	};

	/*
	纹理采样器状态（模块内共享采样器的参数）
	默认值与本版次之前的行为一致：线性过滤 + 重复寻址 + 不采样 mip（maxLod = 0）。
	开启 mip 过滤后，拥有 mip 链的纹理在缩小绘制时才会真正采到低级别；
	单级纹理（未生成 mip）不受影响（lod 恒被钳到 0）。
	*/
	struct Texture_Sampler_State
	{
		//寻址模式（U / V / W 同值）
		Texture_Address_Mode address_mode = Texture_Address_Mode::Repeat;
		//是否启用 mip 过滤（false = maxLod 钳到 0，等价于忽略 mip 链）
		bool mip_filter = false;
	};
}
