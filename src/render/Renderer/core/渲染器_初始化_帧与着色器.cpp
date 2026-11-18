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

	//初始化阶段：帧 / 着色器模块 / 描述符布局与管线
	bool renderer_init_frame_shader_pipeline(Renderer* owner, Device_State& state, Renderer_Config& effective)
	{
		//⑧ 帧（像素回读缓冲依赖内存模块，故传入 memory）
		/*
		实例存储容量与渲染工作线程（第一百七十七轮）
		容量钳到 [16, 1048576]，随后按该容量分配逐帧实例缓冲（默认 256，与引入该配置前一致）；
		线程数大于 0 时启动工作线程，启动失败回落为单线程（仅影响耗时，不影响结果）。
		*/
		{
			//容量归一
			uint32_t instance_capacity = effective.instance_capacity;
			if (instance_capacity < 16)
				instance_capacity = 16;
			if (instance_capacity > 1048576)
				instance_capacity = 1048576;
			state.instance_capacity = instance_capacity;
			/*
			实例登记缓冲预保留（第一百九十四轮，优化）
			pending_items 已在下方按实例容量预留（第一百三十六轮）；此处补 pending_instances 的同类预留，
			避免帧循环内首次扩容（行为不变）。
			*/
			state.pending_instances.reserve(state.instance_capacity);
			//掩码缓冲按容量预留（避免逐次调用分配）
			state.instanced_cull_mask.reserve(instance_capacity);
			state.instance_changed_mask.reserve(instance_capacity);
			//线程数归一
			uint32_t render_threads = effective.render_threads;
			if (render_threads > 64)
				render_threads = 64;
			//启动工作线程（0 = 单线程）
			if (render_threads > 0 && !state.thread_pool.threads_start(render_threads))
			{
				logger.warn("渲染器:: 渲染工作线程启动失败，回落为单线程");
				render_threads = 0;
			}
			state.render_threads = render_threads;
			/*
			实例剔除并行阈值（第二百二十二轮；钳 1..65536）
			默认 256 与引入该配置前一致；调低可让小批量实例也走并行，调高则更保守。
			*/
			uint32_t cull_parallel_min = effective.cull_parallel_min_instances;
			if (cull_parallel_min < 1)
				cull_parallel_min = 1;
			if (cull_parallel_min > 65536)
				cull_parallel_min = 65536;
			state.cull_parallel_min_instances = cull_parallel_min;
			/*
			差异扫描 / 缓存回写并行阈值（第二百二十六轮；钳 1..65536）
			默认 512 与引入该配置前一致。
			*/
			uint32_t scan_parallel_min = effective.instance_scan_parallel_min;
			if (scan_parallel_min < 1)
				scan_parallel_min = 1;
			if (scan_parallel_min > 65536)
				scan_parallel_min = 65536;
			state.instance_scan_parallel_min = scan_parallel_min;
			/*
			并行录制最小项数（第二百零二轮；钳到 2..4096）
			线程数为 0 时该值无作用（始终单线程）。
			*/
			uint32_t parallel_min_items = effective.parallel_min_items;
			if (parallel_min_items < 2)
				parallel_min_items = 2;
			if (parallel_min_items > 4096)
				parallel_min_items = 4096;
			state.parallel_min_items = parallel_min_items;
			//把阈值同步给帧模块（第二百零二轮）
			state.frame.parallel_min_items_set(parallel_min_items);
			/*
			并行段数上限（第二百零三轮；钳到 2..32）
			段数 = min(线程数, 上限, 项数 / 16)。
			*/
			uint32_t parallel_max_segments = effective.parallel_max_segments;
			if (parallel_max_segments < 2)
				parallel_max_segments = 2;
			if (parallel_max_segments > 32)
				parallel_max_segments = 32;
			state.parallel_max_segments = parallel_max_segments;
			state.frame.parallel_max_segments_set(parallel_max_segments);
			//把线程池交给帧模块（第一百九十七轮：场景 pass 次级缓冲并行录制；空 / 0 线程时退回单线程）
			state.frame.thread_pool_set(&state.thread_pool);
			logger.info("渲染器:: 渲染工作线程 {}（每帧实例容量 {}）", render_threads, instance_capacity);
		}
		/*
		预留帧内待绘制项与实例存储容量（第一百三十六轮）
		容量取自实例上限，避免逐帧扩容；仅在初始化时执行一次。
		*/
		state.pending_items.reserve(state.instance_capacity);
		state.pending_instances.reserve(state.instance_capacity);
		//逐绘制项采样集输入缓冲按预留上限预分配（第二百二十一轮，避免逐帧扩容）
		owner->resource().frame_inputs_reserve();
		//实例级剔除的筛选缓冲（第一百五十三轮；按实例上限预留，避免逐帧扩容）
		state.instanced_cull_scratch.reserve(static_cast<std::size_t>(state.instance_capacity) * 16);
		state.batch_scratch.reserve(state.instance_capacity);
		//阴影列表与后处理绘制项的复用缓冲（第一百八十二轮；避免逐帧堆分配）
		state.shadow_items_scratch.reserve(64);
		state.post_items_scratch.reserve(POST_CHAIN_MAX_STAGES + 2);
		//后处理级管线序列复用缓冲（第一百八十七轮）
		state.post_stage_pipelines_scratch.reserve(POST_CHAIN_MAX_STAGES + 1);

		if (!state.frame.build(state.device.device_get(), state.device.physical_device_get(),
			state.device.graphics_family_get(), state.device.graphics_queue_get(),
			state.device.present_family_get(), state.device.present_queue_get(),
			state.swapchain, state.memory, effective.frame_overlap))
		{
			logger.error("渲染器:: 帧基础设施创建失败");
			owner->device().shutdown();
			return false;
		}
		//⑨ / ⑩ 着色器宿主与默认场景着色器模块
		if (!owner->resource().scene_shaders_load(effective.shader_dir, state.scene_input_locations))
		{
			logger.error("渲染器:: 场景着色器初始化失败");
			owner->device().shutdown();
			return false;
		}
		//⑪ 管线：描述符布局 / 管线布局 / 描述符池与每帧槽描述符集
		{
			Pipeline_Config pipeline_config{};
			pipeline_config.color_format = state.scene_color_format();
			pipeline_config.depth_format = state.swapchain.depth_format_get();
			pipeline_config.frame_overlap = effective.frame_overlap;
			pipeline_config.sample_count = state.msaa_samples;
			/*
			推送常量块上限探测（第四百零一轮）
			Vulkan 保底 128 字节；桌面常见 256。本层把后处理第四组（offset 128..143）做成「有则用、无则降级」，
			故先取物理设备上限：≥ 144 用 144 并开放 post_stage_extra_set，否则维持 128 并拒绝该接口。
			*/
			VkPhysicalDeviceProperties device_props{};
			vkGetPhysicalDeviceProperties(state.device.physical_device_get(), &device_props);
			state.post_extra_supported = device_props.limits.maxPushConstantsSize >= 144;
			pipeline_config.max_push_constant_size = state.post_extra_supported ? 144 : 128;
			state.frame.post_extra_enabled = state.post_extra_supported;
			if (!state.post_extra_supported)
				logger.warn("渲染器:: 设备推送常量上限 {} 字节不足 144，后处理逐级 extra 参数已禁用",
					device_props.limits.maxPushConstantsSize);
			if (!owner->resource().scene_pipeline_infrastructure_build(
				static_cast<uint32_t>(state.scene_color_format()), static_cast<uint32_t>(state.swapchain.depth_format_get()),
				effective.frame_overlap, state.msaa_samples, pipeline_config.max_push_constant_size))
			{
				logger.error("渲染器:: 管线基础设施创建失败");
				owner->device().shutdown();
				return false;
			}
		}
		/*
		载入管线缓存（第一百零六轮；可选）
		须在创建任何图形管线之前注入；文件缺失或读取失败仅告警，不阻断初始化。
		*/
		if (!effective.pipeline_cache_path.empty())
		{
			std::ifstream cache_stream(path_from_utf8(effective.pipeline_cache_path), std::ios::binary | std::ios::ate);
			//文件存在则读出全部字节
			if (cache_stream.is_open())
			{
				std::streamsize cache_size = cache_stream.tellg();
				if (cache_size > 0)
				{
					std::vector<uint8_t> cache_bytes(static_cast<std::size_t>(cache_size));
					cache_stream.seekg(0);
					cache_stream.read(reinterpret_cast<char*>(cache_bytes.data()), cache_size);
					if (!cache_bytes.empty())
						owner->resource().pipeline_cache_set(cache_bytes.data(), cache_bytes.size());
				}
				cache_stream.close();
			}
		}
		return true;
	}
}
