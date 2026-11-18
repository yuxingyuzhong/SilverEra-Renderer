#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//光源数据记录（Vulkan-free；第二百六十五轮）
#include "Engine/EngineRenderer/src/render/Scene/光源数据.h"
//分簇数据与 CPU 逐簇计数参考（Vulkan-free；第二百六十七轮）
#include "Engine/EngineRenderer/src/render/Cluster/簇划分.h"
//粒子数据（Vulkan-free；第三百零七轮；位于 EngineSystem，模拟与绘制两侧共享同一份布局）
#include "Engine/EngineSystem/src/world/data/particle/Particle_Data/粒子数据.h"
/*
网格数据（Vulkan-free）
公共面只暴露引擎语义类型（Vertex / Mesh_Data / Mesh_Handle），
其定义置于 Mesh 模块的共用头，层外看不到任何 Vulkan / VMA / SDL3 / glm 类型。
*/
#include "Engine/EngineRenderer/src/render/Mesh/网格数据.h"
//管线公共语义类型（Vulkan-free）：混合档
#include "Engine/EngineRenderer/src/render/Pipeline/管线数据.h"
//纹理公共语义类型（Vulkan-free）：采样器状态
#include "Engine/EngineRenderer/src/render/Texture/纹理数据.h"
//材质公共语义类型（Vulkan-free）：材质描述与句柄
#include "Engine/EngineRenderer/src/render/Pipeline/材质数据.h"
//场景公共语义类型（Vulkan-free）：场景描述
#include "Engine/EngineRenderer/src/render/Scene/场景数据.h"
//文字布局公共语义类型（Vulkan-free）：对齐方式与换行宽度
#include "Engine/EngineRenderer/src/render/Text/文字布局数据.h"
//计算公共语义类型（Vulkan-free）：句柄与字节上限（第二百四十三轮）
#include "Engine/EngineRenderer/src/render/Compute/计算数据.h"

//游戏引擎命名空间
namespace engine
{
	//UI 通道（扩展公共头 渲染器_UI通道.h 定义；此处仅前置声明，保持本公共头零 Vulkan / SDL 类型）
	class Ui_Channel_Context;
	//原生 UI 上下文（完整定义见 用户界面.h，SDL / Vulkan-free）
	class Ui_Context;
	//帧输入聚合器（完整定义见公共扩展头 输入聚合.h，含 SDL3 类型）
	class Input_Aggregator;
	//事件终端（完整定义见 EngineFoundation 的事件终端头）
	class Event_Terminal;

	//渲染器与核心模块窄上下文前置声明
	class Renderer;
	struct Render_Core_Context;

	/*
	层内 GPU 粒子推进（模拟语义与计算原语同归本层）
	实现位于 src/render/Particle/粒子GPU推进.cpp，经六域公共接口协作。
	*/
	namespace particle_gpu
	{
		//GPU 发射（常驻模式）：返回实际写入的粒子数
		uint32_t emit(Renderer& renderer, uint32_t count);
		//常驻推进：仅调度（读 front / 写 back 并交换）
		bool step_resident(Renderer& renderer, float dt);
	}

	//渲染层版本查询（d34da1c 已存在，保留）
	std::string renderer_version(void);

