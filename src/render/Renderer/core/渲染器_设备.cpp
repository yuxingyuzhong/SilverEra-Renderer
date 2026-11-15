#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"

//游戏引擎命名空间
namespace engine
{
	//构造（只创建域状态，不接触 GPU 资源）
	Render_Device::Render_Device(Renderer* owner)
		: owner(owner),
		state_ptr(new(std::nothrow) Device_State())
	{
	}

	//析构（资源由门面关闭流程统一回收）
	Render_Device::~Render_Device(void)
	{
	}

	//设备域
	Device_State& Render_Device::state(void)
	{
		return *state_ptr;
	}

	//设备域（只读）
	const Device_State& Render_Device::state(void) const
	{
		return *state_ptr;
	}

	//关闭（按原门面语义复位设备域状态，不销毁其他域资源）
	void Render_Device::shutdown(void)
	{
		//本帧渲染目标回落交换链
		state().active_target = 0;
		//关闭同帧后处理呈现路径并清空目标组
		state().present_target = 0;
		state().present_extra.clear();
		//清空后处理链与懒登记句柄（资源由对应模块统一回收）
		state().post_chain.clear();
		state().post_pipeline = 0;
		state().fullscreen_mesh = 0;
		//复位 SSAA 状态（目标已随资源域渲染目标模块销毁）
		state().ssaa_scene_target = 0;
		state().ssaa_resolve_target = 0;
		state().ssaa_extent_width = 0;
		state().ssaa_extent_height = 0;
		state().ssaa_target_factor = 0;
		state().ssaa_pipeline = 0;
		state().ssaa_factor = 1;
		state().ssaa_extent_failed = false;
		//逐级专设值失效（参数本身与全局泛光参数按原语义保留）
		for (std::size_t stage = 0; stage < POST_CHAIN_MAX_STAGES; ++stage)
		{
			state().post_stage_param_valid[stage] = false;
			state().post_stage_color_valid[stage] = false;
			state().post_stage_extra_valid[stage] = false;
		}
	}
}
