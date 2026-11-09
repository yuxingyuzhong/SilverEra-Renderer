#pragma once
//公共语义类型与共享实现体访问
#include "渲染器实现访问.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器能力接口：光照
	*/
	class Renderer_Lighting : public virtual Renderer_Impl_Holder
	{
	public:
		/*
		设置距离雾（第三十八轮新增；默认关闭）
		color 为雾色（分量 0..1），density 为强度（0..1，0 = 关闭），start / end 为线性距离区间（世界单位，须 start ≥ 0 且 end > start）。
		片元按「相机 → 片元」距离在 [start, end] 间取 `t² × density` 混合雾色；任一参数非法即整次忽略并告警。
		*/
		void fog_set(const float color[3], float density, float start, float end);
		/*
		设置指数平方雾（第一百四十三轮新增）
		color 为雾色（分量 0..1），density 为密度（0..1，按「每世界单位」计；0 = 关闭），
		start 为起雾距离（片元近于此距离无雾）。
		因子为 `1 − exp(−(density × (距离 − start))²)`：近处趋零、远处平滑饱和到 1，
		与 fog_set 的线性区间模式互斥（本调用把模式切到 1）。任一参数非法即整次忽略并告警。
		*/
		void fog_exp2_set(const float color[3], float density, float start);
		//读取当前雾模式（第一百四十三轮新增；0 = 线性区间、1 = 指数平方；未初始化返回 0）
		uint32_t fog_mode_get(void) const;
		/*
		设置雾的高度衰减（第一百四十三轮新增）
		falloff 为「每世界单位」的衰减系数（0..10；0 = 关闭，默认）。
		雾因子再乘以 `exp(−falloff × max(世界 y, 0))`：越高越稀薄，地面附近不受影响。
		负值、越界或非数即整次忽略并告警（保持既有系数）。
		*/
		void fog_height_set(float falloff);
		//关闭距离雾（第三十八轮新增；密度归零，其余参数保持既有值）
		void fog_clear(void);
		/*
		设置环境光颜色（第四十轮新增；默认白）
		color 为环境光颜色（分量 0..1），与环境光强度（`light_set` / `Scene_Data::light_ambient`）相乘后叠加到照度；
		任一分量非法（越界 / 非数 / 空指针）即整次忽略并告警，保持既有颜色。
		*/
		void ambient_color_set(const float color[3]);
		/*
		设置半球环境光（第四十七轮新增）
		ground_color 为「朝下方向的环境光颜色」（分量 0..1；朝上方向仍用 `ambient_color_set` 的颜色），
		hemisphere 为混合强度（按 0..1 钳制；0 = 关闭，只用单一环境光颜色）。
		法线朝上取 ambient_color、朝下取 ground_color，其间按法线 y 分量线性插值。
		颜色任一分量非法（越界 / 非数 / 空指针）即整次忽略并告警；强度非数按 0 处理。
		*/
		void ambient_ground_set(const float ground_color[3], float hemisphere);
		/*
		设置方向光的逐光高光缩放（第五十轮新增）
		scale 为方向光高光缩放（0..100；1 = 跟随全局高光强度，0 = 方向光不产生高光）；
		越界 / 非数即整次忽略并告警。
		*/
		void directional_specular_set(float scale);
		/*
		方向光阴影开关 / 深度偏置（第五十二轮新增）
		开启后每帧以方向光视角把场景几何渲染进 1024×1024 阴影图（RGBA8 打包 32 位深度），
		主 pass 按「光源空间深度比较」对方向光漫反射与高光做遮蔽；默认关闭（既有画面逐字节不变）。
		*/
		void shadow_enable_set(bool enabled);
		//阴影是否启用（未初始化时为假）
		bool shadow_enabled(void) const;
		//设置阴影深度偏置（0..0.05；越界 / 非数整次忽略）
		void shadow_bias_set(float bias);
		//当前阴影深度偏置（未初始化时为默认 0.003）
		float shadow_bias(void) const;
		/*
		阴影软度 / 强度（第五十三轮新增）
		softness ∈ [0,1]：0 = 单点硬边，≥0.5 = 3×3 邻域 PCF 平均（越界 / 非数整次忽略，默认 0）；
		strength ∈ [0,1]：0 = 阴影完全无效、1 = 全遮蔽（越界 / 非数整次忽略，默认 1）。
		*/
		void shadow_softness_set(float softness);
		//当前阴影软度（未初始化时为默认 0）
		float shadow_softness(void) const;
		void shadow_strength_set(float strength);
		//当前阴影强度（未初始化时为默认 1）
		float shadow_strength(void) const;
		/*
		聚光灯阴影质量参数（第四百七十六轮新增）
		与方向光阴影的偏置 / 软度 / 强度相互独立（聚光为透视投影，深度精度与正交级联不同）：
		bias ∈ [0, 0.05]（默认 0.010）、softness ∈ [0, 1]（默认 0）、strength ∈ [0, 1]（默认 1）。
		任一非法即整次忽略并告警；仅在 `Light_Data.spot_shadow_enabled` 非 0 时参与着色。
		*/
		void spot_shadow_params_set(float bias, float softness, float strength);
		//当前聚光灯阴影参数（未初始化时写默认 0.010 / 0 / 1）
		void spot_shadow_params_get(float* bias_out, float* softness_out, float* strength_out) const;
		/*
		聚光灯阴影序号（第四百七十九轮新增）
		index 指向已登记的聚光灯槽位（0..7）：阴影图只由该盏聚光投影，且只遮蔽该盏聚光灯；
		越界（≥ 8）或该槽不是聚光灯时按「无聚光阴影」处理；非法值整次忽略并告警。
		*/
		void spot_shadow_index_set(int index);
		//当前聚光灯阴影序号（未初始化时为默认 0）
		int spot_shadow_index(void) const;
		/*
		多盏聚光灯阴影开关（第五百一十一轮新增）
		true：前 8 盏聚光灯按声明顺序各占瓦片 0..7 独立投影（瓦片 1..7 首次开启时按需创建）；
		false：沿用 spot_shadow_index 的单盏语义。
		供运行期切换使用；关卡装载路径仍由 Light_Data.spot_shadow_all 驱动，二者语义一致。
		*/
		void spot_shadow_all_set(bool enabled);
		//当前是否多盏聚光灯阴影（未初始化时为默认假）
		bool spot_shadow_all(void) const;
		/*
		逐盏聚光灯阴影开关（第六百四十八轮；A2 数据化）
		index 为前向槽位（0..7）：true = 该盏参与阴影、false = 该盏不投影（其余盏与方向光阴影不受影响）；
		越界返回假（配置不变）。默认全开，与引入前逐字节一致。
		*/
		bool spot_shadow_light_set(uint32_t index, bool enabled);
		/*
		聚光阴影图集（第六百七十轮；C1）
		enabled = true 时把 8 张聚光阴影瓦片并入单张 4×2 图集（每瓦片 shadow_resolution 见方），
		聚光阴影 pass 改为写图集子视口；enabled = false 恢复 8 张独立阴影图（默认，逐字节一致）。
		创建失败仅告警并保持独立图口径；返回实际生效状态。
		*/
		bool spot_shadow_atlas_set(bool enabled);
		//当前聚光阴影图集是否实际生效（未初始化 / 创建失败时为假）
		bool spot_shadow_atlas(void) const;
		/*
		PCSS 软阴影开关与预算（第六百五十七轮；仅影响着色器采样，不重建阴影图）
		enabled = false 恢复旧口径（硬边 / 3×3 软度，逐字节一致）；
		search 钳到 [1, 32] 纹素（阻塞物搜索尺度）；max_radius 钳到 [1, 16] 纹素（PCF 半径上限）。
		非有限值整次忽略（保持原配置）。
		*/
		void shadow_pcss_set(bool enabled, float search, float max_radius);
		/*
		逐盏聚光灯阴影偏置缩放（第六百五十八轮；仅影响采样偏置，不重建阴影图）
		index 为前向槽位（0..7）；有效偏置 = 既有逐瓦片偏置 × 该缩放（钳到 [0.1, 10]，非有限值忽略）。
		默认 1（与引入前一致）；越界返回假。
		*/
		bool spot_shadow_bias_scale_set(uint32_t index, float scale);
		/*
		逐盏聚光灯阴影质量缩放（第七百零八轮；仅影响着色器采样，不重建阴影图）
		index 为前向槽位（0..7）；softness_scale 钳到 [0.25, 4]（有效软度 = 全局软度 × 该值），
		strength_scale 钳到 [0, 1]（有效强度 = 全局强度 × 该值）。默认 1 / 1（与引入前一致）。
		非有限值整次忽略；越界槽位返回假。
		*/
		bool spot_shadow_quality_set(uint32_t index, float softness_scale, float strength_scale);
		/*
		逐盏聚光灯阴影优先级（第七百零九轮；按光源槽位 0..31）
		仅在「多盏聚光阴影开启且可投影聚光灯超过 8 盏」时生效：按优先级降序、同分槽位升序取前 8 盏投影。
		默认 0（全部同分 → 取登记顺序最前的 8 盏，与引入前一致）；非有限值整次忽略，越界槽位返回假。
		*/
		bool spot_shadow_priority_set(uint32_t index, float priority);
		/*
		阴影分辨率预算（第六百五十九轮；0 = 关闭）
		按 RGBA8 折算总阴影光栅内存：边长 = floor(√(字节 /(单位数 × 4)))，向下取 256 倍数并钳 256..4096，
		再取不超过该值的最大受支持档（512 / 1024 / 2048）；结果与请求分辨率取较小值。
		*/
		void shadow_budget_set(uint64_t bytes);
		/*
		曝光补偿（第六百六十轮；EV，钳到 [-4, 4]，非有限值忽略）
		最终颜色按 2^EV 缩放；0（默认）为乘性恒等，画面逐字节不变。
		*/
		bool exposure_set(float ev);
		//当前曝光补偿（EV）
		float exposure_get(void) const;
		/*
		阴影纹素尺寸上限（第六百六十一轮；世界单位 / 纹素，0 = 关闭（默认））
		>0 时每片级联半径按「上限 × 分片边长 / 2」收紧；非有限值或负数忽略，上限 64。
		关闭（0）时矩阵与引入前逐字节一致。
		*/
		void shadow_texel_max_set(float texel);
		//当前阴影纹素尺寸上限（未初始化为 0）
		float shadow_texel_max(void) const;
		/*
		级联切换带自动淡化（第六百六十一轮）
		开启且显式淡化比例为 0、级联数 ≥ 2 时，按「4 / 分片边长」给出建议比例（钳到 [0.005, 0.1]）；
		关闭（默认）时完全沿用显式比例，画面逐字节不变。
		*/
		void shadow_cascade_blend_auto_set(bool enabled);
		//当前是否开启级联切换带自动淡化（未初始化为假）
		bool shadow_cascade_blend_auto(void) const;
		/*
		阴影批量提交（第六百六十九轮；C2）
		开启且本帧阴影段数 > 1 时，级联片与聚光瓦片在同一命令提交内完成；
		关闭（默认）时逐段提交，画面逐字节一致。
		*/
		void shadow_batch_submit_set(bool enabled);
		//当前是否开启阴影批量提交（未初始化为假）
		bool shadow_batch_submit(void) const;
		/*
		聚光灯阴影偏置自校准（第五百二十一轮新增）
		true：逐瓦片有效偏置 = spot_shadow_bias × (9 / 该瓦片跨度)，使不同半径的世界法线偏移接近一致；
		false（默认）：逐瓦片偏置恒等于 spot_shadow_bias，画面与引入前逐字节一致。
		*/
		void spot_shadow_bias_auto_set(bool enabled);
		//当前是否开启偏置自校准（未初始化时为默认假）
		bool spot_shadow_bias_auto(void) const;
		//最近一帧逐瓦片有效偏置（8 项；诊断用；未初始化时写 0.010）
		void spot_shadow_bias_tiles_get(float out[8]) const;
		/*
		阴影图分辨率（第五十四轮新增）
		仅支持 512 / 1024 / 2048（默认 1024）；切换会等尽在途帧、重建阴影目标并重新绑定各帧槽，
		非法值整次忽略；降低分辨率可减少阴影 pass 的填充开销。
		*/
		void shadow_resolution_set(uint32_t resolution);
		//当前阴影图分辨率（未初始化时为默认 1024）
		uint32_t shadow_resolution(void) const;
		/*
		阴影光源正交视锥范围（第一百零七轮新增）
		half_extent 为半宽 / 半高（世界单位，默认 12）；near / far 为光源空间近远平面（默认 1 / 60）。
		三者共同决定阴影覆盖范围与深度精度；非法（半宽 ≤ 0、near ≤ 0、far ≤ near）整次忽略并告警。
		*/
		void shadow_range_set(float half_extent, float near_plane, float far_plane);
		//当前阴影正交范围（未初始化时写默认 12 / 1 / 60）
		void shadow_range_get(float* half_extent_out, float* near_out, float* far_out) const;
		/*
		级联阴影（第一百二十一轮新增；第一百四十六轮扩为三级）
		count 为级联数（接受 1 / 2 / 3，默认 1）；far_scale 为远区半径相对近区的倍数（> 1，默认 3）；
		split_distance 为按「与相机的世界距离」切换首个级联的阈值（> 0，默认 12）。
		单级时行为与引入级联前逐字节一致；多级时阴影图沿 X 切成同级数的方形小片，
		片元按距离在 split_distance 与 split_distance × far_scale 处选择级联，
		故片内分辨率随级数下降（级联的固有代价）。
		任一参数非法整次忽略并告警；生效后旧阴影图不再可信，下一帧重绘。
		*/
		void shadow_cascade_set(uint32_t count, float far_scale, float split_distance);
		//当前级联配置（未初始化时写默认 1 / 3 / 12）
		void shadow_cascade_get(uint32_t* count_out, float* far_scale_out, float* split_distance_out) const;
		/*
		级联接缝淡化（第一百四十八轮新增）
		blend 为切换阈值两侧的淡化带宽比例（0..0.5；0 = 硬切换，默认）。
		带宽 = 阈值 × blend：片元落在带内时对相邻两片阴影因子做平滑混合，消除分辨率突变接缝。
		越界 / 非数整次忽略并告警；不影响阴影图内容（无需重绘）。
		*/
		void shadow_cascade_blend_set(float blend);
		//当前级联接缝淡化比例（未初始化时为默认 0）
		float shadow_cascade_blend(void) const;
		/*
		累计实际执行的阴影 pass 次数（第五十五轮新增）
		阴影 pass 采用「输入签名不变则复用上一帧阴影图」的按需重绘策略；
		本查询供上层观测该策略的命中情况（静态场景下应显著低于帧数）。
		*/
		uint64_t shadow_pass_count(void) const;
	};
}