	//渲染层配置
	struct Renderer_Config
	{
		//窗口客户区宽度（像素）
		uint32_t width = 1280;
		//窗口客户区高度（像素）
		uint32_t height = 720;
		//窗口标题
		std::string title = "白银纪元";
		//在途帧数量（1 = 不重叠，2 = 双缓冲，3 = 三缓冲）
		uint32_t frame_overlap = 2;
		//垂直同步（true → FIFO 呈现模式）
		bool vsync = true;
		//校验层开关（仅 Debug 构建有效；Release 强制关闭）
		bool validation = true;
		//窗口是否隐藏（无头用例用；仍创建真实窗口与表面）
		bool hidden = false;
		//超采样抗锯齿倍率（1 = 关闭；2..4 = N×N SSAA；默认 1 保持既有画面逐字节不变）
		uint32_t ssaa_factor = 1;
		//多重采样抗锯齿采样数（第九十六轮；1 = 关闭，2/4/8 = MSAA；默认 1 保持既有画面逐字节不变）
		uint32_t msaa_samples = 1;
		/*
		MSAA 深度解析（第一百九十轮；默认开启）
		采样数 > 1 时，场景 pass 结束时把多重采样深度按「样本 0」解析到单采样深度图像，
		使景深 / 深度描边 / 屏幕空间遮蔽等深度类后处理在 MSAA 下读到有效深度（此前读到的是未定义内容）。
		采样数为 1 或目标无多重采样深度时该开关无作用（画面逐字节不变）。
		*/
		bool msaa_depth_resolve = true;
		/*
		离屏渲染目标默认 16F（方向九第 756 轮；默认关闭）
		开启后 resource().render_target_create 改用 VK_FORMAT_R16G16B16A16_SFLOAT，
		使离屏目标可保存 >1 的线性值（HDR 泛光高光阈值）；关闭时与引入该开关前逐字节一致。
		（显式指定格式的 render_target_create_format 不受该开关影响。）
		*/
		bool hdr_targets = false;
		/*
		HDR 默认路径的色调映射算子（第 759 轮；默认 0）
		0 = ACES 近似（色调映射.frag，与第七百五十七轮一致）；1 = 胶片（Hable）；2 = 指数；3 = 扩展 Reinhard。
		仅在 hdr_targets 开启且走默认路径时生效；越界值按 0 处理，关闭时不影响任何画面。
		*/
		uint32_t hdr_tone_operator = 0;
		//静态合批（第一百二十七轮；默认开启；仅合并相邻等价绘制项，画面不变）
		bool static_batching = true;
		/*
		渲染工作线程数（第一百七十七轮；0 = 单线程，默认）
		大于 0 时初始化阶段启动该数量的工作线程，把「实例级视锥剔除」与「实例缓冲差异扫描」按块并行；
		块之间互不重叠，结果与单线程完全一致（画面与统计逐字节相同），仅主机侧耗时下降。
		*/
		uint32_t render_threads = 0;
		/*
		并行录制的最小绘制项数（第二百零二轮；默认 32，钳到 2..4096）
		绘制项少于此值时即使有工作线程也走单线程录制（并行收益小于分段开销）。
		*/
		uint32_t parallel_min_items = 32;
		/*
		并行录制的最大段数（第二百零三轮；默认 8，钳到 2..32）
		段数还受「线程数」与「绘制项数 / 16」约束，取三者最小值。
		*/
		uint32_t parallel_max_segments = 8;
		/*
		每帧实例存储容量（第一百七十七轮；默认 256，钳到 [16, 1048576]）
		决定单帧可登记的实例上限（实例缓冲按该容量 × 112 字节 × 在途帧数分配；第四百一十五轮同步步长注释）；
		大批量实例化绘制（数千以上）时才需要上调，默认值与引入该配置前一致。
		*/
		uint32_t instance_capacity = 256;
		/*
		实例级剔除的并行阈值（第二百二十二轮；默认 256，钳到 1..65536）
		单次实例化绘制的实例数达到该值且工作线程可用时，逐实例视锥剔除走按块并行；
		低于该值由调用线程整段执行（分块开销大于收益）。默认值与引入该配置前一致。
		*/
		uint32_t cull_parallel_min_instances = 256;
		/*
		实例缓冲差异扫描 / 缓存回写的并行阈值（第二百二十六轮；默认 512，钳到 1..65536）
		实例数达到该值且工作线程可用时，逐实例比较与缓存回写走按块并行；低于该值由调用线程整段执行。
		默认值与引入该配置前一致。
		*/
		uint32_t instance_scan_parallel_min = 512;
		//着色器目录（空 → 相对可执行文件目录的 "shaders"）
		std::string shader_dir = "";
		/*
		文字覆盖率写入（第一百二十六轮）
		true 时文字管线启用 alphaToCoverage（仅 MSAA 采样数 > 1 时生效，改善字形边缘）；
		false（默认）保持既有 alpha 混合，1× 下逐字节不变。
		*/
		bool text_alpha_to_coverage = false;
		/*
		管线缓存文件路径（第一百零六轮；空 = 关闭）
		初始化时若文件存在则载入作为 VkPipelineCache 初始数据；关闭时写回该文件。
		路径按当前工作目录解释，支持中文（按 UTF-8 转换）。
		*/
		std::string pipeline_cache_path = "";
		/*
		计算路径开关（第二百四十六轮；默认开启）
		false 时初始化不构建计算上下文、不登记自检管线，`compute_supported()` 恒为假，
		全部 GPU 驱动路径保持关闭；默认帧路径不受影响（逐字节不变）。
		*/
		bool compute_enable = true;
		/*
		GPU 实例剔除开关（第二百四十九轮；默认关闭）
		为真且计算路径可用时，实例化绘制改走「计算着色器压缩 ＋ GPU 计数」；失败自动回落 CPU 剔除。
		关闭时与引入前逐字节一致。
		*/
		bool gpu_cull_enable = false;
		/*
		间接绘制开关（第二百五十二轮；默认关闭）
		为真且设备可用时，主场景 pass 把「连续兼容渲染项」合并为 `vkCmdDrawIndexedIndirect`；
		阴影 pass、并行分段录制与容量不足时自动回落逐项绘制。关闭时与引入前逐字节一致。
		*/
		bool gpu_indirect_enable = false;
		/*
		Hi-Z 遮挡剔除开关（第二百五十三轮起；默认关闭）
		为真且计算路径可用时，初始化后首次切换到真会惰性创建「每帧槽深度历史 ＋ mip 金字塔」资源；
		关闭时不分配任何资源。资源只创建不参与录制（遮挡判定与接入在 254–256 轮落地），故关闭时画面逐字节不变。
		*/
		bool hiz_enable = false;
		/*
		Cluster 光照开关（第二百六十六轮；默认关闭）
		为真且计算路径可用时，初始化后首次切换到真会惰性创建「每帧槽光源记录 SSBO」并按帧上传；
		关闭时不分配任何资源，既有前向 8 盏路径逐字节不变（分簇与着色器采样在 267 起落地）。
		*/
		bool cluster_enable = false;
		/*
		分簇网格（第二百六十七轮；默认 16 × 9 × 24）
		各轴运行期经 cluster_grid_set 钳到 1..64，总簇数不超过 4096。
		*/
		uint32_t cluster_grid[3] = { 16, 9, 24 };
	};

