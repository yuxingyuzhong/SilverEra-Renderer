#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
/*
各模块对外头
本头属本层「实现细节」：实现体持有各模块实例，故需其完整类型；
不进公共头，故可以合法暴露 Vulkan / SDL3 类型（层外仍看不到）。
*/
#include "Application/Renderer/src/render/Platform/平台上下文.h"
#include "Application/Renderer/src/render/Device/设备上下文.h"
#include "Application/Renderer/src/render/Memory/内存上下文.h"
#include "Application/Renderer/src/render/Swapchain/交换链.h"
#include "Application/Renderer/src/render/Frame/帧上下文.h"
#include "Application/Renderer/src/render/Shader/着色器.h"
#include "Application/Renderer/src/render/Pipeline/管线.h"
#include "Application/Renderer/src/render/Mesh/网格.h"
#include "Application/Renderer/src/render/Texture/纹理.h"
#include "Application/Renderer/src/render/Scene/相机.h"
//渲染目标模块（离屏渲染目标：颜色 + 深度 + 视图）
#include "Application/Renderer/src/render/RenderTarget/渲染目标.h"
//字体模块（TTF → 字形图集；文字绘制取用）
#include "Application/Renderer/src/render/Text/字体.h"
//文字绘制子系统（网格缓存 / 文字颜色 / 管线句柄；第四十七轮下沉）
#include "Application/Renderer/src/render/Text/文字渲染.h"
//门面声明（Renderer::Impl 为其私有嵌套类）
#include "Application/Renderer/src/render/Renderer/渲染器.h"
//UI 通道（实现体持有其实例，供上层接入 ImGui Vulkan 后端）
#include "Application/Renderer/src/render/Renderer/渲染器_UI通道.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器实现体
	持有各模块的实例并按依赖顺序编排创建与销毁；本类只被 渲染器.cpp 使用。
	*/
	class Renderer::Impl
	{
	public:
		/*
		重建交换链并把旧资源压入帧延迟销毁队列
		无整帧停顿：旧交换链及其附属资源（图像视图 / 两类信号量 / 深度图像与内存与视图）被移交延迟队列，
		延迟 = 在途帧数 + 1，保证在途帧用尽后才释放（§6.4）。返回重建是否成功。
		*/
		bool swapchain_rebuild_deferred(uint32_t width, uint32_t height);
		/*
		登记具名图形管线（第一百七十二轮抽出；门面 pipeline_load 与材质共享管线共用）
		加载着色器对并登记一条具名管线；cull_none 为真时关闭背面剔除（双面渲染）。
		成功时写出句柄并把着色器模块所有权记入门面；失败时回收已加载模块并返回假。
		*/
		bool pipeline_load_ex(uint64_t& handle_out, const std::string& vert_shader,
			const std::string& frag_shader, Blend_Mode blend_mode, bool alpha_to_coverage, bool cull_none,
			bool depth_write = true);
		/*
		确保后处理全屏四边形网格就绪（惰性创建；单级路径与链路径共用）
		全屏四边形（NDC -1..1）随 mesh 模块销毁。
		*/
		bool post_mesh_ensure(void);
		/*
		确保内置单级后处理具名管线就绪（惰性登记；仅既有单级默认路径使用）
		着色器对为 后处理.vert / 后处理.frag（不透明档）；着色器模块由本实现体持有，随 extra_pipelines 统一销毁。
		*/
		bool post_pipeline_ensure(void);
		//确保既有单级默认后处理所需资源就绪（默认后处理管线 ＋ 全屏网格）
		bool post_resources_ensure(void);
		/*
		把后处理采样集绑定到给定目标视图组（第九十七轮）
		签名未变时为空操作（避免逐帧更新在途描述符集）；变化时先等尽在途帧再写满全部帧槽。
		绑定规则：index i 的 binding 0 = views[i]、binding 1 = views[0]（场景目标）。
		*/
		bool post_sets_bind(const VkImageView* views, uint32_t count, VkSampler sampler,
			VkImageView depth_view = VK_NULL_HANDLE);
		/*
		确保 SSAA 降采样具名管线就绪（惰性登记）
		着色器对为 后处理.vert / 降采样.frag（不透明档）；模块由本实现体持有，随 extra_pipelines 统一销毁。
		*/
		bool ssaa_pipeline_ensure(void);
		/*
		确保天空盒具名管线就绪（惰性登记；第一百二十五轮）
		着色器对为 天空盒.vert / 天空盒.frag；采样数跟随主场景配置，深度只读 ＋ LESS_OR_EQUAL，
		使全屏天空项只覆盖深度仍为清屏值的背景像素。
		*/
		bool sky_pipeline_ensure(void);
		/*
		确保 SSAA 超采样目标就绪（场景目标 = 输出 × 倍率；解析目标 = 输出尺寸）
		倍率或输出尺寸变化时先等尽在途帧、销毁旧目标再重建；创建失败置 ssaa_extent_failed 并由调用方回落。
		*/
		bool ssaa_targets_ensure(uint32_t width, uint32_t height, uint32_t factor);
		//销毁 SSAA 超采样目标（关闭时调用；句柄清零）
		void ssaa_targets_destroy(void);
		//配置（init 成功后生效）
		Renderer_Config config;
		//清屏色（RGBA；由 scene_apply / clear_color_set 设置，frame_render 每帧取用）
		float clear_color[4] = { 0.05f, 0.05f, 0.10f, 1.0f };
		/*
		光照参数（第二十九轮方向光 / 第三十一轮点光源与高光 / 第三十二轮多光源；随场景 UBO 逐帧下发）
		默认值保证「未提供法线的网格」画面与旧版完全一致：
		方向 (0,0,-1)（朝 -Z 传播）、颜色白、强度 1、环境光 0、无点光源 → 照度为 1。
		*/
		float light_direction[3] = { 0.0f, 0.0f, -1.0f };
		float light_color[3] = { 1.0f, 1.0f, 1.0f };
		float light_intensity = 1.0f;
		float light_ambient = 0.0f;
		//环境光颜色（第四十轮；默认白，与引入该参数前一致）
		float ambient_color[3] = { 1.0f, 1.0f, 1.0f };
		//半球环境光（第四十七轮）：rgb = 地面颜色、w = 混合强度（0 = 关闭，只用 ambient_color）
		float ambient_ground[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
		//补光（第四十七轮）：第二盏方向光；强度 0 = 关闭
		float fill_direction[3] = { 0.0f, 0.0f, -1.0f };
		float fill_color[3] = { 1.0f, 1.0f, 1.0f };
		float fill_intensity = 0.0f;
		//方向光 / 补光的逐光高光缩放（第五十轮；1 = 跟随全局，0 = 不产生高光）
		float directional_specular = 1.0f;
		float fill_specular = 1.0f;
		//镜面高光（第三十一轮；对全部光源生效）
		float specular_intensity = 0.0f;
		float specular_shininess = 16.0f;
		/*
		距离衰减模式（第三十六轮；对点光源与聚光灯共同生效）
		0 = 线性（clamp(1 − d / radius)，默认，与引入该开关前一致）；1 = 平方反比（1 / (1 + (d / radius)²)）。
		随场景 UBO 的 `light_color.w` 逐帧下发。
		*/
		uint32_t attenuation_mode = 0;
		/*
		距离雾（第三十八轮）
		密度为 0 表示关闭（默认，画面与引入雾前一致）；开启后按「相机 → 片元」距离在起始 / 结束距离间平滑混入雾色。
		*/
		float fog_color[3] = { 0.05f, 0.05f, 0.10f };
		float fog_density = 0.0f;
		float fog_start = 1.0f;
		float fog_end = 10.0f;
		//雾模式（第一百四十三轮：0 = 线性区间、1 = 指数平方；写入 fog_range.z）
		uint32_t fog_mode = 0;
		//雾高度衰减系数（第一百四十三轮；写入 fog_range.w；0 = 关闭）
		float fog_height_falloff = 0.0f;
		/*
		点光源数组（第三十二轮；最多 POINT_LIGHT_MAX 个，按登记顺序累加）
		每个点光源：位置 ＋ 衰减半径；颜色 ＋ 强度。
		*/
		struct Point_Light
		{
			//位置（世界坐标）
			float position[3] = { 0.0f, 0.0f, 0.0f };
			//线性衰减半径（> 0）
			float radius = 5.0f;
			//颜色（RGB，0..1）
			float color[3] = { 1.0f, 1.0f, 1.0f };
			//强度（≥ 0）
			float intensity = 1.0f;
			//逐光源衰减模式（0 = 跟随全局、1 = 线性、2 = 平方反比；第四十六轮）
			uint32_t attenuation = 0;
			//逐盏高光缩放（第四十八轮；1 = 跟随全局，0 = 该灯不产生高光）
			float specular_scale = 1.0f;
			//逐盏高光光泽度（第五十一轮；0 = 跟随全局）
			float shininess = 0.0f;
			/*
			聚光灯附加参数（第三十四轮）
			普通点光源的 is_spot 恒假；聚光灯在此基础上额外乘锥形衰减：
			cone = smoothstep(spot_outer_cos, spot_inner_cos, dot(L, spot_direction))。
			*/
			//是否聚光灯
			bool is_spot = false;
			//光传播方向（单位向量；仅 is_spot 为真时有效）
			float spot_direction[3] = { 0.0f, 0.0f, -1.0f };
			//内锥角余弦（cos(内角)；锥内为 1）
			float spot_inner_cos = 1.0f;
			//外锥角余弦（cos(外角)；锥外为 0）
			float spot_outer_cos = 0.0f;
		};
		//点光源数量上限（与着色器循环上限一致；第六十二轮由 4 扩为 8）
		static constexpr uint32_t POINT_LIGHT_MAX = 8;
		//点光源数组与当前数量
		Point_Light point_lights[POINT_LIGHT_MAX];
		uint32_t point_light_count = 0;
		//平台模块
		Platform_Context platform;
		//设备模块
		Device_Context device;
		//内存模块
		Memory_Context memory;
		//交换链模块
		Swapchain_Context swapchain;
		//帧模块
		Frame_Context frame;
		//着色器模块
		Shader_Context shader;
		//具名管线记录（句柄 → 顶点 / 片元着色器模块；着色器模块所有权在门面，随卸载 / 关闭销毁）
		struct Extra_Pipeline_Record
		{
			//句柄（≥ 1）
			uint64_t handle = 0;
			//顶点着色器模块
			VkShaderModule vert_module = VK_NULL_HANDLE;
			//片元着色器模块
			VkShaderModule frag_module = VK_NULL_HANDLE;
		};
		//管线模块
		Pipeline_Context pipeline;
		//具名管线集合（默认场景管线不在内）
		std::vector<Extra_Pipeline_Record> extra_pipelines;
		//材质记录（管线 ＋ 纹理 ＋ 逐物体 PBR；有效标记用于句柄槽复用）
		struct Material_Record
		{
			//图形管线句柄（0 = 默认场景管线）
			uint64_t pipeline = 0;
			//纹理句柄（0 = 内置默认白纹理）
			uint64_t texture = 0;
			//是否覆盖全局 PBR 参数（第六十八轮；false 时沿用场景 UBO 的 pbr_params）
			bool pbr_override = false;
			//逐物体材质粗糙度缩放（0.05..4；仅 pbr_override 为真时生效）
			float pbr_roughness = 1.0f;
			//逐物体材质金属度（0..1；仅 pbr_override 为真时生效）
			float pbr_metallic = 0.0f;
			//是否覆盖反照率色调（第六十九轮；false 时用白色）
			bool tint_override = false;
			//逐物体反照率色调（RGBA，各分量 0..1；仅 tint_override 为真时生效）
			float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			//逐物体自发光强度（第七十轮；0 = 无自发光）
			float emissive = 0.0f;
			//法线贴图句柄（第一百二十四轮；0 = 无）
			uint64_t normal_texture = 0;
			//粗糙度贴图句柄（第一百二十四轮；0 = 无）
			uint64_t roughness_texture = 0;
			//法线强度（第一百二十四轮；0 = 不启用）
			float normal_strength = 0.0f;
			//自发光颜色贴图句柄（第一百六十八轮；0 = 无）
			uint64_t emissive_texture = 0;
			//环境光遮蔽贴图句柄（第一百六十九轮；0 = 无）
			uint64_t ao_texture = 0;
			//金属度贴图句柄（第一百六十九轮；0 = 无）
			uint64_t metallic_texture = 0;
			//贴图 UV 缩放（第一百七十一轮；默认 (1,1)）
			float uv_scale[2] = { 1.0f, 1.0f };
			//贴图 UV 偏移（第一百七十一轮；默认 (0,0)）
			float uv_offset[2] = { 0.0f, 0.0f };
			//覆盖率写入（第一百二十六轮；仅 MSAA > 1× 时生效）
			bool alpha_to_coverage = false;
			//双面渲染（第一百七十二轮；true 时关闭背面剔除）
			bool double_sided = false;
			//深度写入（第一百七十三轮；false 时不写深度）
			bool depth_write = true;
			//记录是否有效
			bool valid = false;
		};
		//材质集合（下标 = 句柄 - 1）
		std::vector<Material_Record> materials;
		/*
		按「混合档 × 覆盖率写入 × 双面渲染 × 深度写入」组合缓存的具名管线（第一百七十三轮扩为单表八组合）
		下标 = 档位序 × 8 + 组合位（bit2 = 关闭深度写入、bit1 = 覆盖率写入、bit0 = 双面渲染），0 表该组合尚未登记；
		材质共享同组合的具名管线（以默认场景着色器对登记），故同组合多个材质不再各自登记；
		管线所有权归门面（随 shutdown 的 extra_pipelines 统一释放），材质卸载**不**释放它。
		*/
		std::vector<uint64_t> material_pipelines;
		//当前绘制所选管线句柄（0 = 默认场景管线）
		uint64_t bound_pipeline_request = 0;
		//网格模块
		Mesh_Context mesh;
		//纹理模块
		Texture_Context texture;
		//相机（生成每帧视图 × 投影矩阵）
		Camera camera;
		//UI 通道（供上层接入 ImGui Vulkan 后端；不代管 UI）
		Ui_Channel_Context ui_channel;
		//渲染目标模块（离屏渲染目标：颜色 + 深度 + 视图）
		Render_Target_Context render_target;
		//阴影图默认边长（第五十二轮；固定默认 1024，兼顾精度与开销）
		static constexpr uint32_t SHADOW_SIZE = 1024;
		//阴影图实际边长（第五十四轮；可配 512 / 1024 / 2048）
		uint32_t shadow_size = SHADOW_SIZE;
		//上一帧阴影输入签名（第五十五轮；未就绪时为 0）
		uint64_t shadow_signature = 0;
		//上一帧阴影图是否可用（第五十五轮；重建目标 / 关闭时置假）
		bool shadow_pass_ready = false;
		//累计实际执行的阴影 pass 次数（第五十五轮；观测与测试）
		uint64_t shadow_pass_count = 0;
		//累计阴影重绘次数（第一百四十五轮）
		uint64_t shadow_redraws = 0;
		//累计阴影图复用次数（签名一致跳过重绘；第一百四十五轮）
		uint64_t shadow_reuses = 0;
		//最近一次阴影重绘的绘制项数（第一百四十五轮）
		uint32_t last_shadow_items = 0;
		//最近一帧是否重绘了阴影图（1 = 重绘、0 = 复用或未执行；第一百四十五轮）
		uint32_t shadow_last_redrawn = 0;
		//阴影管线句柄（第五十二轮；0 = 未登记）
		uint64_t shadow_pipeline = 0;
		//阴影目标句柄（第五十二轮；0 = 未创建）
		uint64_t shadow_target = 0;
		//阴影是否启用（第五十二轮；默认关闭，保持既有画面逐字节不变）
		bool shadow_enabled = false;
		/*
		阴影光源正交视锥（第一百零七轮）
		默认值与引入该参数前一致：半宽 / 半高 12、近 1、远 60。
		*/
		float shadow_half_extent = 12.0f;
		float shadow_near = 1.0f;
		float shadow_far = 60.0f;
		/*
		级联阴影（第一百二十一轮；第一百四十六轮扩为三级）
		cascade_count 为 1 时沿用上面单级口径（画面逐字节不变）；为 2 / 3 时阴影图按象限切 2 / 3 片：
		各片覆盖以相机为中心、半径 split × far_scale^(片序) 的包围球，
		片元按「与相机的距离」在 split、split × far_scale 处选择级联。
		每片占 shadow_size / 级数 边长，故片内分辨率随级数下降（级联的固有代价）。
		*/
		uint32_t cascade_count = 1;
		float cascade_far_scale = 3.0f;
		float cascade_split_distance = 12.0f;
		//级联接缝淡化带宽比例（第一百四十八轮；0 = 硬切换，0.5 = 阈值两侧各半）
		float cascade_blend = 0.0f;
		//远区光源空间视图投影矩阵（cascade_count >= 2 时有效；每帧更新）
		float light_view_proj_far[16] = {};
		//第三级光源空间视图投影矩阵（cascade_count == 3 时有效；每帧更新）
		float light_view_proj_far2[16] = {};
		/*
		各片光源空间深度跨度（近平面到远平面；第一百二十一轮）
		片元着色器的法线偏移按「偏置 × 深度跨度」换算到世界量级，逐片跨度不同，故逐片记录。
		单级时恒为 59（与引入级联前的硬编码口径一致）。
		*/
		float cascade_depth_span[3] = { 59.0f, 59.0f, 59.0f };
		//阴影深度偏置（第五十二轮）
		float shadow_bias = 0.003f;
		//阴影软度（第五十三轮；0 = 单点硬边，≥0.5 = 3×3 PCF 平均）
		float shadow_softness = 0.0f;
		//阴影强度（第五十三轮；0 = 阴影无效、1 = 全遮蔽）
		float shadow_strength = 1.0f;
		/*
		PBR-lite 镜面开关与粗糙度缩放（第六十四轮）
		关闭时完全沿用 Blinn-Phong（画面逐字节不变）；粗糙度缩放默认 1（合法 0.05..4）。
		*/
		bool pbr_enabled = false;
		float pbr_roughness = 1.0f;
		//PBR 金属度（第六十五轮；0 = 介电、1 = 纯金属；默认 0）
		float pbr_metallic = 0.0f;
		//光源空间视图投影矩阵（第五十二轮；每帧按方向光计算，同时写入 UBO 与阴影绘制推送常量）
		float light_view_proj[16] = {};
		//是否正处于文字绘制回调内（第五十二轮；文字网格不参与阴影投影）
		bool in_text_draw = false;
		//本帧渲染目标句柄（0 = 直接渲染到交换链）
		uint64_t active_target = 0;
		//本帧「渲染到目标并同帧后处理呈现」的目标句柄（0 = 关闭；见 render_target_present_set）
		uint64_t present_target = 0;
		/*
		本帧「同帧后处理链」的额外中间目标句柄（下标 1..N-1；空 = 只有 present_target 一个目标）
		由 render_target_present_chain_set 设置；链上第 i 级取 targets[i % 目标数]，故链长 ≥ 2 时至少需 2 个目标。
		*/
		std::vector<uint64_t> present_extra;
		//同帧后处理呈现的目标数上限（= 每帧槽后处理采样集数量，超出即整组拒绝）
		static constexpr uint32_t PRESENT_TARGET_MAX = Pipeline_Context::POST_SET_PER_SLOT;
		/*
		本帧后处理链（各级具名管线句柄，下标即级序；空 = 走既有单级默认后处理路径）
		由 post_chain_set 设置；各级使用同一全屏几何与场景 UBO，仅管线与源采样集不同。
		级数上限见 POST_CHAIN_MAX_STAGES。
		*/
		std::vector<uint64_t> post_chain;
		//后处理链级数上限（超出即整链拒绝；每级占用一次全屏 pass，超长链会带来隐性性能坍塌）
		static constexpr uint32_t POST_CHAIN_MAX_STAGES = 8;
		//后处理管线句柄（0 = 尚未登记；首次使用时懒登记，不透明档；仅用于既有单级默认路径）
		uint64_t post_pipeline = 0;
		//全屏四边形网格句柄（0 = 尚未创建；后处理 pass 的几何）
		Mesh_Handle fullscreen_mesh = 0;
		/*
		SSAA 超采样状态（倍率钳制 1..4；1 = 关闭）
		scene_target 为输出 × 倍率的场景目标，resolve_target 为输出尺寸的解析目标；
		extent_* / target_factor 记录建立时的参数，变化即重建；extent_failed 避免逐帧重复失败尝试。
		*/
		uint32_t ssaa_factor = 1;
		uint64_t ssaa_scene_target = 0;
		uint64_t ssaa_resolve_target = 0;
		uint32_t ssaa_extent_width = 0;
		uint32_t ssaa_extent_height = 0;
		uint32_t ssaa_target_factor = 0;
		//SSAA 降采样具名管线句柄（0 = 尚未登记）
		uint64_t ssaa_pipeline = 0;
		/*
		天空盒 / 简易 IBL（第一百二十五轮）
		默认关闭，故未启用时画面与引入前逐字节一致；启用后场景片元按法线 / 反射方向采样立方体贴图作补光，
		并由全屏天空项绘制背景。sky_texture 为用户设置的立方体贴图句柄（0 = 内置纯白立方体）。
		*/
		bool sky_enabled = false;
		uint64_t sky_texture = 0;
		uint64_t default_sky_texture = 0;
		uint64_t sky_pipeline = 0;
		//漫反射（环境补光）强度
		float sky_ambient = 0.0f;
		//镜面强度
		float sky_specular = 0.0f;
		//当前 MSAA 采样数（第九十六轮；1 = 关闭）
		uint32_t msaa_samples = 1;
		//场景顶点着色器声明的顶点输入位置位集（第九十七轮；0 = 未解析 → 按全量处理）
		uint32_t scene_input_locations = 0;
		/*
		后处理采样集绑定签名（第九十七轮）
		记录上次写入的目标视图组与采样器；未变化则跳过逐帧重写，避免更新在途描述符集。
		*/
		VkImageView post_bound_views[PRESENT_TARGET_MAX] = {};
		uint32_t post_bound_view_count = 0;
		VkSampler post_bound_sampler = VK_NULL_HANDLE;
		//后处理采样集 binding 2 已绑定的场景深度视图（第一百二十三轮）
		VkImageView post_bound_depth = VK_NULL_HANDLE;
		bool post_bind_cached = false;
		//当前尺寸 / 倍率下超采样目标创建失败（避免逐帧重复尝试）
		bool ssaa_extent_failed = false;
		//字体模块（TTF → 字形图集）
		Font_Context font;
		/*
		文字绘制子系统（第四十七轮下沉）
		持有文字管线句柄、文字颜色与「(字体, 文本, 布局) → 网格」缓存；
		字形补烘 / 管线登记 / 纹理绑定 / 网格创建 / 绘制登记经宿主回调转交本实现体。
		*/
		Text_Renderer text;
		/*
		泛光参数（阈值 / 软膝 / 强度 / 保留；默认 0.6 / 0.1 / 1.0 / 0）
		由 bloom_param_set 更新；后处理链各级以推送常量（offset 96）下发。
		*/
		float bloom_param[4] = { 0.6f, 0.1f, 1.0f, 0.0f };
		/*
		逐级泛光参数覆盖（第二十三轮）
		post_stage_param[stage] 有效时覆盖全局 bloom_param（x = 阈值、y = 软膝、z = 强度、w 保留）；
		由 post_stage_param_set 设置，post_chain_set 时整体清除（链一变，旧下标即失去意义）。
		*/
		float post_stage_param[POST_CHAIN_MAX_STAGES][4] = {};
		bool post_stage_param_valid[POST_CHAIN_MAX_STAGES] = {};
		//累计因视锥剔除而未登记的绘制项数（第二十三轮；仅供 frame_stats_get 观测）
		uint64_t culled_items = 0;
		/*
		最近一帧的即时观测（第一百三十二轮）
		frame_culled_items 为「本帧已登记阶段」累计、在 frame_render 收尾时转存到 last_culled_items 并清零；
		last_draw_items / last_instances 在合批之后、清空待绘制项之前取用（故与累计口径一致）。
		*/
		uint64_t frame_culled_items = 0;
		/*
		实例级剔除（第一百五十三轮）
		culled_instances 为自 init 以来因视锥剔除而未写入实例缓冲的实例数；
		frame_culled_instances 为当前帧累计，帧收尾转存 last_culled_instances 后清零；
		instanced_cull_scratch 为筛选可见实例的复用缓冲（避免每次调用分配）。
		*/
		uint64_t culled_instances = 0;
		uint64_t frame_culled_instances = 0;
		uint64_t last_culled_instances = 0;
		std::vector<float> instanced_cull_scratch;
		uint64_t last_culled_items = 0;
		uint64_t last_draw_items = 0;
		uint64_t last_instances = 0;
		/*
		合批收益观测（第一百三十五轮）
		frame_merged_items 为本帧（主 pass ＋ 阴影各片）被合并掉的项数，帧收尾转存 last_merged_items 并清零；
		merged_items 为自 init 以来累计被合并项数。
		*/
		uint64_t frame_merged_items = 0;
		uint64_t last_merged_items = 0;
		uint64_t merged_items = 0;
		//最近一次阴影 pass 的 GPU 耗时（毫秒；第一百二十八轮；未启用 / 无能力时 0）
		double shadow_gpu_ms = 0.0;
		//是否启用视锥剔除（第二十五轮；默认开启；关闭时全部绘制项照常登记）
		bool cull_enabled = true;
		//是否启用静态合批（第一百二十七轮；默认开启）
		bool static_batching = true;
		/*
		文字描边参数（第一百四十轮；经推送常量 offset 80 下发）
		text_outline_uv_width = 描边半宽（图集 UV；0 = 关闭），text_outline_color = 描边色（0..1）。
		*/
		float text_outline_uv_width = 0.0f;
		float text_outline_color[3] = { 0.0f, 0.0f, 0.0f };
		//本帧推送常量存储（每项 28 浮点 = mat4 矩阵 ＋ 色调 ＋ 材质贴图参数 ＋ UV 变换；deque 保证元素引用稳定，绘制项持其指针）
		std::deque<std::array<float, 28>> pending_push_constants;
		//场景 UBO 缓冲（长度 = 在途帧数，主机可见，逐帧写入相机矩阵）
		std::vector<VkBuffer> scene_ubos;
		//场景 UBO 内存（与 scene_ubos 一一对应）
		std::vector<VmaAllocation> scene_ubo_allocs;
		/*
		场景 UBO 上传缓存（第六十轮）
		逐帧槽保存上一次已写入的 160 浮点（每个槽一段），用于按变化区间写入；
		scene_block_cached 标记该槽缓存是否已建立（首次访问须整块写入）。
		*/
		std::vector<float> scene_block_cache;
		std::vector<uint8_t> scene_block_cached;
		//累计场景 UBO 上传字节数（第六十轮；仅供 scene_upload_bytes 观测，不随帧重置）
		uint64_t scene_upload_bytes = 0;
		/*
		实例缓冲上传缓存（第六十一轮）
		逐帧槽保存上次写入的实例属性（帧槽数 × instance_capacity × 20 浮点）与已写入实例数；
		instance_block_cached 标记该槽缓存是否已建立（首次访问须整块写入）。
		*/
		std::vector<float> instance_block_cache;
		std::vector<uint32_t> instance_block_count;
		std::vector<uint8_t> instance_block_cached;
		//累计实例缓冲上传字节数（第六十一轮；仅供 instance_upload_bytes 观测，不随帧重置）
		uint64_t instance_upload_bytes = 0;
		//实例缓冲（长度 = 在途帧数，主机可见，逐帧写入本帧全部实例模型矩阵）
		std::vector<VkBuffer> instance_buffers;
		//实例缓冲内存（与 instance_buffers 一一对应）
		std::vector<VmaAllocation> instance_allocations;
		//每帧槽实例缓冲容量（实例个数）
		uint32_t instance_capacity = 256;
		//内置默认白纹理句柄
		uint64_t default_texture = 0;
		/*
		当前绑定视图（写入各帧槽 set1 的采样源；持裸句柄）
		纹理与渲染目标共用同一绑定语义：无论来源是纹理还是渲染目标，均以「实际绑定的图像视图」记录，
		故采样器状态切换后可直接以该视图重写 set1（处置 R-FACADE-06：不再按句柄回落后覆盖目标绑定）。
		*/
		VkImageView bound_view = VK_NULL_HANDLE;
		//当前绑定采样器（逐绘制项采样集写用；第一百七十四轮）
		VkSampler bound_sampler = VK_NULL_HANDLE;
		/*
		当前材质贴图视图（第一百七十四轮）
		依次为 法线 / 粗糙度 / 自发光 / 环境光遮蔽 / 金属度；无贴图时为内置默认白纹理视图。
		*/
		VkImageView material_views[5] = {};
		//当前 3D LUT 纹理句柄（第一百四十四轮；0 = 未绑定）
		uint64_t lut_texture = 0;
		//当前混合档（与管线模块内记录保持一致；交换链重建时随重建传入）
		Blend_Mode blend_mode = Blend_Mode::Opaque;
		/*
		逐物体 PBR 材质吸附状态（第六十八轮）
		material_pbr_set 之后，后续 mesh_draw / mesh_draw_instanced 的实例属性携带该材质；
		material_pbr_clear（或 material_bind_set 绑定无覆盖的材质）回落到场景 UBO 的全局 pbr_params。
		*/
		bool material_pbr_active = false;
		//逐物体材质粗糙度缩放（0.05..4）
		float material_pbr_roughness = 1.0f;
		//逐物体材质金属度（0..1）
		float material_pbr_metallic = 0.0f;
		//二次元风格化：色阶数（第一百七十四轮；0 = 关闭，1..8 = 量化档数）
		uint32_t toon_steps = 0;
		//二次元风格化：边缘光强度（第一百七十四轮；0 = 关闭，0..1）
		float toon_rim = 0.0f;
		/*
		逐物体反照率色调吸附状态（第六十九轮）
		material_tint_set 之后，后续非文字网格绘制在推送常量中携带该色调（albedo × tint）；
		material_tint_clear（或 material_bind_set 绑定无色调的材质）回落白色（画面不变）。
		mesh_draw 显式给出的 color 形参优先于该吸附状态。
		*/
		bool material_tint_active = false;
		//逐物体反照率色调（RGBA，各分量 0..1）
		float material_tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		//逐物体自发光强度（第七十轮；0 = 无自发光，恒生效，无需覆盖标志）
		float material_emissive = 0.0f;
		//逐物体法线强度（第一百二十四轮；0 = 不启用，随推送常量 offset 80 的 x 分量下发）
		float material_normal_strength = 0.0f;
		//逐物体贴图 UV 缩放（第一百七十一轮；默认 (1,1)，随推送常量 offset 96 的 xy 下发）
		float material_uv_scale[2] = { 1.0f, 1.0f };
		//逐物体贴图 UV 偏移（第一百七十一轮；默认 (0,0)，随推送常量 offset 96 的 zw 下发）
		float material_uv_offset[2] = { 0.0f, 0.0f };
		//顶点着色器模块句柄（销毁须先于着色器模块）
		VkShaderModule vert_module = VK_NULL_HANDLE;
		//片元着色器模块句柄（销毁须先于着色器模块）
		VkShaderModule frag_module = VK_NULL_HANDLE;
		//待绘制项（由 mesh_draw 逐次登记，frame_render 消费后清空；同帧可登记多个网格）
		std::vector<Frame_Render_Item> pending_items;
		//合批复用缓冲（第一百三十六轮；避免每帧为合并结果分配新向量）
		std::vector<Frame_Render_Item> batch_scratch;
		//本帧待绘制的实例属性（列主序 16 浮点模型矩阵 ＋ 4 浮点材质 × 实例数；按登记顺序连续存放，一次写入帧槽实例缓冲）
		std::vector<std::array<float, INSTANCE_FLOAT_COUNT>> pending_instances;
		//是否已初始化
		bool initialized = false;
		//已呈现帧数
		uint64_t frame_count = 0;
	};
}
