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

	//初始化（任一环节失败即逆序回退）
	bool Render_Device::init(const Renderer_Config& config)
	{
		//若设备域状态未分配成功
		if (state_ptr == nullptr)
		{
			logger.error("渲染器:: 设备域状态分配失败");
			return false;
		}
		//若已初始化
		if (state_ptr->initialized)
		{
			logger.warn("渲染器:: 已初始化，忽略重复 init");
			return true;
		}

		//参数校验：宽高非零
		if (config.width == 0 || config.height == 0)
		{
			logger.error("渲染器:: 窗口尺寸不得为零（{}×{}）", config.width, config.height);
			return false;
		}
		//参数校验：在途帧数量在 [1,3]
		if (config.frame_overlap < 1 || config.frame_overlap > 3)
		{
			logger.error("渲染器:: 在途帧数量越界（{}，应在 [1,3]）", config.frame_overlap);
			return false;
		}

		//生效配置（Release 下强制关闭校验层；K21）
		Renderer_Config effective = config;
#ifdef NDEBUG
		effective.validation = false;
#endif
		/*
		离屏目标默认 16F 开关须在管线烘焙之前落位（第 757 轮）
		HDR 默认路径要求场景管线按 16F 烘焙，故不能等到阶段函数之后再赋值。
		*/
		state_ptr->hdr_targets = effective.hdr_targets;
		//色调映射算子同须早于后处理管线惰性登记（第 759 / 760 轮；越界按 0）
		state_ptr->hdr_tone_operator = effective.hdr_tone_operator > 4 ? 0 : effective.hdr_tone_operator;

		//① 平台 / 实例 / 设备 / 表面
		if (!renderer_init_platform(owner, *state_ptr, effective))
			return false;
		//⑥ 内存 / 交换链 / 在途帧数钳制
		if (!renderer_init_memory_swapchain(owner, *state_ptr, effective))
			return false;
		//⑧ 帧 / 着色器 / 描述符布局与管线
		if (!renderer_init_frame_shader_pipeline(owner, *state_ptr, effective))
			return false;
		//⑫ 管线 / 描述符 / 采样 / 粒子 / 文字 / 阴影 / 后处理 / 天空 / 阴影资源
		if (!renderer_init_graphics(owner, *state_ptr, effective))
			return false;
		//⑮ 网格 / 渲染目标 / 字体 / 相机
		if (!renderer_init_services(owner, *state_ptr, effective))
			return false;

		//⑲ UI 通道 / 事件桥接 / 原生 UI
		if (!renderer_init_ui(owner, *state_ptr, effective))
			return false;

		//记录配置并标记初始化完成
		state_ptr->config = effective;
		//静态合批开关（第一百二十七轮；默认开启）
		state_ptr->static_batching = effective.static_batching;
		//GPU 剔除开关（第二百四十九轮；默认关闭）
		//间接绘制开关（第二百五十二轮；默认关闭）
		state_ptr->gpu_indirect_enabled = effective.gpu_indirect_enable;
		/*
		Hi-Z 开关（第二百五十三 / 二百五十四轮）
		真正的启用（构建资源 ＋ 懒加载生成管线）放在 initialized 置真之后，以便复用门面启用路径。
		*/
		//SSAA 倍率生效（钳制 1..4；默认 1 保持既有画面逐字节不变）
		state_ptr->ssaa_factor = effective.ssaa_factor < 1 ? 1 : (effective.ssaa_factor > 4 ? 4 : effective.ssaa_factor);
		state_ptr->ssaa_extent_failed = false;
		//记录生效的 MSAA 采样数（已按设备支持钳制）
		state_ptr->config.msaa_samples = state_ptr->msaa_samples;
		//MSAA 深度解析开关（第一百九十轮；默认开启）
		state_ptr->msaa_depth_resolve = effective.msaa_depth_resolve;
		//Cluster 能力与初始分簇网格由计算域承接（初始化 guard 尚未置位）
		owner->compute().options_apply(effective.gpu_cull_enable, effective.cluster_grid);
		state_ptr->initialized = true;
		/*
		Hi-Z 启用（第二百五十三 / 二百五十四轮）
		配置为真时构建资源并懒加载生成管线；失败仅告警、开关保持关闭，默认路径不受影响。
		*/
		if (effective.hiz_enable && !(*owner).compute().hiz_enable_set(true))
			logger.warn("渲染器:: Hi-Z 启用失败（开关保持关闭）");
		/*
		Cluster 光照启用（第二百六十六轮）
		配置为真时构建每帧槽光源记录 SSBO；失败仅告警、开关保持关闭。
		*/
		if (effective.cluster_enable && !(*owner).compute().cluster_enable_set(true))
			logger.warn("渲染器:: Cluster 光照启用失败（开关保持关闭）");
		/*
		⑳ 阴影资源（第五十二轮；第五十四轮边长可配）
		阴影目标（颜色存打包深度，边长取场景域阴影分辨率）＋ 阴影管线（与场景管线共享管线布局，
		光源空间矩阵经推送常量下发）；并把阴影图写入各帧槽场景采样集的 binding 1。
		默认关闭，故不改变既有画面。
		*/
		{
			const uint32_t shadow_size = owner->scene().shadow_resolution();
			const uint32_t depth_format = static_cast<uint32_t>(state_ptr->swapchain.depth_format_get());
			if (!owner->resource().render_target_create_format(state_ptr->shadow_target, shadow_size, shadow_size,
				static_cast<uint32_t>(VK_FORMAT_R8G8B8A8_UNORM), depth_format, 1))
			{
				logger.error("渲染器:: 阴影目标创建失败");
				(*owner).shutdown();
				return false;
			}
			Render_Target_Native_Info shadow_info{};
			uint64_t default_view = 0;
			uint64_t sampler = 0;
			if (!owner->resource().render_target_native_get(state_ptr->shadow_target, shadow_info)
				|| !owner->resource().texture_native_get(owner->resource().default_texture_handle(), default_view, sampler))
			{
				logger.error("渲染器:: 阴影图绑定失败：资源查询失败");
				(*owner).shutdown();
				return false;
			}
			for (std::size_t slot = 0; slot < state_ptr->config.frame_overlap; ++slot)
				if (!owner->resource().scene_shadow_bind(static_cast<uint32_t>(slot), sampler, shadow_info.color_view))
				{
					logger.error("渲染器:: 阴影图绑定失败（帧槽 {}）", slot);
					(*owner).shutdown();
					return false;
				}
			for (std::size_t tile = 0; tile < 1; ++tile)
			{
				if (!owner->resource().render_target_create_format(state_ptr->spot_shadow_targets[tile], shadow_size, shadow_size,
					static_cast<uint32_t>(VK_FORMAT_R8G8B8A8_UNORM), depth_format, 1))
				{
					logger.error("渲染器:: 聚光灯阴影目标创建失败（瓦片 {}）", tile);
					(*owner).shutdown();
					return false;
				}
				Render_Target_Native_Info spot_info{};
				if (!owner->resource().render_target_native_get(state_ptr->spot_shadow_targets[tile], spot_info))
				{
					logger.error("渲染器:: 聚光灯阴影目标查询失败（瓦片 {}）", tile);
					(*owner).shutdown();
					return false;
				}
				for (std::size_t slot = 0; slot < state_ptr->config.frame_overlap; ++slot)
					if (!owner->resource().scene_spot_shadow_bind(static_cast<uint32_t>(slot), sampler, spot_info.color_view,
						static_cast<uint32_t>(tile)))
					{
						logger.error("渲染器:: 聚光灯阴影图绑定失败（帧槽 {}，瓦片 {}）", slot, tile);
						(*owner).shutdown();
						return false;
					}
			}
		}
		logger.info("渲染器:: 初始化完成（校验层 {}）", state_ptr->device.validation_get() ? "开" : "关");
		return true;
	}
}
