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

namespace engine
{
	//创建离屏渲染目标

	void Render_Device::cull_enabled_set(bool enabled)
	{
		//未初始化则静默返回
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		state_ptr->cull_enabled = enabled;
	}

	bool Render_Device::cull_enabled(void) const
	{
		//未初始化时返回默认值（开启）
		if (state_ptr == nullptr || !state_ptr->initialized)
			return true;
		return state_ptr->cull_enabled;
	}

	void Render_Device::static_batching_set(bool enabled)
	{
		//未初始化则静默返回
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		state_ptr->static_batching = enabled;
	}

	bool Render_Device::static_batching(void) const
	{
		//未初始化时返回默认值（开启）
		if (state_ptr == nullptr || !state_ptr->initialized)
			return true;
		return state_ptr->static_batching;
	}

	void Render_Device::render_threads_set(uint32_t count)
	{
		//未初始化则静默返回
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//线程数钳到 0..64
		if (count > 64)
			count = 64;
		//同值重复设置为空操作
		if (count == state_ptr->thread_pool.thread_count_get())
			return;
		//先回收旧线程，再按新数量启动
		state_ptr->thread_pool.threads_stop();
		state_ptr->render_threads = 0;
		if (count > 0 && !state_ptr->thread_pool.threads_start(count))
		{
			logger.warn("渲染器:: 渲染工作线程切换失败，保持单线程");
			return;
		}
		state_ptr->render_threads = state_ptr->thread_pool.thread_count_get();
		logger.info("渲染器:: 渲染工作线程已设为 {}", state_ptr->render_threads);
	}

	uint32_t Render_Device::render_threads(void) const
	{
		//未初始化时返回默认值（0 = 单线程）
		if (state_ptr == nullptr || !state_ptr->initialized)
			return 0;
		return state_ptr->thread_pool.thread_count_get();
	}

	void Render_Device::cull_parallel_min_instances_set(uint32_t count)
	{
		//未初始化则静默返回
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//钳到 1..65536
		if (count < 1)
			count = 1;
		if (count > 65536)
			count = 65536;
		//同值重复设置为空操作
		if (state_ptr->cull_parallel_min_instances == count)
			return;
		state_ptr->cull_parallel_min_instances = count;
		logger.info("渲染器:: 实例剔除并行阈值已设为 {}", count);
	}

	uint32_t Render_Device::cull_parallel_min_instances(void) const
	{
		//未初始化时返回 0
		if (state_ptr == nullptr || !state_ptr->initialized)
			return 0;
		return static_cast<uint32_t>(state_ptr->cull_parallel_min_instances);
	}

	void Render_Device::instance_scan_parallel_min_set(uint32_t count)
	{
		//未初始化则静默返回
		if (state_ptr == nullptr || !state_ptr->initialized)
			return;
		//钳到 1..65536
		if (count < 1)
			count = 1;
		if (count > 65536)
			count = 65536;
		//同值重复设置为空操作
		if (state_ptr->instance_scan_parallel_min == count)
			return;
		state_ptr->instance_scan_parallel_min = count;
		logger.info("渲染器:: 实例扫描并行阈值已设为 {}", count);
	}

	uint32_t Render_Device::instance_scan_parallel_min(void) const
	{
		//未初始化时返回 0
		if (state_ptr == nullptr || !state_ptr->initialized)
			return 0;
		return static_cast<uint32_t>(state_ptr->instance_scan_parallel_min);
	}
}