	/*
	帧统计（只读观测值；Vulkan-free）
	主机侧计时区间为「录制 ＋ 提交 ＋ 呈现调用」；
	GPU 侧耗时由帧模块的时间戳查询池量测（设备不支持时该字段恒 0）。
	*/
	struct Frame_Stats
	{
		//已推进帧数
		uint64_t frames = 0;
		//最近一帧主机侧耗时（毫秒）
		double frame_ms = 0.0;
		/*
		最近一帧主机侧分段耗时（毫秒；第一百三十三轮）
		录制 = 命令录制调用；提交 = vkQueueSubmit2；呈现 = vkQueuePresentKHR（离屏路径恒 0）。
		三者之和不超过 frame_ms（差值为取图像 / 等待栅栏等前置开销）。
		*/
		double host_record_ms = 0.0;
		double host_submit_ms = 0.0;
		double host_present_ms = 0.0;
		//累计绘制项数（含同帧多 pass 路径的后处理绘制项；**按登记项计**，含被跳过的不完整项）
		uint64_t draw_items = 0;
		//累计实例数（按登记项计）
		uint64_t instances = 0;
		/*
		累计**实际绘制**项数（第十一轮新增；不含被跳过的渲染项）
		与 draw_items 的差异：登记项若管线 / 缓冲缺失、索引数或实例数为 0，则被跳过、只计入 draw_items。
		二者相等即表示「所有登记项都真正下达了绘制命令」。
		*/
		uint64_t drawn_items = 0;
		//累计**实际绘制**实例数（第十一轮新增；不含被跳过的渲染项）
		uint64_t drawn_instances = 0;
		//最近一帧 GPU 侧耗时（毫秒；设备不支持 GPU 时间戳时恒 0）
		double gpu_frame_ms = 0.0;
		/*
		GPU 耗时分解（第一百二十八轮；设备不支持时各项恒 0）
		场景 = 场景 pass；后处理 = 全部后处理级合计；阴影 = 最近一次阴影 pass（独立提交）。
		场景 ＋ 后处理 ≤ 总耗时（差值为收尾布局转换与呈现准备）。
		*/
		double gpu_scene_ms = 0.0;
		double gpu_post_ms = 0.0;
		double gpu_shadow_ms = 0.0;
		/*
		累计分段 GPU 耗时（第一百三十一轮；设备不支持时各项恒 0）
		场景 / 后处理随每次成功读回累加；阴影每完成一次读回累加一次（按需重绘时才会产生样本）。
		gpu_shadow_samples 为已读回的阴影样本数。
		*/
		double gpu_scene_total_ms = 0.0;
		double gpu_post_total_ms = 0.0;
		double gpu_shadow_total_ms = 0.0;
		uint64_t gpu_shadow_samples = 0;
		//累计主机侧耗时（毫秒；自 init 以来各帧 frame_ms 之和）
		double frame_total_ms = 0.0;
		/*
		累计主机侧分段耗时（毫秒；第一百三十四轮）
		三段随每帧累加，与 frame_total_ms 同一口径；离屏帧不累加呈现段。
		*/
		double host_record_total_ms = 0.0;
		double host_submit_total_ms = 0.0;
		double host_present_total_ms = 0.0;
		/*
		累计 GPU 侧耗时（毫秒；自 init 以来各次成功读回的时间戳之和；设备不支持时恒 0）
		读回口径为「按帧槽环形读回」：第 N 帧读回的是该槽上一轮（第 N − 在途帧数 帧）的耗时。
		*/
		double gpu_total_ms = 0.0;
		/*
		已读回 GPU 样本数（自 init 以来累计）
		与帧数关系为 max(0, frames − 在途帧数)（帧槽首轮未写时间戳时不可读回）；设备不支持时恒 0。
		*/
		uint64_t gpu_samples = 0;
		/*
		最近一帧实际执行的后处理级数（第二十二轮新增）
		0 = 本帧未走后处理链（交换链直通 / 离屏路径）；N = 链路径实际录制了 N 个后处理 pass。
		*/
		uint32_t post_stages = 0;
		/*
		累计因视锥剔除而未登记的绘制项数（第二十三轮新增）
		与 draw_items 互补：draw_items 为实际登记量，二者之和即「意图登记的项数」。
		*/
		uint64_t culled_items = 0;
		/*
		最近一帧即时观测（第一百三十二轮）
		last_draw_items / last_instances = 该帧合批后实际交给录制的项数与实例数；
		last_culled_items = 该帧因视锥剔除而未登记的项数。清零累计量时三者保持不变。
		*/
		uint64_t last_draw_items = 0;
		uint64_t last_instances = 0;
		uint64_t last_culled_items = 0;
		/*
		实例级剔除观测（第一百五十三轮）
		culled_instances 为自 init 以来因视锥剔除而未写入实例缓冲的实例数；
		last_culled_instances 为最近一帧的同类数量；清零累计量时即时量保持不变。
		*/
		uint64_t culled_instances = 0;
		uint64_t last_culled_instances = 0;
		/*
		剔除耗时与 GPU 驱动观测（第二百四十七轮）
		cpu_cull_ms = 最近一帧主机侧实例级视锥剔除耗时（毫秒）；cpu_cull_total_ms = 自 init 以来累计。
		gpu_cull_ms / cluster_build_ms / indirect_draws 分别对应 GPU 剔除耗时、Cluster 构建耗时与间接绘制次数，
		在接入前（２４８ / ２６３ / ２４９ 轮）恒 0；清零累计量时 cpu_cull_ms 保持不变。
		*/
		double cpu_cull_ms = 0.0;
		double cpu_cull_total_ms = 0.0;
		double gpu_cull_ms = 0.0;
		//GPU 压缩成功派发次数（第二百五十一轮；关闭 GPU 剔除时恒 0；清零累计量时归零）
		uint64_t gpu_cull_dispatches = 0;
		//最近一次 GPU 压缩被 Hi-Z 完全遮挡而剔除的实例数（第二百六十一轮；关闭遮挡时恒 0）
		uint64_t gpu_occluded = 0;
		double cluster_build_ms = 0.0;
		//最近一次簇数据整备的条目数（第二百七十三轮；Cluster 关闭时恒 0）
		uint32_t cluster_entries = 0;
		//最近一帧上传到图形 set0 的簇数据字节数（第二百八十八轮；Cluster 关闭时恒 0）
		uint32_t cluster_gpu_bytes = 0;
		//是否发生过「Cluster 关闭且光源超过 8 盏」的截断（第二百八十轮；1 = 发生过）
		uint32_t light_truncated = 0;
		/*
		光照诊断（第六百四十五轮；A4）
		lights_active = 最近一次场景应用后参与光照的光源数（旧前向 ≤ 8、簇路径 ≤ 1024）；
		lights_dropped = 同一次应用中因预算 / 重要性选择而未登记的光源数；
		light_select_mode = 生效的选择模式（0 = 声明顺序、1 = 重要性选择）。
		*/
		uint32_t lights_active = 0;
		uint32_t lights_dropped = 0;
		uint32_t light_select_mode = 0;
		/*
		阴影 pass 诊断（第六百四十七轮；A4 补齐）
		shadow_passes = 最近一帧实际执行的阴影 pass 数（方向光级联 ＋ 聚光瓦片；复用帧为 0）；
		spot_shadow_passes = 其中聚光瓦片 pass 数；shadow_resolution = 当前阴影图边长（像素；未建立为 0）。
		*/
		uint32_t shadow_passes = 0;
		uint32_t spot_shadow_passes = 0;
		uint32_t shadow_resolution = 0;
		//聚光阴影图集是否实际生效（第六百八十轮；0 = 独立瓦片、1 = 4×2 图集）
		uint32_t shadow_atlas = 0;
		//聚光阴影图集颜色附件字节数（第六百八十四轮；0 = 未启用图集；4S×2S×4 字节）
		uint64_t shadow_atlas_bytes = 0;
		//PCSS 软阴影是否生效（第六百五十七轮；0 = 关闭（默认）、1 = 开启）
		uint32_t shadow_pcss = 0;
		/*
		级联纹素与淡化观测（第六百六十一轮）
		shadow_texel_world = 最近一帧生效的纹素世界尺寸（多级取各片最大值；未建立阴影图为 0）；
		cascade_blend_applied = 最近一帧实际写入 UBO 的级联淡化比例（0 = 硬切换）。
		*/
		float shadow_texel_world = 0.0f;
		float cascade_blend_applied = 0.0f;
		/*
		阴影提交观测（第六百六十五轮；C2 基线）
		shadow_submits = 最近一次阴影重绘实际提交的命令缓冲次数。
		当前口径为「一片一次提交」，故等于 shadow_passes；C2（单次提交合并）落地后应恒为 1。
		*/
		uint32_t shadow_submits = 0;
		uint64_t indirect_draws = 0;
		/*
		合批收益（第一百三十五轮）
		last_merged_items = 最近一帧被合并掉的项数（主 pass ＋ 阴影各片）；merged_items = 自 init 以来累计。
		仅在 `static_batching` 开启时增长；清零累计量时 last_merged_items 保持不变。
		*/
		uint64_t last_merged_items = 0;
		uint64_t merged_items = 0;
		/*
		阴影按需重绘观测（第一百四十五轮）
		shadow_redraws / shadow_reuses 为自 init 以来「重绘 / 复用」的累计次数；
		last_shadow_items 为最近一次重绘的绘制项数，shadow_last_redrawn 为最近一帧是否重绘（1 / 0）。
		*/
		uint64_t shadow_redraws = 0;
		uint64_t shadow_reuses = 0;
		uint32_t last_shadow_items = 0;
		uint32_t shadow_last_redrawn = 0;
		/*
		本帧参与投影的聚光灯阴影瓦片数（第五百二十轮）
		等于本帧有效聚光灯数量（0..8）；0 = 本帧无聚光阴影。
		*/
		uint32_t spot_shadow_tiles = 0;
		/*
		已建立的独立聚光阴影瓦片目标数（第七百零七轮；图集生效时恒 0）
		图集承担全部瓦片后不再创建独立目标，用于验证「切换即预创建、首帧零创建」与显存回收。
		*/
		uint32_t spot_shadow_tile_targets = 0;
		/*
		最近一次阴影 pass 的并行段数（第二百一十二轮）
		0 = 单线程录制（默认、投影项未达 `parallel_min_items`、线程数不足）；
		≥ 2 表示阴影命令由多段次级缓冲并行录制。与 parallel_segments（最近一帧场景 pass）分列，便于分别观测两条录制链路。
		*/
		uint32_t shadow_parallel_segments = 0;
		/*
		累计按需上传字节（第七十二轮新增；与门面 scene_upload_bytes / instance_upload_bytes 同源）
		场景 UBO 以 16 字节为单元、实例缓冲以一个实例（80 字节）为单元，仅写入变化区间。
		*/
		uint64_t scene_upload_bytes = 0;
		uint64_t instance_upload_bytes = 0;
		//最近一帧实际使用的 MSAA 采样数（第一百零一轮；1 = 单采样）
		uint32_t msaa_samples = 1;
		/*
		粒子模拟统计（第三百二十二轮；未开启粒子时各项为 0，particle_gpu_ms 为 −1）
		particle_active = 当前活跃数；particle_born_total / particle_died_total 自 init 累计；
		particle_sim_ms = 最近一次步进的主机侧耗时（CPU 参考或 GPU 同步链）；
		particle_gpu_ms = 最近一次 GPU 调度的设备耗时（设备不支持时间戳时为 −1）。
		*/
		uint64_t particle_active = 0;
		uint64_t particle_born_total = 0;
		uint64_t particle_died_total = 0;
		double particle_sim_ms = 0.0;
		double particle_gpu_ms = -1.0;
		/*
		最近一次常驻紧凑的主机侧耗时（第三百四十轮；未紧凑为 0）
		第三百四十九轮起，间接路径的紧凑与写命令合并为同一次提交，故该值含「写间接命令」的耗时。
		*/
		double particle_compact_ms = 0.0;
		/*
		最近一次粒子绘制登记的路径（第三百四十八轮）
		0 = 尚未登记 / 粒子未开启；1 = CPU 参考（`particle_draw`）；2 = GPU 直接（`particle_gpu_draw`）；
		3 = GPU 间接（`particle_indirect_draw`）。仅反映「最近一次登记」，与实际出帧可相差一帧。
		*/
		uint32_t particle_draw_path = 0;
		/*
		渲染工作线程观测（第一百七十七轮）
		render_threads = 当前生效的工作线程数（0 = 单线程）；
		parallel_dispatches = 自 init 以来实际派发的并行任务次数（退化为调用线程整段执行的任务不计）；
		parallel_segments = 最近一帧并行录制的段数（第一百九十八轮；0 = 单线程录制，仅同帧后处理链路径非 0）。
		三者均不影响画面与绘制统计。
		*/
		uint32_t render_threads = 0;
		uint64_t parallel_dispatches = 0;
		uint32_t parallel_segments = 0;
		/*
		并行派发按用途拆分（第二百二十四轮）
		三类均自 init 起累计（与 parallel_dispatches 同口径，frame_stats_reset 不清零）；
		用于区分「实例剔除 / 差异扫描 / 缓存回写」的派发来源；录制段的派发不计入这三项。
		*/
		uint64_t cull_parallel_dispatches = 0;
		uint64_t scan_parallel_dispatches = 0;
		uint64_t cache_parallel_dispatches = 0;
	};

