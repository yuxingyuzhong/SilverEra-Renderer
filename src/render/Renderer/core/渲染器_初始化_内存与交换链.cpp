#include "../局部命名空间使用.h"
//六域完整状态与核心模块类型
#include "渲染器内部状态.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//OBJ 网格文件解析（第四百二十二轮；资产加载模块）
#include "src/render/Asset/资产加载.h"
//系统输入桥（原始输入载荷构造）
#include "Engine/EngineRenderer/src/render/Input/系统输入桥.h"
//中转站协议（订阅者退出事件）
#include "Engine/EngineFoundation/src/event/Common/事件协议.h"
//std::nothrow
//std::isfinite（第一百七十一轮：材质 UV 变换的有限性校验）
//管线缓存文件读写（第一百零六轮）
//异步资产加载（第四百二十六轮；std::async 与轮询）
//glm 数学库（第五十二轮：光源空间视图投影矩阵）
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "渲染器内部工具.h"

namespace engine
{
	namespace
	{
		//UTF-8 字符串 → 文件系统路径（中文路径按 u8 解释；第一百零六轮）
		std::filesystem::path path_from_utf8(const std::string& text)
		{
			std::u8string u8(reinterpret_cast<const char8_t*>(text.data()), text.size());
			return std::filesystem::path(u8);
		}
	}

	//取门面自身

	//初始化（任一环节失败即逆序回退）

	//初始化阶段：VMA 分配器 / 交换链 / 在途帧数钳制
	bool renderer_init_memory_swapchain(Renderer* owner, Device_State& state, Renderer_Config& effective)
	{
		//⑥ 内存：VMA 分配器
		if (!state.memory.build(state.device.instance_get(), state.device.physical_device_get(),
			state.device.device_get(), state.device.graphics_family_get(), state.device.graphics_queue_get()))
		{
			logger.error("渲染器:: 内存初始化失败");
			owner->device().shutdown();
			return false;
		}

		//取窗口客户区实际尺寸（HiDPI 下可能与配置不同）
		uint32_t swapchain_width = 0;
		uint32_t swapchain_height = 0;
		state.platform.window_size_get(swapchain_width, swapchain_height);
		//若窗口尺寸不可用则回退配置值
		if (swapchain_width == 0 || swapchain_height == 0)
		{
			swapchain_width = effective.width;
			swapchain_height = effective.height;
		}
		//⑦ 交换链（深度图像依赖内存模块，故传入 memory；首次建链固定单采样，待格式确定后再择定 MSAA）
		state.swapchain.sample_count_set(1);
		if (!state.swapchain.build(state.device.physical_device_get(), state.memory, state.device.device_get(),
			state.platform.surface_get(), state.device.graphics_family_get(), state.device.present_family_get(),
			swapchain_width, swapchain_height, effective.vsync))
		{
			logger.error("渲染器:: 交换链创建失败");
			owner->device().shutdown();
			return false;
		}
		/*
		MSAA 采样数择定（第九十六轮）
		交换链格式已确定，按设备对「颜色 ＋ 深度」两种格式的共同支持上限钳制请求值；
		请求 > 1 时重建一次交换链以建立多重采样附件（初始化期一次性开销）。
		*/
		{
			//请求值归一（仅接受 2 / 4 / 8，其余按 1）
			uint32_t requested_samples = effective.msaa_samples;
			if (requested_samples != 2 && requested_samples != 4 && requested_samples != 8)
				requested_samples = 1;
			//按设备支持钳制
			uint32_t chosen_samples = msaa_supported_max(state.device.physical_device_get(),
				state.swapchain.format_get(), state.swapchain.depth_format_get(), requested_samples);
			//请求未被完全满足时告警
			if (requested_samples > 1 && chosen_samples < requested_samples)
				logger.warn("渲染器:: 请求 MSAA {}× 不被支持，降级为 {}×", requested_samples, chosen_samples);
			//若启用 MSAA：重建交换链以建立多重采样附件
			if (chosen_samples > 1)
			{
				state.swapchain.sample_count_set(chosen_samples);
				state.swapchain.destroy();
				if (!state.swapchain.build(state.device.physical_device_get(), state.memory, state.device.device_get(),
					state.platform.surface_get(), state.device.graphics_family_get(), state.device.present_family_get(),
					swapchain_width, swapchain_height, effective.vsync))
				{
					logger.error("渲染器:: 交换链 MSAA 附件创建失败");
					owner->device().shutdown();
					return false;
				}
			}
			state.msaa_samples = state.swapchain.sample_count_get();
			logger.info("渲染器:: MSAA 采样数 {}", state.msaa_samples);
		}
		/*
		钳制在途帧数
		图像可用信号量属「每交换链图像」（数量 = 交换链图像数），帧模块按该池轮转取用；
		仅当在途帧数不超过信号量数时，「先等帧栅栏」才能保证被取用的信号量已在前一次被消费（K13）。
		*/
		uint32_t image_available_count = state.swapchain.image_available_count_get();
		//若在途帧数超过图像可用信号量数则下调并告警
		if (image_available_count > 0 && effective.frame_overlap > image_available_count)
		{
			logger.warn("渲染器:: 在途帧数 {} 超过图像可用信号量数 {}，已下调至前者", effective.frame_overlap, image_available_count);
			effective.frame_overlap = image_available_count;
		}
		return true;
	}
}
