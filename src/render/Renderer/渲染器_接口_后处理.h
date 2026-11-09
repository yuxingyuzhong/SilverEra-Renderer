#pragma once
//公共语义类型与共享实现体访问
#include "渲染器实现访问.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器能力接口：后处理
	*/
	class Renderer_Post : public virtual Renderer_Impl_Holder
	{
	public:
		/*
		创建离屏渲染目标（颜色 ＋ 深度；颜色可作纹理采样，内容可像素回读）
		width / height 为目标尺寸；颜色 / 深度格式取当前交换链的对应格式。
		成功写出句柄（≥ 1）；未初始化 / 参数非法 / 资源创建失败时返回假、句柄保持 0，不留半初始化资源。
		*/
		bool render_target_create(uint64_t& handle_out, uint32_t width, uint32_t height);
		//销毁离屏渲染目标（句柄无效则忽略）
		void render_target_destroy(uint64_t handle);
		/*
		查询离屏渲染目标尺寸（第七十七轮新增）
		成功时写出宽 / 高（任一非空）并返回真；未初始化 / 句柄无效返回假且出参归零。
		供上层在「目标尺寸独立」场景下按实际尺寸安排后处理链 / 纹理映射，避免自行记账。
		*/
		bool render_target_size(uint64_t handle, uint32_t* width_out, uint32_t* height_out) const;
		/*
		读回离屏渲染目标的颜色内容（第二十六轮新增）
		先把在途帧等尽（frames_wait），再用一次性命令缓冲同步拷贝到主机；
		pixels 为 RGBA8 紧排（逐行自上而下、无行填充），capacity 须不小于 宽×高×4；
		成功时 width_out / height_out（非空）回填目标尺寸。未初始化 / 句柄无效 / 容量不足返回假。
		读回不改变目标内容，颜色图像仍保持 SHADER_READ_ONLY_OPTIMAL（可继续采样）。
		*/
		bool render_target_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		读回离屏渲染目标的指定子矩形（第二十七轮新增）
		x / y 为左上角、region_width / region_height 为子区域尺寸（须完整落在目标内，否则整次拒绝）；
		pixels 为该子区域 RGBA8 紧排，capacity 须不小于 区域宽×高×4；成功时回填区域尺寸。
		同样先 frames_wait，再同步读回；读回不改内容与布局。
		*/
		bool render_target_readback_region(uint64_t handle, uint32_t x, uint32_t y,
			uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		读回离屏渲染目标的深度图像（第二十八轮新增）
		values 为 32 位深度原始值数组（本层深度格式 D32_SFLOAT，按 float 解释）；
		capacity 为元素个数，须不小于 宽×高；成功时回填目标尺寸。先 frames_wait 再同步读回。
		*/
		bool render_target_readback_depth(uint64_t handle, uint32_t* values, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		读回离屏渲染目标的深度子区域（第一百六十三轮新增）
		values 为 32 位深度原始值（按 float 解释）；区域须完整落在目标内；先等尽在途帧。
		未初始化 / MSAA 目标 / 越界 / 容量不足返回假且尺寸出参归零。
		*/
		bool render_target_readback_depth_region(uint64_t handle, uint32_t x, uint32_t y,
			uint32_t region_width, uint32_t region_height, uint32_t* values, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		设置「本帧渲染到目标」
		handle 为 0 时渲染到交换链（默认）；否则本帧把场景渲染进该目标且**不呈现到窗口**，
		离屏结果在帧间恒处于可采样布局，可用 texture_bind_target 在后续帧采样合成（后处理闭环）。
		句柄无效时回落交换链路径（不报错）。
		*/
		void render_target_set(uint64_t handle);
		/*
		设置「本帧渲染到目标并同帧后处理呈现到交换链」
		handle 为 0 时关闭（回到 render_target_set 决定的默认路径）；
		句柄有效时本帧把场景渲染进该目标，随即在**同一命令缓冲内**以该目标为纹理做一次全屏后处理 pass
		直接呈现到窗口（同帧多 pass，无需跨帧中间态；优先于 render_target_set 生效）。
		本调用把中间目标组重置为「仅该目标」（额外目标清空）；需要多级链时改用
		render_target_present_chain_set 一次性给出目标组。
		要求目标范围与交换链范围一致（后处理按帧缓冲坐标 1:1 取源，故通常以当前交换链尺寸创建目标）。
		句柄无效时回落原路径（不报错）。
		*/
		void render_target_present_set(uint64_t handle);
		/*
		设置「同帧后处理链」的中间目标组（2..4 个已登记渲染目标句柄）
		handles[0] 为场景目标（等同 render_target_present_set 的目标），handles[1..] 为额外中间目标；
		链上第 i 级的源 = handles[i % count]、非末级宿 = handles[(i + 1) % count]，故链长 ≥ 2 时至少要给 2 个目标。
		count 为 0 时清除（关闭同帧后处理呈现路径）；handles 为空而 count 非零、count > 4、
		或任一句柄未登记时整组拒绝（保持原目标组不变）。
		本调用会覆盖 render_target_present_set 设定的场景目标（目标组以本调用为准）。
		*/
		bool render_target_present_chain_set(const uint64_t* handles, uint32_t count);
		/*
		设置「同帧后处理链」（若干已登记管线句柄，下标即级序）
		每级以独立管线对「上一级的输出」做一次全屏后处理：第 0 级取目标组的首个目标，
		后续各级按「级序 % 目标数」在目标组间轮转取样，末级结果呈现到交换链。
		count 为 0 时清除链（回到既有的单级默认后处理路径，使用内置 后处理.vert / 后处理.frag）；
		句柄数组为空而 count 非零、count 超过级数上限（8）、或任一句柄未登记时整链拒绝（保持原链不变）。
		链长 ≥ 2 时须先用 render_target_present_chain_set 设置 ≥ 2 个目标，否则该帧回落单级路径。
		*/
		bool post_chain_set(const uint64_t* handles, uint32_t count);
		/*
		当前链各级管线的实际采样数（第一百一十六轮）
		按级序写入 out（每级 1 个整数：1 = 单采样、2/4/8 = 多重采样）；
		返回实际写入级数 = min(链长, capacity)；out 为空 / capacity 为 0 / 未初始化返回 0。
		*/
		uint32_t post_chain_sample_counts(uint32_t* out, uint32_t capacity) const;
		/*
		读取当前后处理链级数（0 表未设链，即走单级默认后处理）
		**下标即当前级序**：若某级管线被卸载，链中该句柄会被摘除、后续级序前移，
		故与「post_chain_set 时的原始下标」可能不同。
		*/
		uint32_t post_chain_count(void) const;
		/*
		读回当前后处理链的各级管线句柄（只读诊断入口）
		返回实际写入 out 的级数 = min(链级数, capacity)；out 为空时返回 0。
		未初始化 / 未设链时返回 0。
		*/
		uint32_t post_chain_get(uint64_t* out, uint32_t capacity) const;
		/*
		把指定渲染目标的颜色绑定为「当前纹理」（供后续绘制采样）
		等价于把当前纹理替换为该目标的颜色图像；句柄无效时静默返回（保持原绑定不变）。
		与 render_target_set 配合即可完成「离屏渲染 → 采样合成」闭环。
		*/
		void texture_bind_target(uint64_t handle);
		/*
		绑定 3D LUT 纹理（第一百四十四轮新增；第一百五十七轮补清除语义）
		把该纹理写入全部帧槽的全部后处理描述符集（set 1 binding 3），
		之后以 色彩查找.frag 作为任一后处理级即可按表调色（该级参数 x = 混合强度）。
		句柄 0 表示**清除绑定**：改写为内置默认白纹理（2×2），查找级因尺寸非法而原样输出；
		无效句柄（非 0 且未登记）告警并保持原绑定；卸载该纹理会使其失效，须重新绑定。
		绑定前会等尽在途帧。
		*/
		void post_lut_bind(uint64_t texture_handle);
		//当前 3D LUT 句柄（第一百四十四轮新增；0 = 未绑定 / 已清除）
		uint64_t post_lut_get(void) const;


		/*
		设置泛光参数（阈值 / 软膝 / 强度；均为线性空间量）
		阈值与软膝须满足「阈值 > 0 且软膝 ≥ 0 且软膝 < 阈值」，强度须为有限正值；非法即整次忽略（保持原值）。
		默认 0.6 / 0.1 / 1.0（与既有 `亮部提取.frag` / `泛光叠加.frag` 的常量一致，故不调用时画面不变）。
		参数自**下一次** frame_render 的后处理链各级生效（经推送常量 offset 96 下发）。
		*/
		void bloom_param_set(float threshold, float knee, float intensity);
		/*
		设置**某一级**后处理链的泛光参数（第二十三轮新增；覆盖全局 bloom_param_set 的值）
		stage 为链内级序（0 起，须小于当前链级数）；参数校验同 bloom_param_set（非法整次忽略）。
		专设值自下一次 frame_render 生效；post_chain_set 会清除全部逐级专设值（链变则下标失效）。
		*/
		void post_stage_param_set(uint32_t stage, float threshold, float knee, float intensity);
		/*
		读取某一级的泛光专设参数（第二十四轮新增；只读诊断）
		返回 1 = 该级有专设值（out 写出 4 个分量）、0 = 未设或级序不在链内（out 写 0）；未初始化返回 0。
		*/
		uint32_t post_stage_param_get(uint32_t stage, float out[4]) const;
		/*
		四参逐级设置（第一百二十二轮新增；供「分级调色」阶段使用）
		stage 为链内级序（0 起，须小于当前链级数）；
		x = 黑位抬升 lift（−1..1）、y = 伽马 gamma（0.01..10）、z = 增益 gain（0..10）、
		w = 饱和度 saturation（0..4）；恒等参数为 (0, 1, 1, 1)。
		任一参数越界或非有限值即整次忽略并告警；生效后自下一次 frame_render 起按该级下发。
		*/
		void post_stage_param4_set(uint32_t stage, float x, float y, float z, float w);
		/*
		四参逐级设置（第三百七十三轮；通用后处理参数，不做调色语义校验）
		与 post_stage_param4_set 写同一份逐级参数，但只要求四个分量有限（NaN / Inf 整次忽略），
		不限制取值范围——供「体积雾 / 屏幕空间步进」等自定义后处理阶段使用。
		生效后自下一次 frame_render 起按该级下发（offset 96 的四个浮点）。
		*/
		void post_stage_vec4_set(uint32_t stage, float x, float y, float z, float w);
		/*
		设置某一级后处理链的描边色（第一百八十一轮新增；配合 `深度描边.frag`）
		stage 为链内级序（0 起，须小于当前链级数）；rgb 为描边色（0..1，非有限值按 0，其余逐分量钳制）。
		默认黑 (0,0,0) 表示把轮廓压暗到黑，与引入该参数前逐字节一致；post_chain_set 会清除全部逐级专设色。
		*/
		void post_stage_color_set(uint32_t stage, float r, float g, float b);
		/*
		四分量逐级颜色设置（第三百七十六轮；第四分量为自定义阶段的附加标量）
		前三通道语义同 post_stage_color_set；a 写入推送块 offset 112 的第四分量
		（0..1，非有限值按 0），供「体积雾密度噪声强度」等自定义阶段使用。
		未调用本接口时第四分量恒 0，与引入前逐字节一致。
		*/
		void post_stage_color4_set(uint32_t stage, float r, float g, float b, float a);
		/*
		逐级额外四参（第四百零一轮；推送块 offset 128 的第四组）
		供后续自定义阶段使用（如体积光阴影开关、各向异性散射、噪声抖动），不再抢占 offset 96 / 112 的分量。
		仅接受有限值（NaN / Inf 整次忽略）；stage 须在链内。
		设备 maxPushConstantsSize < 144 字节时该组不可用：写入被拒绝并记日志（参数保持全零）。
		未调用本接口时全零，既有阶段不读取该组，故画面逐字节不变。
		*/
		void post_stage_extra_set(uint32_t stage, float x, float y, float z, float w);
		/*
		批量读回当前链各级的专设参数（第八十二轮新增；只读诊断）
		out 按「级序 × 4 浮点」紧排（阈值 / 软膝 / 强度 / 保留），最多写 capacity / 4 级；
		返回值 = 实际写入的级数 = min(链级数, capacity / 4)；未专设的级写 0。
		未初始化 / out 为空 / capacity < 4 返回 0。
		*/
		uint32_t post_chain_params_get(float* out, uint32_t capacity) const;
		/*
		启用 / 关闭视锥剔除（第二十五轮新增；默认开启）
		关闭后网格绘制项一律照常登记（含完全在视锥外的项），culled_items 不再递增；
		用于调试 / 对照与「上层自行剔除」的场景。未初始化时静默忽略。
		*/
		void cull_enabled_set(bool enabled);
		//查询视锥剔除是否启用（未初始化返回默认值 true）
		bool cull_enabled(void) const;
		/*
		启用 / 关闭静态合批（第一百二十七轮；默认开启）
		开启时把**相邻且完全等价**的绘制项合并为一次实例化绘制（同管线 / 索引区 / 推送常量 / 连续实例区）；
		绘制顺序与画面逐字节不变，仅减少实际绘制次数（`Frame_Stats::drawn_items` 下降）。
		未初始化时静默忽略。
		*/
		void static_batching_set(bool enabled);
		//查询静态合批是否启用（未初始化返回默认值 true）
		bool static_batching(void) const;
		/*
		设置渲染工作线程数（第一百七十七轮；0 = 单线程，默认）
		count 钳到 0..64；与当前值相同时为空操作；线程创建失败时回落为 0 并告警。
		仅并行化纯 CPU 的「实例级视锥剔除」与「实例缓冲差异扫描」，结果与单线程逐字节一致。
		未初始化时静默忽略。
		*/
		void render_threads_set(uint32_t count);
		//查询当前渲染工作线程数（未初始化返回 0）
		uint32_t render_threads(void) const;
		/*
		设置实例级剔除的并行阈值（第二百二十二轮；钳到 1..65536）
		单次实例化绘制的实例数达到该值时逐实例剔除走按块并行；仅影响主机侧并行度，
		结果与单线程逐字节一致。默认 256 与配置项 cull_parallel_min_instances 同源。
		*/
		void cull_parallel_min_instances_set(uint32_t count);
		//查询实例级剔除的并行阈值（未初始化返回 0）
		uint32_t cull_parallel_min_instances(void) const;
		/*
		设置实例缓冲差异扫描 / 缓存回写的并行阈值（第二百二十六轮；钳到 1..65536）
		只影响主机侧并行度，结果与单线程逐字节一致。默认 512 与配置项 instance_scan_parallel_min 同源。
		*/
		void instance_scan_parallel_min_set(uint32_t count);
		//查询实例缓冲差异扫描 / 缓存回写的并行阈值（未初始化返回 0）
		uint32_t instance_scan_parallel_min(void) const;
	};
}