	/*
	资源计数快照（第一百一十三 / 一百一十四轮；只读诊断）
	各字段为当前有效记录数（材质按有效材质记录计）。
	*/
	struct Renderer_Resource_Counts
	{
		//网格数
		uint32_t meshes = 0;
		//纹理数（含内置默认白纹理）
		uint32_t textures = 0;
		//图形管线数（含默认场景管线）
		uint32_t pipelines = 0;
		//字体数
		uint32_t fonts = 0;
		//渲染目标数（含阴影目标）
		uint32_t render_targets = 0;
		//材质数
		uint32_t materials = 0;
	};

	//渲染目标原生附件快照（uint64_t 承载原生句柄，保持公共头 Vulkan-free）
	struct Render_Target_Native_Info
	{
		uint64_t color_image = 0;
		uint64_t color_view = 0;
		uint64_t depth_image = 0;
		uint64_t depth_view = 0;
		uint64_t msaa_color_image = 0;
		uint64_t msaa_color_view = 0;
		uint64_t msaa_depth_image = 0;
		uint64_t msaa_depth_view = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t sample_count = 1;
	};

	//单批实例登记所需的材质值快照
	struct Instance_Material_Values
	{
		bool pbr_active = false;
		float pbr_roughness = 1.0f;
		float pbr_metallic = 0.0f;
		float emissive = 0.0f;
		bool toon_active = false;
		float toon_steps = 0.0f;
		float toon_rim = 0.0f;
		float toon_specular = 0.0f;
		float toon_tint[3] = { 1.0f, 1.0f, 1.0f };
	};

