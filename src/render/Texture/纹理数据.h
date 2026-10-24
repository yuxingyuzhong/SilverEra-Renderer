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
		//是否启用 mip 过滤（false = 不采样 mip 链）
		bool mip_filter = false;
		/*
		细节层级范围
		min_lod 为采样允许的最小层级；max_lod 为允许的最大层级，
		取**负值**（默认 -1）表示「按 mip_filter 自动定」——
		启用 mip 过滤时放开到 VK_LOD_CLAMP_NONE，否则钳到 0（等价于忽略 mip 链，与本版次之前行为一致）。
		*/
		float min_lod = 0.0f;
		float max_lod = -1.0f;
		/*
		细节层级偏置（第七十七轮）
		叠加在硬件按屏幕导数算出的 LOD 之上：正值更模糊、负值更锐利；
		仅对拥有 mip 链的纹理在缩小绘制时有可观察效果。门面钳到 [-16, 16]，非数归 0。
		*/
		float lod_bias = 0.0f;
		/*
		各向异性过滤
		anisotropy 为真时启用；须设备在创建时启用了 samplerAnisotropy 特性，
		门面会在调用采样器前按设备能力归一（不支持则置假并告警，倍数钳到设备上限）。
		max_anisotropy 为最大各向异性倍数，小于 1 会被钳到 1。
		*/
		bool anisotropy = false;
		float max_anisotropy = 1.0f;
	};
}
