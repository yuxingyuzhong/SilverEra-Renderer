#pragma once
//公共语义类型与共享实现体访问
#include "渲染器实现访问.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器能力接口：管线
	*/
	class Renderer_Pipeline : public virtual Renderer_Impl_Holder
	{
	public:
		/*
		设置后续绘制所用混合档（Opaque / Alpha / Additive）
		切档会触发一次图形管线重建（内部先等设备空闲），故应在帧与帧之间调用而非帧内；
		同档重复设置为空操作；重建失败时记录错误并保持档位记录不变（不改变帧循环时序）。
		注意：本方法只作用于**默认场景管线**；具名管线的混合档在 pipeline_load 登记时确定，不受影响。
		Additive（第二百二十七轮）：颜色按「源 + 目标」累加，适合自发光叠加；非不透明档的绘制项不参与静态合批。
		*/
		void blend_mode_set(Blend_Mode mode);

		/*
		登记具名图形管线（顶点 / 片元着色器文件名 ＋ 混合档 ＋ 可选覆盖率写入）
		成功写出句柄（≥ 1）；具名管线与默认场景管线共享同一管线布局与描述符集布局，
		仅「着色器对 ＋ 混合档 ＋ 覆盖率开关」不同；未初始化 / 着色器加载失败 / 管线创建失败时返回假、句柄保持 0。
		alpha_to_coverage 仅在 MSAA 采样数 > 1 时生效（1× 下自动关闭，行为与既有 alpha 混合一致）。
		*/
		bool pipeline_load(uint64_t& handle_out, const std::string& vert_shader, const std::string& frag_shader,
			Blend_Mode blend_mode = Blend_Mode::Opaque, bool alpha_to_coverage = false);
		//卸载具名图形管线（句柄 0 或无效句柄忽略；卸载后其句柄失效）
		void pipeline_unload(uint64_t handle);
		/*
		设置后续绘制所用图形管线
		handle 为 0 时选回默认场景管线；句柄无效（未登记 / 已卸载）时回落默认场景管线。
		*/
		void pipeline_bind_set(uint64_t handle);

	};
}
