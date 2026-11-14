#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
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
	bool Renderer_Core::init(const Renderer_Config& config)
	{
		//若实现体未分配成功
		if (impl == nullptr)
		{
			logger.error("渲染器:: 实现体分配失败");
			return false;
		}
		//若已初始化
		if (impl->initialized)
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

		//① 平台 / 实例 / 设备 / 表面
		if (!renderer_init_platform(self_ptr, impl.get(), effective))
			return false;
		//⑥ 内存 / 交换链 / 在途帧数钳制
		if (!renderer_init_memory_swapchain(self_ptr, impl.get(), effective))
			return false;
		//⑧ 帧 / 着色器 / 描述符布局与管线
		if (!renderer_init_frame_shader_pipeline(self_ptr, impl.get(), effective))
			return false;
		//⑫ 管线 / 描述符 / 采样 / 粒子 / 文字 / 阴影 / 后处理 / 天空 / 阴影资源
		if (!renderer_init_graphics(self_ptr, impl.get(), effective))
			return false;
		//⑮ 网格 / 渲染目标 / 字体 / 相机
		if (!renderer_init_services(self_ptr, impl.get(), effective))
			return false;

		//⑲ UI 通道 / 事件桥接 / 原生 UI
		if (!renderer_init_ui(self_ptr, impl.get(), effective))
			return false;

		//记录配置并标记初始化完成
		impl->config = effective;
		//静态合批开关（第一百二十七轮；默认开启）
		impl->static_batching = effective.static_batching;
		//GPU 剔除开关（第二百四十九轮；默认关闭）
		impl->compute_domain->state().gpu_cull_enabled = effective.gpu_cull_enable;
		//间接绘制开关（第二百五十二轮；默认关闭）
		impl->gpu_indirect_enabled = effective.gpu_indirect_enable;
		/*
		Hi-Z 开关（第二百五十三 / 二百五十四轮）
		真正的启用（构建资源 ＋ 懒加载生成管线）放在 initialized 置真之后，以便复用门面启用路径。
		*/
		//SSAA 倍率生效（钳制 1..4；默认 1 保持既有画面逐字节不变）
		impl->ssaa_factor = effective.ssaa_factor < 1 ? 1 : (effective.ssaa_factor > 4 ? 4 : effective.ssaa_factor);
		impl->ssaa_extent_failed = false;
		//记录生效的 MSAA 采样数（已按设备支持钳制）
		impl->config.msaa_samples = impl->msaa_samples;
		//MSAA 深度解析开关（第一百九十轮；默认开启）
		impl->msaa_depth_resolve = effective.msaa_depth_resolve;
		/*
		Cluster 光照能力探测（第二百六十六轮）
		与 Hi-Z 同口径：计算路径可用即具备；每帧槽光源记录 SSBO 在启用时惰性构建。
		*/
		impl->compute_domain->state().cluster_capable = impl->compute_domain->state().compute.supported();
		//分簇网格按配置钳制（第二百六十七轮；默认 16 × 9 × 24）
		impl->compute_domain->state().cluster_grid = cluster_grid_clamp(Cluster_Grid{ effective.cluster_grid[0],
			effective.cluster_grid[1], effective.cluster_grid[2] });
		impl->initialized = true;
		/*
		Hi-Z 启用（第二百五十三 / 二百五十四轮）
		配置为真时构建资源并懒加载生成管线；失败仅告警、开关保持关闭，默认路径不受影响。
		*/
		if (effective.hiz_enable && !self().compute().hiz_enable_set(true))
			logger.warn("渲染器:: Hi-Z 启用失败（开关保持关闭）");
		/*
		Cluster 光照启用（第二百六十六轮）
		配置为真时构建每帧槽光源记录 SSBO；失败仅告警、开关保持关闭。
		*/
		if (effective.cluster_enable && !self().compute().cluster_enable_set(true))
			logger.warn("渲染器:: Cluster 光照启用失败（开关保持关闭）");
		/*
		⑳ 阴影资源（第五十二轮；第五十四轮边长可配）
		阴影目标（颜色存打包深度，边长取 impl->shadow_size）＋ 阴影管线（与场景管线共享管线布局，
		光源空间矩阵经推送常量下发）；并把阴影图写入各帧槽场景采样集的 binding 1。
		默认关闭，故不改变既有画面。
		*/
		{
			/*
			① 阴影目标（边长取配置值，颜色存打包深度）
			颜色格式固定 R8G8B8A8_UNORM（第五十四轮修正）：交换链优先 sRGB，若沿用其格式，
			打包深度的字节会被 sRGB 编码 / 解码破坏，导致片元读回深度失真并误判为全遮蔽（自阴影全黑）。
			*/
			//阴影目标固定单采样（第九十六轮；与阴影管线一致，不随 MSAA 变化）
			if (!impl->render_target.target_create(impl->shadow_target, impl->shadow_size, impl->shadow_size,
				VK_FORMAT_R8G8B8A8_UNORM, impl->swapchain.depth_format_get(), 1))
			{
				logger.error("渲染器:: 阴影目标创建失败");
				self().shutdown();
				return false;
			}
			//② 把阴影图写入各帧槽场景采样集的 binding 1（管线在首次开启阴影时懒登记）
			VkImage shadow_image = VK_NULL_HANDLE;
			VkImageView shadow_view = VK_NULL_HANDLE;
			VkImageView shadow_depth_view = VK_NULL_HANDLE;
			VkExtent2D shadow_extent{};
			VkImageView default_view = VK_NULL_HANDLE;
			VkSampler sampler = VK_NULL_HANDLE;
			if (!impl->render_target.target_get(impl->shadow_target, shadow_image, shadow_view,
				shadow_depth_view, shadow_extent)
				|| !impl->resource_domain->state().texture.texture_bind_get(impl->resource_domain->state().default_texture, default_view, sampler))
			{
				logger.error("渲染器:: 阴影图绑定失败：资源查询失败");
				self().shutdown();
				return false;
			}
			for (std::size_t slot = 0; slot < impl->config.frame_overlap; ++slot)
				//把阴影图写入该帧槽场景采样集的 binding 1
				if (!impl->pipeline.shadow_bind(static_cast<uint32_t>(slot), sampler, shadow_view))
				{
					logger.error("渲染器:: 阴影图绑定失败（帧槽 {}）", slot);
					self().shutdown();
					return false;
				}
			/*
			③ 聚光灯阴影目标（第四百八十三轮；第五百零七轮扩为 4 张，按需建立）
			瓦片 0 恒建（与旧单盏行为一致）；瓦片 1..3 仅在 spot_shadow_all 首次开启时按需创建，
			避免未使用多盏时的显存与初始化开销。
			*/
			for (std::size_t tile = 0; tile < 1; ++tile)
			{
				if (!impl->render_target.target_create(impl->spot_shadow_targets[tile], impl->shadow_size, impl->shadow_size,
					VK_FORMAT_R8G8B8A8_UNORM, impl->swapchain.depth_format_get(), 1))
				{
					logger.error("渲染器:: 聚光灯阴影目标创建失败（瓦片 {}）", tile);
					self().shutdown();
					return false;
				}
				VkImage spot_shadow_image = VK_NULL_HANDLE;
				VkImageView spot_shadow_view = VK_NULL_HANDLE;
				VkImageView spot_shadow_depth_view = VK_NULL_HANDLE;
				VkExtent2D spot_shadow_extent{};
				if (!impl->render_target.target_get(impl->spot_shadow_targets[tile], spot_shadow_image, spot_shadow_view,
					spot_shadow_depth_view, spot_shadow_extent))
				{
					logger.error("渲染器:: 聚光灯阴影目标查询失败（瓦片 {}）", tile);
					self().shutdown();
					return false;
				}
				for (std::size_t slot = 0; slot < impl->config.frame_overlap; ++slot)
					//把该瓦片阴影图写入帧槽场景采样集 binding 9 的对应数组元素
					if (!impl->pipeline.spot_shadow_bind(static_cast<uint32_t>(slot), sampler, spot_shadow_view,
						static_cast<uint32_t>(tile)))
					{
						logger.error("渲染器:: 聚光灯阴影图绑定失败（帧槽 {}，瓦片 {}）", slot, tile);
						self().shutdown();
						return false;
					}
			}
		}
		logger.info("渲染器:: 初始化完成（校验层 {}）", impl->device.validation_get() ? "开" : "关");
		return true;
	}
}
