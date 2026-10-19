#include "src/render/Texture/局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

namespace engine
{
	//取共享采样器（按当前采样器状态创建；首次创建后复用）
	bool Texture_Context::sampler_create(VkSampler& sampler_out)
	{
		//若已存在则直接复用（状态未变时无需重建；状态切换路径会先归零再调本函数）
		if (sampler_out != VK_NULL_HANDLE)
			return true;

		//默认无有效句柄
		sampler_out = VK_NULL_HANDLE;
		//前置：逻辑设备必须齐备
		if (device == VK_NULL_HANDLE)
		{
			logger.error("纹理创建:: 创建采样器时逻辑设备为空");
			return false;
		}

		/*
		寻址模式映射（引擎语义 → Vulkan 枚举）
		*/
		VkSamplerAddressMode address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		if (sampler_state.address_mode == Texture_Address_Mode::Clamp_To_Edge)
			address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		//镜像重复
		else if (sampler_state.address_mode == Texture_Address_Mode::Mirrored_Repeat)
			address_mode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;

		/*
		填充采样器创建信息
		过滤一律线性；mip 过滤由状态决定：
		启用 → mipmapMode 线性且 maxLod 放开（VK_LOD_CLAMP_NONE），拥有 mip 链的纹理会采到低级别；
		关闭 → mipmapMode 最近邻且 maxLod = 0（等价于忽略 mip 链，行为与本版次之前一致）。
		*/
		VkSamplerCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		info.magFilter = VK_FILTER_LINEAR;
		info.minFilter = VK_FILTER_LINEAR;
		info.mipmapMode = sampler_state.mip_filter ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.addressModeU = address_mode;
		info.addressModeV = address_mode;
		info.addressModeW = address_mode;
		info.mipLodBias = 0.0f;
		info.anisotropyEnable = VK_FALSE;
		info.maxAnisotropy = 1.0f;
		info.compareEnable = VK_FALSE;
		info.compareOp = VK_COMPARE_OP_ALWAYS;
		info.minLod = 0.0f;
		info.maxLod = sampler_state.mip_filter ? VK_LOD_CLAMP_NONE : 0.0f;
		info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		info.unnormalizedCoordinates = VK_FALSE;

		//创建采样器
		if (!vk_result_check(vkCreateSampler(device, &info, nullptr, &sampler_out), "纹理创建:: 创建采样器"))
		{
			sampler_out = VK_NULL_HANDLE;
			return false;
		}
		return true;
	}

	/*
	设置共享采样器状态
	状态变化时销毁并重建共享采样器，并把全部有效纹理记录改指新句柄；
	已写入的描述符集不随之更新，故调用方须在切换后重新绑定纹理（见 纹理.h 接口注释）。
	*/
	void Texture_Context::sampler_state_set(const Texture_Sampler_State& state)
	{
		//前置：逻辑设备必须齐备
		if (device == VK_NULL_HANDLE)
		{
			logger.error("纹理创建:: 设置采样器状态时逻辑设备为空");
			return;
		}
		//同状态重复设置为空操作
		if (sampler_state.address_mode == state.address_mode && sampler_state.mip_filter == state.mip_filter)
			return;

		//记录新状态
		sampler_state = state;

		//归还旧共享采样器（参数已变，无法复用）
		if (shared_sampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, shared_sampler, nullptr);
			shared_sampler = VK_NULL_HANDLE;
		}
		//按新状态重建
		if (!sampler_create(shared_sampler))
		{
			logger.error("纹理创建:: 采样器状态切换失败（共享采样器已不可用）");
			return;
		}
		//逐条纹理改指新采样器（旧句柄已销毁）
		for (auto& record : records)
		{
			//仅改写有效记录
			if (record.valid)
				record.sampler = shared_sampler;
		}
		logger.info("纹理创建:: 采样器状态已切换（寻址 {}，mip 过滤 {}）",
			static_cast<int32_t>(state.address_mode), state.mip_filter);
	}
}