	//按帧阴影矩阵与逐瓦片参数值快照
	struct Scene_Shadow_Frame
	{
		float light_view_proj[16] = {};
		float light_view_proj_far[16] = {};
		float light_view_proj_far2[16] = {};
		float cascade_depth_span[3] = {};
		float shadow_texel_world = 0.0f;
		float cascade_blend_effective = 0.0f;
		float spot_view_proj_tiles[8][16] = {};
		float spot_shadow_spans[8] = {};
		float spot_shadow_bias_tiles[8] = {};
		float spot_shadow_softness_tiles[8] = {};
		float spot_shadow_strength_tiles[8] = {};
		float spot_shadow_slot_tiles[32] = {};
		uint32_t spot_shadow_tiles = 0;
	};

	/*
	材质实例（第四百四十八轮；方向七阶段 A）
	模板材质句柄 ＋ 逐实例覆盖参数；本身不持有 GPU 资源，绘制时与模板合成（阶段 C）。
	覆盖范围：tint（含 alpha）与自发光强度；未覆盖项沿用模板。
	*/
	struct Material_Instance_Data
	{
		//模板材质句柄（须为已登记材质）
		Material_Handle 模板 = 0;
		//覆盖底色（RGBA，0..1；仅 use_tint 为真时生效，乘性叠加到模板色调）
		float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		//是否覆盖底色
		bool use_tint = false;
		//覆盖自发光强度（0..1；仅 use_emissive 为真时生效，直接覆盖模板值）
		float emissive = 0.0f;
		//是否覆盖自发光
		bool use_emissive = false;
		/*
		逐实例卡通覆盖（第四百七十一轮；方向七延伸）
		仅 use_toon 为真时生效：色阶数 0..8（0 = 关闭）、边缘光 0..1、卡通高光阈值 0..1。
		与模板材质的 toon_override 互不依赖：实例值优先。
		*/
		bool use_toon = false;
		//色阶数（0..8）
		float toon_steps = 0.0f;
		//边缘光强度（0..1）
		float toon_rim = 0.0f;
		//卡通高光阈值（0..1）
		float toon_specular = 0.0f;
		/*
		逐实例 PBR 覆盖（第五百六十四轮）
		仅 use_pbr 为真时生效：覆盖即启用 PBR（等价于模板材质执行 material_pbr_set）；
		粗糙度 (0.05, 4]、金属度 [0, 1]。
		*/
		bool use_pbr = false;
		//粗糙度（0.05 < 值 ≤ 4）
		float pbr_roughness = 1.0f;
		//金属度（0..1）
		float pbr_metallic = 0.0f;
		/*
		逐实例清漆覆盖（第五百九十四轮）
		仅 use_clearcoat 为真时生效：覆盖即启用清漆层（等价于模板材质执行清漆设置）；
		强度 (0, 1]、粗糙度 [0.03, 1]。
		*/
		bool use_clearcoat = false;
		//清漆强度（0 < 值 ≤ 1）
		float clear_coat = 0.0f;
		//清漆粗糙度（0.03..1）
		float clear_coat_roughness = 0.15f;
	};

