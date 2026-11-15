#pragma once
//公共语义类型与前置声明
#include "渲染器类型.h"

//游戏引擎命名空间
namespace engine
{
	class Renderer;
	struct Device_State;

	/*
	渲染器域：设备
	离屏目标选择、同帧后处理呈现、后处理链参数与主机侧并行配置。
	*/
	class Render_Device
	{
	public:
		explicit Render_Device(Renderer* owner);
		~Render_Device(void);
		//不可拷贝、不可移动
		Render_Device(const Render_Device&) = delete;
		Render_Device& operator=(const Render_Device&) = delete;

		//设置本帧渲染目标（0 = 交换链）
		void render_target_set(uint64_t handle);
		//设置本帧同帧后处理呈现目标（0 = 关闭）
		void render_target_present_set(uint64_t handle);
		//设置同帧后处理链目标组
		bool render_target_present_chain_set(const uint64_t* handles, uint32_t count);

		//设置后处理链
		bool post_chain_set(const uint64_t* handles, uint32_t count);
		//读取当前后处理链级数
		uint32_t post_chain_count(void) const;
		//读回当前后处理链各级管线句柄
		uint32_t post_chain_get(uint64_t* out, uint32_t capacity) const;
		//读取当前链各级管线的实际采样数
		uint32_t post_chain_sample_counts(uint32_t* out, uint32_t capacity) const;
		//绑定 3D LUT 纹理（0 = 清除）
		void post_lut_bind(uint64_t texture_handle);
		//当前 3D LUT 句柄
		uint64_t post_lut_get(void) const;
		//设置全局泛光参数
		void bloom_param_set(float threshold, float knee, float intensity);
		//设置逐级后处理参数
		void post_stage_param_set(uint32_t stage, float threshold, float knee, float intensity);
		//读取逐级后处理专设参数
		uint32_t post_stage_param_get(uint32_t stage, float out[4]) const;
		//设置逐级调色四参
		void post_stage_param4_set(uint32_t stage, float x, float y, float z, float w);
		//设置逐级通用四参
		void post_stage_vec4_set(uint32_t stage, float x, float y, float z, float w);
		//设置逐级三分量颜色
		void post_stage_color_set(uint32_t stage, float r, float g, float b);
		//设置逐级四分量颜色
		void post_stage_color4_set(uint32_t stage, float r, float g, float b, float a);
		//设置逐级额外四参
		void post_stage_extra_set(uint32_t stage, float x, float y, float z, float w);
		//批量读回当前链各级专设参数
		uint32_t post_chain_params_get(float* out, uint32_t capacity) const;

		//启用或关闭视锥剔除
		void cull_enabled_set(bool enabled);
		//查询视锥剔除是否启用
		bool cull_enabled(void) const;
		//启用或关闭静态合批
		void static_batching_set(bool enabled);
		//查询静态合批是否启用
		bool static_batching(void) const;
		//设置渲染工作线程数
		void render_threads_set(uint32_t count);
		//查询当前渲染工作线程数
		uint32_t render_threads(void) const;
		//设置实例级剔除并行阈值
		void cull_parallel_min_instances_set(uint32_t count);
		//查询实例级剔除并行阈值
		uint32_t cull_parallel_min_instances(void) const;
		//设置实例扫描并行阈值
		void instance_scan_parallel_min_set(uint32_t count);
		//查询实例扫描并行阈值
		uint32_t instance_scan_parallel_min(void) const;

		//=== 迁移期内部入口（阶段 8 收口时降为私有）===
		//关闭（只复位设备域状态，不销毁其他域资源）
		void shutdown(void);
		//迁移期：状态直取（供帧模块过渡）
		Device_State& state(void);
		const Device_State& state(void) const;

	private:
		//门面自身（迁移期用于调用尚未迁出的门面方法与 impl）
		Renderer* owner = nullptr;
		//设备域状态
		std::unique_ptr<Device_State> state_ptr;
	};
}
