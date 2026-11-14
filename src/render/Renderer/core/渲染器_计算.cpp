//实现体定义（持有各模块实例）
#include "渲染器实现.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"

//游戏引擎命名空间
namespace engine
{
	//构造（只创建域状态，不接触 GPU 资源）
	Render_Compute::Render_Compute(Renderer* owner)
		: owner(owner),
		state_ptr(new(std::nothrow) Compute_State())
	{
	}

	//析构（资源由门面关闭流程统一回收）
	Render_Compute::~Render_Compute(void)
	{
	}

	//计算域
	Compute_State& Render_Compute::state(void)
	{
		return *state_ptr;
	}

	//计算域（只读）
	const Compute_State& Render_Compute::state(void) const
	{
		return *state_ptr;
	}

	/*
	初始化
	原 renderer_init_graphics 中「⑫.2 计算管线」部分：能力探测 / 上下文构建 / 健康检查管线登记；
	失败时沿用原语义直接关闭门面。
	*/
	bool Render_Compute::init(const Renderer_Config& effective)
	{
		//限制：实现体未就绪（半初始化）
		if (owner == nullptr || owner->impl == nullptr)
			return false;
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		/*
		GPU 能力与降级矩阵探测（第二百四十六轮）
		与 compute_enable 无关：即使强制关闭计算路径，也如实记录设备能力，便于上层决定是否开启。
		*/
		Compute_Capabilities& capabilities = state().compute_capabilities;
		capabilities = Compute_Capabilities{};
		VkPhysicalDevice physical = impl->device.physical_device_get();
		uint32_t probe_family = impl->device.graphics_family_get();
		//队列族属性：COMPUTE 位与时间戳有效位
		uint32_t family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, nullptr);
		if (family_count > probe_family)
		{
			std::vector<VkQueueFamilyProperties> families(family_count);
			vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families.data());
			capabilities.compute = (families[probe_family].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
			capabilities.timestamp = families[probe_family].timestampValidBits > 0;
		}
		//特性查询：多批次间接绘制（核心特性）与 Vulkan 1.2 两项
		VkPhysicalDeviceVulkan12Features features12{};
		features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		VkPhysicalDeviceFeatures2 features{};
		features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features.pNext = &features12;
		vkGetPhysicalDeviceFeatures2(physical, &features);
		capabilities.multi_draw_indirect = features.features.multiDrawIndirect == VK_TRUE;
		capabilities.draw_indirect_count = features12.drawIndirectCount == VK_TRUE;
		capabilities.host_query_reset = features12.hostQueryReset == VK_TRUE;
		//间接命令以 firstInstance 承担实例偏移，故需该核心特性（第二百五十二轮）
		impl->indirect_first_instance_supported = features.features.drawIndirectFirstInstance == VK_TRUE;
		/*
		Hi-Z 能力探测（第二百五十三轮）
		要求计算路径可用且 R32 浮点格式支持存储图像（金字塔逐级写入）与采样（遮挡查询读取）；
		不满足时 hiz_supported 为假，开启请求被拒绝且不创建任何资源。
		*/
		VkFormatProperties hiz_format_properties{};
		vkGetPhysicalDeviceFormatProperties(physical, HIZ_PYRAMID_FORMAT, &hiz_format_properties);
		const VkFormatFeatureFlags hiz_features = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
		state().hiz_capable = capabilities.compute
			&& (hiz_format_properties.optimalTilingFeatures & hiz_features) == hiz_features;
		//强制关闭计算路径时跳过构建（全部 GPU 驱动路径保持关闭，默认帧路径不受影响）
		if (!effective.compute_enable)
		{
			state().compute_health_handle = 0;
			state().compute_health_module = VK_NULL_HANDLE;
		}
		else
		{
			Compute_Config compute_config;
			compute_config.frame_overlap = effective.frame_overlap;
			//构建计算上下文（布局 / 池 / 描述符集 / 一次性提交资源）
			if (!state().compute.build(impl->device.physical_device_get(), impl->device.device_get(),
				impl->device.graphics_family_get(), impl->device.graphics_queue_get(), compute_config))
			{
				logger.error("渲染器:: 计算上下文构建失败");
				owner->shutdown();
				return false;
			}
			//设备支持计算路径时才加载自检着色器并登记管线
			if (state().compute.supported())
			{
				VkShaderModule compute_module = VK_NULL_HANDLE;
				if (impl->shader.module_load(compute_module, "计算写入.comp.spv"))
				{
					uint64_t compute_handle = 0;
					//登记健康检查管线
					if (state().compute.pipeline_register(compute_handle, compute_module))
					{
						state().compute_health_module = compute_module;
						state().compute_health_handle = compute_handle;
					}
					else
					{
						logger.warn("渲染器:: 计算健康检查管线登记失败");
						impl->shader.module_destroy(compute_module);
					}
				}
				else
					logger.warn("渲染器:: 计算写入着色器加载失败（计算路径保持未启用）");
			}
		}
		//时间戳能力与实际计时链路同口径（队列族有效位 ＋ 设备周期，二者与关闭开关无关）
		VkPhysicalDeviceProperties device_properties{};
		vkGetPhysicalDeviceProperties(physical, &device_properties);
		capabilities.timestamp = capabilities.timestamp && device_properties.limits.timestampPeriod > 0.0f;
		return true;
	}

	/*
	关闭
	原 shutdown 中计算相关部分：先释放缓冲与管线登记，再销毁 Hi-Z 资源、计算上下文与健康检查模块；
	各步骤幂等，可安全处理半初始化状态。
	*/
	void Render_Compute::shutdown(void)
	{
		//限制：实现体未就绪（半初始化或已关闭）
		if (owner == nullptr || owner->impl == nullptr)
			return;
		//实现体（未迁成员经此访问）
		Renderer_Impl_Holder::Impl* impl = owner->impl.get();
		//先释放门面层计算资源（缓冲与管线模块），再销毁计算上下文（第二百四十三轮）
		for (Compute_Buffer_Record& record : state().compute_buffers)
			//归还有效缓冲与其内存
			if (record.buffer != VK_NULL_HANDLE)
				impl->memory.buffer_destroy(record.buffer, record.allocation);
		state().compute_buffers.clear();
		for (Compute_Pipeline_Record& record : state().compute_pipelines)
		{
			//释放计算上下文内部管线
			state().compute.pipeline_release(record.context_handle);
			//回收该管线的着色器模块
			if (record.module != VK_NULL_HANDLE)
				impl->shader.module_destroy(record.module);
		}
		state().compute_pipelines.clear();
		state().gpu_cull_pipeline = 0;
		//实例压缩管线句柄同样失效（第二百四十九轮）
		state().gpu_compact_pipeline = 0;
		state().last_gpu_cull_ms = 0.0;
		//Hi-Z 资源依赖内存模块与计算上下文，须先于两者销毁（第二百五十三 / 二百五十四轮）
		state().hiz.compute_set(nullptr);
		state().hiz.destroy();
		state().hiz_enabled = false;
		state().hiz_depth_copy_pipeline = 0;
		state().hiz_downsample_pipeline = 0;
		//计算管线同样依赖设备与内存，先于两者销毁（第二百四十一轮）
		state().compute.destroy();
		//计算着色器模块须先于着色器上下文复位
		if (state().compute_health_module != VK_NULL_HANDLE)
		{
			impl->shader.module_destroy(state().compute_health_module);
			state().compute_health_module = VK_NULL_HANDLE;
		}
		state().compute_health_handle = 0;
	}
}