	/*
	场景对象（第四百二十四轮；资产加载方向第三阶段）
	materials 为该网格的材质句柄（按 OBJ 段序号对齐，供 mesh_draw_sections 逐段绑定；空表示无材质）。
	*/
	struct Asset_Object
	{
		//网格句柄
		Mesh_Handle mesh = 0;
		//材质句柄（按段序号对齐）
		std::vector<Material_Handle> materials;
		/*
		材质实例覆盖（第四百六十七轮；方向七阶段 B-B）
		与 materials 等长；模板字段填模板句柄，use_tint / use_emissive 为假表示不覆盖。
		空数组表示该对象没有实例覆盖（绘制路径与旧版逐字节一致）。
		*/
		std::vector<Material_Instance_Data> material_instances;
		//位置（世界坐标）
		float position[3] = { 0.0f, 0.0f, 0.0f };
		//缩放（三轴）
		float scale[3] = { 1.0f, 1.0f, 1.0f };
		/*
		欧拉角旋转（度；第四百三十一轮）
		顺序 X → Y → Z（R = Rz·Ry·Rx）；缺省全零 = 不旋转。
		变换矩阵由应用层构造后经 `mesh_draw_sections` 传入（渲染层不做 T·R·S 组装）。
		*/
		float rotation_deg[3] = { 0.0f, 0.0f, 0.0f };
	};

	/*
	场景绘制清单（第四百二十四轮）
	由应用层装配后交给渲染层：`objects` 按装配顺序给出待绘制对象，
	渲染层只按清单逐项登记绘制，不解析资产、不管理资源生命周期。
	*/
	struct Asset_Scene
	{
		//摆放对象
		std::vector<Asset_Object> objects;
		//本次创建的网格（由应用层持有并在其桥接层释放）
		std::vector<Mesh_Handle> meshes;
		//本次创建的材质（同上）
		std::vector<Material_Handle> materials;
	};
}
