#include "../局部命名空间使用.h"
//获取结果检查出口（含日志系统）
#include "Application/Renderer/src/render/Renderer/core/结果检查.h"

namespace engine
{
	//取共享采样器（线性过滤 / REPEAT 寻址 / mipmap 关闭；首次创建后复用）
	bool Texture_Context::sampler_create(VkSampler& sampler_out)
	{
		//若已存在则直接复用（参数固定，无需重建）
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

		//填充采样器创建信息（过滤线性、mipmap 最近、寻址重复、各向异性关闭、无 mip）
		VkSamplerCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		info.magFilter = VK_FILTER_LINEAR;
		info.minFilter = VK_FILTER_LINEAR;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.mipLodBias = 0.0f;
		info.anisotropyEnable = VK_FALSE;
		info.maxAnisotropy = 1.0f;
		info.compareEnable = VK_FALSE;
		info.compareOp = VK_COMPARE_OP_ALWAYS;
		info.minLod = 0.0f;
		info.maxLod = 0.0f;
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
}
