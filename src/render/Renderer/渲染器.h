#pragma once
//预编译头
#include "Engine/EngineSystem/src/core/Renderer/common/前置头文件包含.h"
//光源数据记录（Vulkan-free；第二百六十五轮）
#include "Engine/EngineSystem/src/core/Renderer/src/render/Scene/光源数据.h"
//分簇数据与 CPU 逐簇计数参考（Vulkan-free；第二百六十七轮）
#include "Engine/EngineSystem/src/core/Renderer/src/render/Cluster/簇划分.h"
//粒子数据（Vulkan-free；第三百零七轮；已下沉到 EngineCore，模拟与绘制两侧共享同一份布局）
#include "Engine/EngineCore/src/core/effect/粒子数据.h"
/*
网格数据（Vulkan-free）
公共面只暴露引擎语义类型（Vertex / Mesh_Data / Mesh_Handle），
其定义置于 Mesh 模块的共用头，层外看不到任何 Vulkan / VMA / SDL3 / glm 类型。
*/
#include "Engine/EngineSystem/src/core/Renderer/src/render/Mesh/网格数据.h"
//管线公共语义类型（Vulkan-free）：混合档
#include "Engine/EngineSystem/src/core/Renderer/src/render/Pipeline/管线数据.h"
//纹理公共语义类型（Vulkan-free）：采样器状态
#include "Engine/EngineSystem/src/core/Renderer/src/render/Texture/纹理数据.h"
//材质公共语义类型（Vulkan-free）：材质描述与句柄
#include "Engine/EngineSystem/src/core/Renderer/src/render/Pipeline/材质数据.h"
//场景公共语义类型（Vulkan-free）：场景描述
#include "Engine/EngineSystem/src/core/Renderer/src/render/Scene/场景数据.h"
//文字布局公共语义类型（Vulkan-free）：对齐方式与换行宽度
#include "Engine/EngineSystem/src/core/Renderer/src/render/Text/文字布局数据.h"
//计算公共语义类型（Vulkan-free）：句柄与字节上限（第二百四十三轮）
#include "Engine/EngineSystem/src/core/Renderer/src/render/Compute/计算数据.h"

//游戏引擎命名空间
namespace engine
{
	//UI 通道（扩展公共头 渲染器_UI通道.h 定义；此处仅前置声明，保持本公共头零 Vulkan / SDL 类型）
	class Ui_Channel_Context;

	//渲染器前置声明（供下方层内 GPU 粒子推进入口引用）
	class Renderer;

	/*
	层内 GPU 粒子推进（模拟语义归 EngineSystem；渲染层只提供计算原语与缓冲）
	实现位于 EngineSystem/src/effect/Particle/粒子GPU推进.cpp，经 friend 访问渲染器私有实现。
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

	/*
	渲染器（门面）
	生命周期：init → frame_render × N → shutdown
	状态机：未初始化 → 已初始化 → 已销毁（shutdown 幂等，可重复调用）
	非线程安全：要求所有公开 API（init / shutdown / frame_render / mesh_draw / scene_apply / 各查询与 setter）
	来自同一线程（见 §4.5）。
	说明（第六百四十四轮）：本层的「多线程渲染」指**内部并行**——初始化时按 `render_threads` 启动自己的工作线程池，
	由它们并行执行实例级视锥剔除、实例缓冲差异扫描，并把场景 / 阴影 pass 的绘制命令录制进**次级命令缓冲**，
	最后由调用线程在主命令缓冲里以 `vkCmdExecuteCommands` 汇总提交。
	工作线程不对外暴露、也不接受外部调用；Vulkan 对象（命令池 / 缓冲 / 描述符集）的创建、提交与呈现仍全部在调用线程上完成。
	故「内部多线程」与「对外线程安全」是两件事：不要从多个线程并发调用本类接口，也不要与外部自行提交共用同一队列。
	*/
	class Renderer
	{
	public:
		//构造（不创建任何 GPU 资源）
		Renderer(void);
		//析构（内部调用 shutdown，保证资源不泄漏）
		~Renderer(void);
		//不可拷贝、不可移动（持有 GPU 资源与线程亲和）
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		//初始化（任一环节失败即逆序回退并返回 false，不留下半初始化状态）
		bool init(const Renderer_Config& config);
		//关闭（幂等）
		void shutdown(void);
		//是否已初始化
		bool valid(void) const;
		/*
		每帧推进（泵事件 → 取图像 → 录制 → 提交 → 呈现）
		readback_pixels 非空即把本帧最终颜色附件回读到该主机缓冲（RGBA8 紧排、逐行自上而下）；
		readback_capacity 为其字节容量，须不小于「宽 × 高 × 4」，否则本帧不渲染并返回假；
		readback_width / readback_height 非空时回填回读尺寸。
		未请求回读时，后三个参数可全部省略（与旧调用完全兼容）。
		*/
		bool frame_render(uint8_t* readback_pixels = nullptr, uint32_t readback_capacity = 0,
			uint32_t* readback_width = nullptr, uint32_t* readback_height = nullptr);
		//窗口是否请求退出
		bool close_requested(void) const;
		//已呈现帧数（调试与用例断言用）
		uint64_t frame_count(void) const;
		//取帧统计快照（未初始化时各项为零）
		Frame_Stats frame_stats_get(void) const;
		/*
		等待全部在途帧完成（第二百一十八轮）
		宿主在销毁自建的 GPU 资源（如 UI 后端的描述符池 / 采样器 / 管线）之前调用，
		避免资源仍被在途命令缓冲引用而触发校验错误；未初始化时静默返回。
		*/
		void frame_wait_idle(void);
		/*
		清零累计帧统计（第九十一轮新增）
		帧数、绘制项 / 实例 / 实际绘制量、累计耗时（主机 / GPU / 分段）、GPU 样本（含阴影样本）、
		剔除数、上传字节全部归零；最近一帧的即时量（frame_ms / host_record_ms / host_submit_ms /
		host_present_ms / gpu_frame_ms / gpu_scene_ms / gpu_post_ms / gpu_shadow_ms / post_stages /
		last_draw_items / last_instances / last_culled_items / last_merged_items）保持不变。未初始化时静默忽略。
		*/
		void frame_stats_reset(void);
		/*
		是否支持 GPU 时间戳（设备与图形队列族均支持时为真；未初始化时为假）
		为假时帧统计的 gpu_frame_ms 恒 0（已优雅降级，不影响渲染）。
		*/
		bool gpu_timestamp_supported(void) const;
		//是否支持像素回读（交换链图像具备传输源用法；未初始化时为假）
		bool readback_supported(void) const;
		/*
		是否支持计算路径（第二百四十一轮）
		为真表示：图形队列族含 COMPUTE 位、计算上下文构建完成、健康检查管线已登记。
		为假时全部 GPU 驱动路径（剔除 / Hi-Z / Cluster）保持关闭，初始化本身不失败。
		*/
		bool compute_supported(void) const;
		/*
		计算链路健康检查（第二百四十一轮）
		在一次性命令缓冲内完成「绑定 → 推送常量 → 调度 → 计算→主机屏障 → 回读首元素」，
		回读值应等于传入的 magic（按 uint32 位模式比较）。不支持计算或无管线时返回假。
		*/
		bool compute_health_check(uint32_t magic, uint32_t& value_out);
		/*
		计算存储缓冲（第二百四十三轮；第二百四十四轮补设备本地选项）
		默认主机可见 SSBO（`STORAGE_BUFFER` 用法），供计算着色器读写；句柄自 1 起，0 = 无效。
		size 须 > 0 且 ≤ COMPUTE_BUFFER_MAX_BYTES；data 为初值（空 = 分配后不复位）。
		device_local 为真时改为设备本地（`STORAGE_BUFFER | TRANSFER_SRC | TRANSFER_DST`）：
		初值与后续写入经暂存上传，读回须走 compute_readback（GPU 拷贝到暂存再映射）。
		*/
		bool compute_buffer_load(Compute_Buffer_Handle& handle_out, uint32_t size,
			const void* data = nullptr, bool device_local = false);
		//卸载计算存储缓冲（幂等；句柄复位为 0）
		void compute_buffer_unload(Compute_Buffer_Handle handle);
		//写入缓冲一段（主机可见缓冲直接映射写入；设备本地缓冲经暂存上传；越界或句柄无效返回假）
		bool compute_buffer_write(Compute_Buffer_Handle handle, const void* data, uint32_t size, uint32_t offset = 0);
		//读回缓冲一段（仅主机可见缓冲直接映射；设备本地缓冲请用 compute_readback）
		bool compute_buffer_read(Compute_Buffer_Handle handle, void* data, uint32_t size, uint32_t offset = 0);
		/*
		粒子 GPU 推进资源（第三百一十一轮）
		ensure 惰性创建双缓冲（容量 × 32 字节）与推进管线；destroy 幂等释放（须先于计算上下文销毁）。
		*/
		bool particle_gpu_ensure(void);
		void particle_gpu_destroy(void);
		//粒子 billboard 绘制资源惰性创建（第三百一十四轮；单位四边形 ＋ 粒子管线）
		bool particle_draw_ensure(void);
		//粒子 GPU 绘制管线惰性创建（第三百四十一轮；粒子GPU.vert ＋ 粒子.frag，不消费实例属性）
		bool particle_gpu_draw_ensure(void);
		/*
		粒子外观推送（第三百五十四轮；第三百五十五轮补软边宽度）
		按当前粒子参数写入推送块：offset 64 = 新生颜色 rgb、offset 80 = 衰老颜色 rgb、
		offset 96.x = 颜色启用标志、offset 96.y = 软边宽度。
		未启用颜色时标志写 0（片元着色器回落内置暖白 → 暗红曲线）；软边宽度恒写（0 = 内置 0.65 内半径），
		CPU 路径占位块残留的材质数值一并被覆盖。
		*/
		void particle_style_push_fill(std::array<float, 32>& push) const;
		/*
		粒子软粒子深度源（第三百六十六轮）
		取本帧槽 HiZ 历史视图与最近邻采样器的原生句柄（uint64_t）；参数档位为 0 / HiZ 未启用 / 该槽历史未就绪时返回假。
		*/
		bool particle_depth_source(uint32_t slot, uint64_t& view_out, uint64_t& sampler_out) const;
		/*
		GPU 回读通道（第二百四十四轮）
		在一次性命令缓冲内：计算→传输屏障 → `vkCmdCopyBuffer` 到暂存 → 传输→主机屏障 → 围栏等待 → 映射读回。
		与缓冲是否设备本地无关（主机可见缓冲亦可，用于统一口径的验证）。
		越界或句柄无效返回假；size 须 > 0。
		*/
		bool compute_readback(Compute_Buffer_Handle handle, void* data, uint32_t size, uint32_t offset = 0);
		/*
		计算统计（第二百四十五轮）
		dispatch_count = 累计调度次数；elapsed_ms = 最近一次同步调度的 GPU 耗时（毫秒）。
		本机不支持时间戳时 elapsed_ms 恒为 −1（统计退化为计数，不影响调度本身）。
		*/
		Compute_Stats compute_stats_get(void) const;
		//是否支持计算侧时间戳计时（不支持时 compute_stats_get 的 elapsed_ms 恒为 −1）
		bool compute_timestamp_supported(void) const;
		/*
		GPU 能力与降级矩阵（第二百四十六轮）
		五项探测与方向一规划 §2.5 对应；未初始化时各项为假。`compute_enable = false` 时 compute 项仍如实反映设备能力。
		*/
		Compute_Capabilities compute_capabilities_get(void) const;
		/*
		视锥剔除原语（第二百四十八轮）
		输入 count 个列主序模型矩阵与网格局部 AABB：CPU 侧求世界包围球 → 写 SSBO → 计算着色器逐球测试 → 回读掩码。
		mask_out 每项 1 = 可见、0 = 剔除；长度须为 count。GPU 一律用球判定（比 AABB 保守），保证 CPU 的可见集是 GPU 结果的子集。
		默认路径不调用本接口；返回假表示未初始化 / 不支持计算 / 入参非法 / 调度或回读失败。
		*/
		bool gpu_cull_mask(const float* transforms, uint32_t count,
			const float local_min[3], const float local_max[3], uint32_t* mask_out);
		/*
		GPU 实例压缩（第二百四十九轮）
		与 CPU 剔除逐式同构：mvp × 8 角点 × 六面「全部角点在外」判定；可见者原子计数并写压缩矩阵。
		输出为按原相对顺序压缩的模型矩阵（每实例 16 浮点）与可见数；失败返回假（调用方回落 CPU 路径）。
		*/
		bool gpu_instance_compact(const float* transforms, uint32_t count,
			const float local_min[3], const float local_max[3], float* matrices_out, uint32_t* visible_out);
		/*
		带距离 / 背面剔除的实例压缩（第二百五十轮）
		camera / forward 为相机世界位置与单位前向；max_distance ≤ 0 不限距离；backface 为真时剔除位于相机后方的实例。
		与 `gpu_instance_compact` 的差别仅多这两个判据，视锥判定与压缩口径完全一致。
		*/
		bool gpu_instance_compact_ex(const float* transforms, uint32_t count,
			const float local_min[3], const float local_max[3], const float camera[3], const float forward[3],
			float max_distance, bool backface, float* matrices_out, uint32_t* visible_out);
		//GPU 剔除开关（默认关；为真且计算可用时实例化绘制走 GPU 压缩）
		void gpu_cull_enable_set(bool enabled);
		bool gpu_cull_enable_get(void) const;
		/*
		Hi-Z 开关与资源状态（第二百五十三轮）
		hiz_enable_set(true) 在计算路径可用时惰性创建深度历史 ＋ 金字塔资源，失败返回假且开关保持关闭；
		hiz_enable_set(false) 只关闭开关，保留已创建资源。hiz_supported() 反映「计算可用 ＋ R32 浮点存储图像可用」。
		hiz_state_get 写出当前资源尺寸与级数（未创建时全 0）；hiz_level_extent 写出指定级尺寸（越界返回假）。
		*/
		bool hiz_enable_set(bool enabled);
		bool hiz_enable_get(void) const;
		bool hiz_supported(void) const;
		bool hiz_state_get(uint32_t& width_out, uint32_t& height_out, uint32_t& level_count_out) const;
		bool hiz_level_extent(uint32_t level, uint32_t& width_out, uint32_t& height_out) const;
		//累计金字塔生成次数（第二百五十四轮；关闭或未生成时为 0）
		uint64_t hiz_generation_count(void) const;
		//指定帧槽是否已生成过有效金字塔（索引超出在途帧数返回假）
		bool hiz_history_ready(uint32_t slot) const;
		/*
		HiZ 深度历史采样句柄（第三百六十五轮；阶段 E 软粒子地基）
		该槽历史已生成时写出历史视图与最近邻采样器的原生句柄（uint64_t 承载 VkImageView / VkSampler）；
		生成结束时历史已转为 SHADER_READ_ONLY 布局，可直接以 sampler2D 采样，
		内容 = 该槽上一次使用时的场景深度（双在途帧槽下约为两帧前）。
		未启用 Hi-Z / 资源未建 / 该槽无历史时返回假且出参归零。
		*/
		bool hiz_history_sample_get(uint32_t slot, uint64_t& view_out, uint64_t& sampler_out) const;
		/*
		Cluster 光照开关与资源状态（第二百六十六轮）
		cluster_enable_set(true) 在计算路径可用时惰性创建每帧槽光源记录 SSBO，失败返回假且开关保持关闭；
		关闭时只停用上传、保留已建资源。cluster_supported 反映计算路径是否可用。
		cluster_light_count 为最近一帧实际上传的光源记录数（关闭或尚未出帧时为 0）。
		*/
		bool cluster_enable_set(bool enabled);
		bool cluster_enable_get(void) const;
		bool cluster_supported(void) const;
		uint32_t cluster_light_count(void) const;
		/*
		分簇网格与 CPU 逐簇计数（第二百六十七轮）
		cluster_grid_set 按 [1,64] 逐轴钳制并保证总簇数 ≤ 4096；未初始化返回假。
		cluster_grid_get 写出当前网格（未初始化时全 0）；cluster_grid_cluster_count 返回总簇数。
		cluster_counts_reference_ex 用当前相机与光源记录写出逐簇计数（CPU 参考，供 268 轮 GPU 计数对数）；
		counts_out 长度须 ≥ 总簇数；成功返回真并写出总簇数，容量不足或未初始化返回假。
		*/
		bool cluster_grid_set(uint32_t x, uint32_t y, uint32_t z);
		bool cluster_grid_get(uint32_t& x_out, uint32_t& y_out, uint32_t& z_out) const;
		uint32_t cluster_grid_cluster_count(void) const;
		bool cluster_counts_reference_ex(uint32_t* counts_out, uint32_t capacity, uint32_t* total_out) const;
		/*
		GPU 逐簇计数（第二百六十八轮）
		与 CPU 参考同口径：上传光源记录与清零计数缓冲 → 每记录一线程调度 → 回读逐簇计数。
		成功返回真并写出总簇数；不支持计算 / 容量不足 / 调度或回读失败返回假。
		*/
		bool cluster_counts_gpu(uint32_t* counts_out, uint32_t capacity, uint32_t* total_out);
		/*
		块内前缀和（第二百六十九轮）
		CPU 参考与 GPU 同口径：按 CLUSTER_SCAN_BLOCK（256）分块的包含式前缀和 ＋ 每块总和。
		GPU 路径写出 prefix_out（长度 count）与 block_totals_out（长度 ceil(count/256)）；
		成功返回真并写出块数；不支持计算 / 入参非法 / 调度失败返回假。
		*/
		bool cluster_prefix_block_scan_cpu(const uint32_t* counts, uint32_t count, uint32_t* prefix_out,
			uint32_t* block_totals_out, uint32_t block_capacity, uint32_t* block_count_out) const;
		bool cluster_prefix_block_scan_gpu(const uint32_t* counts, uint32_t count, uint32_t* prefix_out,
			uint32_t* block_totals_out, uint32_t block_count_capacity, uint32_t* block_count_out);
		/*
		全量前缀和（第二百七十轮）
		CPU 参考与 GPU 同口径的包含式全量前缀和（块内前缀 ＋ 块间偏移）。
		GPU 路径：块内扫描 → 块总和串行扫描（排他偏移）→ 逐元素叠加偏移 → 回读。
		成功返回真；不支持计算 / 入参非法 / 调度失败返回假。
		*/
		bool cluster_prefix_scan_cpu(const uint32_t* counts, uint32_t count, uint32_t* prefix_out) const;
		bool cluster_prefix_scan_gpu(const uint32_t* counts, uint32_t count, uint32_t* prefix_out);
		/*
		簇光源列表（第二百七十一轮）
		以逐簇计数为输入，写出逐簇排他偏移（offsets_out）与「簇 → 光源索引」列表（list_out）。
		entries_out 为列表总条目（= 计数总和）；容量不足 / 未初始化 / 不支持计算返回假。
		CPU 路径按光源顺序写入（确定性）；GPU 路径簇内顺序由原子竞争决定，成员与 CPU 一致。
		*/
		bool cluster_list_build_cpu(uint32_t* list_out, uint32_t capacity, uint32_t* offsets_out,
			const uint32_t* counts, uint32_t cluster_total, uint32_t* entries_out) const;
		bool cluster_list_build_gpu(uint32_t* list_out, uint32_t capacity, uint32_t* offsets_out,
			const uint32_t* counts, uint32_t cluster_total, uint32_t* entries_out);
		/*
		簇数据整备（第二百七十二轮）
		一次调用完成「逐簇计数 → 排他偏移 → 光源列表」；use_gpu 为真且计算可用时走 GPU 路径，否则走 CPU 参考。
		counts_out / offsets_out 长度须 ≥ 网格总簇数；list_out 长度须 ≥ 条目总数（= 计数之和）。
		成功返回真并写出条目数；未初始化 / 容量不足 / 入参非法返回假。同时更新构建耗时与条目观测值。
		*/
		bool cluster_data_build(uint32_t* counts_out, uint32_t* offsets_out, uint32_t* list_out,
			uint32_t cluster_capacity, uint32_t list_capacity, uint32_t* entries_out, bool use_gpu);
		//最近一次簇数据整备的条目数（未构建时为 0）
		uint32_t cluster_entries_get(void) const;
		//最近一次簇数据整备耗时（毫秒；未构建时为 0）
		double cluster_build_ms_get(void) const;
		/*
		计算管线（第二百四十三轮）
		按 `.spv` 文件名加载并登记（入口固定 main）；句柄自 1 起，0 = 无效。
		卸载时同时释放管线与着色器模块。
		*/
		bool compute_pipeline_load(Compute_Pipeline_Handle& handle_out, const std::string& shader_name);
		//卸载计算管线（幂等；句柄复位为 0）
		void compute_pipeline_unload(Compute_Pipeline_Handle handle);
		//把存储缓冲绑定到计算集 set 0 的指定 binding（binding < COMPUTE_BINDING_MAX）
		bool compute_buffer_bind(Compute_Buffer_Handle buffer, uint32_t binding);
		/*
		同步调度（第二百四十三轮）
		一次性命令缓冲内：激活管线 → 写推送常量 → 按 elements / local_size 换算组数调度 →
		计算→主机屏障 → 围栏等待。返回即可读回结果；elements 为 0 视为无需调度（返回真）。
		*/
		bool compute_dispatch(Compute_Pipeline_Handle pipeline, uint32_t elements, uint32_t local_size,
			const void* push_data = nullptr, uint32_t push_size = 0);
		/*
		设置超采样抗锯齿倍率（SSAA；1 = 关闭，2..4 = N×N 超采样）
		取值按 1..4 钳制；自下一次 frame_render 起生效（内部按需重建超采样目标）。
		仅在「未设置同帧后处理呈现目标且未设置本帧离屏目标」时启用，优先顺序：后处理/离屏 > SSAA。
		未初始化时静默忽略。
		*/
		void ssaa_factor_set(uint32_t factor);
		//当前超采样倍率（未初始化时为 1）
		uint32_t ssaa_factor(void) const;
		/*
		设置多重采样抗锯齿采样数（MSAA；第九十六轮）
		仅接受 2 / 4 / 8（1 = 关闭；其余请求按 1 处理），并按设备对当前颜色 / 深度格式的支持上限钳制；
		自调用后下一次 frame_render 起生效（内部重建交换链 MSAA 附件、全部渲染目标与全部图形管线）。
		与 SSAA 可叠加（两者互不替代）；未初始化时静默忽略。
		*/
		void msaa_samples_set(uint32_t samples);
		//当前 MSAA 采样数（未初始化或关闭时为 1）
		uint32_t msaa_samples(void) const;
		/*
		设置 MSAA 深度解析开关（第一百九十轮；默认开启）
		开启时采样数 > 1 的场景 pass 结束时把多重采样深度按「样本 0」解析到单采样深度图像，
		使深度类后处理（景深 / 深度描边 / 屏幕空间遮蔽）可读到有效深度；
		自调用后下一次 frame_render 起生效（不重建任何资源）；未初始化时静默忽略。
		*/
		void msaa_depth_resolve_set(bool enabled);
		//当前 MSAA 深度解析开关（未初始化时为假）
		bool msaa_depth_resolve(void) const;
		/*
		当前颜色 / 深度格式共同支持的最大 MSAA 采样数（第九十九轮）
		返回 1 / 2 / 4 / 8；未初始化或均不支持时返回 1。
		*/
		uint32_t msaa_max_samples(void) const;
		//是否支持采样器各向异性过滤（设备创建时按特性支持情况启用；未初始化时为假）
		bool sampler_anisotropy_supported(void) const;
		//采样器最大各向异性倍数（设备上限；未初始化或未启用该特性时为 1）
		float sampler_max_anisotropy(void) const;
		//采样器 LOD 偏置绝对值上限（设备 `maxSamplerLodBias`；未初始化时为 0）
		float sampler_max_lod_bias(void) const;
		//当前交换链尺寸（窗口缩放后可能变化）
		uint32_t extent_width(void) const;
		uint32_t extent_height(void) const;
		/*
		请求改变窗口客户区尺寸（像素）
		成功返回真并置「交换链待重建」标志，交换链重建在下一次 frame_render 内完成；
		使「尺寸变化 → 交换链重建」可由上层驱动、可被用例构造。未初始化 / 尺寸为零时返回假。
		*/
		bool window_size_set(uint32_t width, uint32_t height);
		/*
		全屏切换（第一百一十一轮）
		成功返回真并置「交换链待重建」标志；隐藏窗口下后端可能拒绝，失败返回假。
		未初始化时返回假。
		*/
		bool window_fullscreen_set(bool fullscreen);
		//当前是否处于全屏（未初始化时为假）
		bool window_fullscreen(void) const;
		//窗口显示缩放比例（DPI；未初始化或查询失败时为 1）
		float display_scale(void) const;

		/*
		加载网格（顶点 / 索引缓冲上传至设备本地）
		失败返回假，句柄保持无效（0）。
		*/
		bool mesh_load(Mesh_Handle& handle_out, const Mesh_Data& data);
		/*
		加载带子网格分段的网格（第四百一十六轮）
		与 mesh_load 相同，但额外保存 sections（每段 first_index / index_count / material）；
		段校验在网格模块完成（索引数为 3 的倍数、区间不越界），任一不合法整次拒绝并返回假。
		注意（第四百一十六轮修正）：分段不作为 Mesh_Data 字段，避免与 EngineCore 同名 Mesh_Data 的 ODR 冲突。
		*/
		bool mesh_load_sections(Mesh_Handle& handle_out, const Mesh_Data& data,
			const Mesh_Section* sections, uint32_t count);
		/*
		从 OBJ 文件加载网格（第四百二十二轮；资产加载方向第一阶段）
		解析 v / vt / vn / f（多边形按扇形三角化、支持负数索引）与 usemtl 分组；
		缺纹理坐标填 (0,0)，缺法线按相邻三角形面积加权自动生成；
		成功时按 usemtl 分组建立子网格段（段序号即材质槽），可配合 mesh_draw_sections 逐段绑材质绘制。
		路径按 UTF-8 解释；失败返回假且句柄为 0（文件不存在 / 超限 / 语法非法 / 无有效三角面）。
		*/
		bool mesh_load_obj(Mesh_Handle& handle_out, const std::string& path);
		/*
		加载程序化三角形（第一百四十二轮新增）
		单个正面朝 +Z 的等腰三角形（3 顶点 / 3 索引）；仅需已初始化，无参数校验。
		*/
		bool mesh_triangle_load(Mesh_Handle& handle_out);
		/*
		加载程序化立方体（第一百四十二轮新增）
		half_extent 为正（边长 = 2 × half_extent，中心在原点）；每面独立法线，24 顶点 / 36 索引；
		非法参数整次拒绝（句柄置 0 并告警）。
		*/
		bool mesh_cube_load(Mesh_Handle& handle_out, float half_extent);
		/*
		加载程序化球体（第六十六轮新增）
		radius 为正；slices 经度分段 3..128、stacks 纬度分段 2..64；
		非法参数整次拒绝（句柄置 0 并告警）。法线朝外，UV 覆盖整球。
		*/
		bool mesh_sphere_load(Mesh_Handle& handle_out, float radius, uint32_t slices, uint32_t stacks);
		/*
		加载程序化正二十面体球（第一百八十九轮新增）
		radius 为正；subdivisions 细分次数 0..4（0 = 正二十面体 12 顶点 / 20 面，每级面数 ×4）；
		非法参数整次拒绝（句柄置 0 并告警）。顶点按球面归一化，法线朝外，UV 按球坐标展开；
		与 UV 球相比三角形更均匀（适合展示与物理近似）。
		*/
		bool mesh_icosphere_load(Mesh_Handle& handle_out, float radius, uint32_t subdivisions);
		/*
		加载程序化 XZ 平面细分网格（第一百九十三轮新增）
		width / depth 为正（世界单位，沿 X / Z、中心在原点）；segments_x / segments_z 分段数 1..512；
		顶点 (segments_x+1) × (segments_z+1)、索引 segments_x × segments_z × 6；
		法线恒 +Y、UV 覆盖 [0,1]；非法参数整次拒绝（句柄置 0 并告警）。适合作地形 / 水面底板。
		*/
		bool mesh_ground_grid_load(Mesh_Handle& handle_out, float width, float depth,
			uint32_t segments_x, uint32_t segments_z);
		/*
		加载程序化圆柱（第六十六轮新增）
		radius / height 为正（沿 Y 轴、中心在原点）；segments 圆周分段 3..128；
		非法参数整次拒绝（句柄置 0 并告警）。侧面法线径向，上下端盖法线朝 ±Y。
		*/
		bool mesh_cylinder_load(Mesh_Handle& handle_out, float radius, float height, uint32_t segments);
		/*
		加载程序化圆锥（第六十八轮新增）
		radius / height 为正（沿 Y 轴、中心在原点、锥尖朝 +Y）；segments 圆周分段 3..128；
		非法参数整次拒绝（句柄置 0 并告警）。侧面法线按锥面斜率倾斜，底面端盖法线朝 -Y。
		*/
		bool mesh_cone_load(Mesh_Handle& handle_out, float radius, float height, uint32_t segments);
		/*
		加载程序化四棱锥（第二百三十二轮新增）
		base_half 底面半边长（> 0）、height 总高（> 0，沿 Y 轴、中心在原点，锥尖朝 +Y）；
		非法参数整次拒绝（句柄置 0 并告警）。侧面与底面为逐面法线（硬边），外向绕序。
		*/
		bool mesh_pyramid_load(Mesh_Handle& handle_out, float base_half, float height);
		/*
		加载程序化胶囊（第六十九轮新增）
		radius 为正、height 为中段圆柱高度（可为 0 → 退化为球）；总高 = height + 2 × radius；
		segments 圆周分段 3..128、rings 单侧半球纬圈 2..64；
		非法参数整次拒绝（句柄置 0 并告警）。法线朝外，接缝 UV，外向绕序。
		*/
		bool mesh_capsule_load(Mesh_Handle& handle_out, float radius, float height,
			uint32_t segments, uint32_t rings);
		/*
		加载程序化平面（第七十一轮新增）
		width / height 为正（沿 X / Y，中心在原点，法线朝 +Z）；非法参数整次拒绝（句柄置 0 并告警）。
		*/
		bool mesh_plane_load(Mesh_Handle& handle_out, float width, float height);
		/*
		加载程序化圆盘（第八十三轮新增）
		radius 为正、segments 圆周分段 3..256；非法参数整次拒绝（句柄置 0 并告警）。
		圆盘位于 XY 平面、中心在原点、法线朝 +Z，外向（从 +Z 观察逆时针）绕序。
		*/
		bool mesh_disc_load(Mesh_Handle& handle_out, float radius, uint32_t segments);
		/*
		加载细分平面（第九十三轮新增）
		width / height 为正、columns / rows 为网格分段 1..256；
		顶点 (columns+1)×(rows+1)、索引 columns×rows×6，XY 平面、法线 +Z；非法参数整次拒绝。
		*/
		bool mesh_grid_load(Mesh_Handle& handle_out, float width, float height,
			uint32_t columns, uint32_t rows);
		/*
		查询网格顶点 / 索引数量（第九十三轮新增）
		成功写出（各出参可为空）并返回真；未初始化 / 句柄无效返回假且出参归零。
		*/
		bool mesh_counts(Mesh_Handle handle, uint32_t* vertex_count_out, uint32_t* index_count_out) const;
		/*
		加载程序化圆环（第六十七轮新增）
		major_radius / minor_radius 为正；双分段 3..128；非法参数整次拒绝（句柄置 0 并告警）。
		*/
		bool mesh_torus_load(Mesh_Handle& handle_out, float major_radius, float minor_radius,
			uint32_t major_segments, uint32_t minor_segments);
		//卸载网格
		void mesh_unload(Mesh_Handle handle);
		/*
		查询网格包围盒（第七十三轮新增；网格局部空间的轴对齐包围盒）
		成功写出 min / max 并返回真；未初始化 / 句柄无效返回假（出参不变）。
		*/
		bool mesh_bounds(Mesh_Handle handle, float min_out[3], float max_out[3]) const;
		/*
		更新网格顶点区域（第一百五十九轮新增）
		first_vertex 起、count 个顶点以 vertices 覆盖（结构须与创建时一致）；索引区不变。
		更新前会等尽在途帧；局部包围盒只扩不缩（保守），确保剔除不会误剔。
		未初始化 / 句柄无效 / 数据为空 / 区间越界返回假且内容不变。
		*/
		bool mesh_update_vertices(Mesh_Handle handle, uint32_t first_vertex, uint32_t count,
			const Vertex* vertices);
		/*
		更新网格索引区域（第一百六十轮新增）
		first_index 起、count 个索引以 indices 覆盖；每个索引须小于该网格顶点数，否则整次拒绝。
		更新前会等尽在途帧；顶点区与网格计数不变。
		*/
		bool mesh_update_indices(Mesh_Handle handle, uint32_t first_index, uint32_t count,
			const uint32_t* indices);
		/*
		绘制网格
		transform 为列主序 16 浮点模型矩阵，可为 nullptr 表示单位矩阵；
		color 为可选的 RGBA 颜色（长度 ≥ 4，分量 0..1）：非空时随绘制项推送常量下发（offset 64），
		仅对声明了该推送常量的片元（文字）有意义；为空时不推送常量（场景 / 后处理路径的既有行为）；
		本调用只登记一个待绘制项，实际绘制在下一帧 frame_render 时发生；
		同一帧内可多次调用以登记多个网格（各自独立的模型矩阵）。
		*/
		bool mesh_draw(Mesh_Handle handle, const float* transform, const float* color = nullptr);
		/*
		按索引区间绘制网格（第四百一十轮；物体多材质的分段绘制基础）
		first_index 与 index_count 描述该网格索引区内的一个区间（count 个 uint32 索引）；
		first_index == 0 且 index_count == 0 视为整段（等价于 mesh_draw，内部即以此转发）；
		区间越界（first_index ≥ 总索引数 或 first_index + index_count > 总索引数）整次拒绝并告警；
		其余语义（模型矩阵 / 颜色 / 剔除 / 合批 / 延迟登记）与 mesh_draw 一致。
		*/
		bool mesh_draw_range(Mesh_Handle handle, const float* transform, const float* color,
			uint32_t first_index, uint32_t index_count);
		/*
		多材质分段绘制（第四百一十轮；一个物体一次请求内使用多个材质）
		把网格索引区按 count 个材质均分（每段索引数按 3 对齐，避免切断三角形；余数归最后一段），
		逐段先 material_bind_set(materials[i]) 再按区间登记绘制，结束后恢复 material_bind_set(0)；
		materials 为空 / count 为 0 / 句柄无效 / 段数多于三角形数返回假且不登记任何绘制项；
		任一段登记失败时返回假（已登记的段不回滚，与 mesh_draw 的既有语义一致）。
		`instances`（第四百六十九轮；方向七阶段 C）非空时按段套用材质实例覆盖（tint / 自发光），
		长度须与 materials 相同（由调用方保证；句柄无效时不套用）。
		*/
		bool mesh_draw_sections(Mesh_Handle handle, const float* transform,
			const Material_Handle* materials, uint32_t count,
			const Material_Instance_Data* instances = nullptr);

		/*
		实例化绘制网格（同一网格 × N 个模型矩阵，一次绘制调用提交）
		transforms 为 count 个「列主序 16 浮点模型矩阵」的连续存储（共 count × 16 个浮点），可为 nullptr 表整批单位矩阵；
		count 为 0 或句柄无效时返回假，且**不登记任何绘制项**（不会留下半截绘制）；
		本调用只登记一个待绘制项（其内部为一个实例化绘制调用），实际绘制在下一帧 frame_render 时发生。
		*/
		bool mesh_draw_instanced(Mesh_Handle handle, const float* transforms, uint32_t count);

		/*
		纹理加载（RGBA8 紧排像素）
		成功返回真并写出纹理句柄；失败返回假，句柄保持 0。
		*/
		bool texture_load(uint64_t& handle_out, const uint8_t* pixels, uint32_t width, uint32_t height);
		/*
		从文件加载纹理（PNG / JPG / BMP 等经 stb 解码为 RGBA8 后上传）
		成功返回真并写出纹理句柄；文件不存在 / 为空 / 解码失败时返回假，句柄保持 0。
		*/
		bool texture_load_file(uint64_t& handle_out, const std::string& path);
		//程序生成棋盘格纹理
		bool texture_load_checkerboard(uint64_t& handle_out, uint32_t width, uint32_t height, uint32_t cell_size);
		/*
		程序生成恒等 3D LUT（第一百四十四轮新增）
		edge 为每轴纹素数（2..64），生成 RGBA8 的 2D 条带：宽 = edge × edge、高 = edge；
		恒等表输出等于输入，可作为自定义查找表的基线或通路验证。非法 edge 返回假且句柄保持 0。
		*/
		bool texture_load_lut_identity(uint64_t& handle_out, uint32_t edge);
		/*
		从内存像素创建立方体贴图（第一百二十五轮）
		six_faces 为 6 面 RGBA8 紧排（顺序 +X / -X / +Y / -Y / +Z / -Z），每面 width × height；
		用于天空盒背景与简易 IBL 采样；失败返回假且句柄保持 0。
		*/
		bool texture_cube_load(uint64_t& handle_out, const uint8_t* six_faces, uint32_t width, uint32_t height);
		/*
		天空盒 / 简易 IBL 开关（第一百二十五轮）
		启用后场景片元按法线方向与反射方向采样立方体贴图作环境补光，并在主场景 pass 末尾绘制全屏背景；
		默认关闭，关闭时画面与引入前逐字节一致。未初始化时静默忽略。
		*/
		void sky_enable_set(bool enabled);
		//当前是否启用天空盒（未初始化时为假）
		bool sky_enabled(void) const;
		//设置天空盒强度（ambient = 漫反射 / 环境补光、specular = 镜面；负值按 0 处理，默认均为 0）
		void sky_params_set(float ambient, float specular);
		//读取天空盒强度（出参可为空）
		void sky_params_get(float* ambient_out, float* specular_out) const;
		/*
		绑定天空盒立方体贴图（第一百二十五轮；0 = 内置纯白立方体）
		内部先等尽在途帧，再重写全部帧槽 set1 binding 5；句柄无效时保持原绑定。
		*/
		void sky_texture_set(uint64_t texture_handle);
		//当前天空盒立方体贴图句柄（0 = 内置纯白立方体）
		uint64_t sky_texture(void) const;
		//卸载纹理（句柄无效则忽略；绑定过该纹理的帧槽需由调用方重新 texture_bind_set）
		void texture_unload(uint64_t handle);
		/*
		读回纹理第 0 级像素（第二十七轮新增）
		先等尽在途帧（frames_wait）再同步读回；pixels 为 RGBA8 紧排（逐行自上而下），
		capacity 须不小于 宽×高×4；成功时 width_out / height_out（非空）回填纹理尺寸。
		未初始化 / 句柄无效 / 容量不足返回假（尺寸出参为 0）；读回不改变纹理内容与布局。
		*/
		bool texture_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		读回纹理第 0 级的指定子区域（第二十八轮新增）
		x / y 为左上角，区域须完整落在纹理内；pixels 为区域 RGBA8 紧排，容量 ≥ 区域宽×高×4。
		*/
		bool texture_readback_region(uint64_t handle, uint32_t x, uint32_t y,
			uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		更新纹理第 0 级的子区域（第一百五十一轮新增；第一百五十四轮补 mip 重建）
		pixels 为 RGBA8 紧排（每行 width 个纹素）；区域须完整落在纹理内，否则整次拒绝。
		更新前会等尽在途帧（避免采样中的图像被改写）；成功后纹理仍处于着色器可读布局，
		其余区域保持不变；含 mip 链的纹理会重建第 1..N 级以保持一致。
		*/
		bool texture_update_region(uint64_t handle, uint32_t x, uint32_t y,
			uint32_t width, uint32_t height, const uint8_t* pixels);
		/*
		更新纹理指定级的子区域（第一百五十八轮新增）
		level 须小于纹理的 mip 级数；x / y / width / height 按该级尺寸计，
		pixels 为 RGBA8 紧排（每行 width 个纹素）；区域须完整落在该级内，否则整次拒绝。
		更新前会等尽在途帧；成功后仅重建该级的下游（level+1 .. 末级），上游级不变。
		*/
		bool texture_update_region_level(uint64_t handle, uint32_t level, uint32_t x, uint32_t y,
			uint32_t width, uint32_t height, const uint8_t* pixels);
		/*
		读回纹理指定级的子区域（第一百六十六轮新增）
		x / y 按该级尺寸计，区域须完整落在该级内；pixels 为该区域 RGBA8 紧排。
		未初始化 / 级号无效 / 越界 / 容量不足返回假且尺寸出参归零。
		*/
		bool texture_readback_region_level(uint64_t handle, uint32_t level, uint32_t x, uint32_t y,
			uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		读回纹理指定级整幅像素（第一百五十八轮新增）
		pixels 为 RGBA8 紧排（逐行自上而下），capacity 须不小于 该级宽×高×4；
		成功时写出该级宽 / 高。未初始化 / 级号无效 / 容量不足返回假（尺寸出参为 0）。
		*/
		bool texture_readback_level(uint64_t handle, uint32_t level, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		查询纹理 mip 级数（第一百六十二轮新增；句柄无效 / 未初始化返回假且出参归零）
		*/
		bool texture_mip_levels(uint64_t handle, uint32_t* mip_levels_out) const;
		/*
		查询纹理指定级尺寸（第一百六十一轮新增）
		level 须小于纹理 mip 级数；成功写出该级宽 / 高（任一可为空）并返回真，
		否则返回假且两出参归零。
		*/
		bool texture_size_level(uint64_t handle, uint32_t level, uint32_t* width_out, uint32_t* height_out) const;
		/*
		查询纹理尺寸（第九十轮新增）
		成功时写出宽 / 高（任一非空）并返回真；未初始化 / 句柄无效返回假且出参归零。
		*/
		bool texture_size(uint64_t handle, uint32_t* width_out, uint32_t* height_out) const;
		//设置当前绘制所用纹理（传 0 或无效句柄则回落内置默认白纹理）
		void texture_bind_set(uint64_t texture_handle);
		/*
		设置纹理采样器状态（寻址模式 / 是否启用 mip 过滤 / LOD 范围与偏置 / 各向异性）
		lod_bias 钳到设备上限 `±maxSamplerLodBias`（非数归 0），正值为更模糊、负值为更锐利；
		采样器为模块共享且被烘焙进各帧槽描述符集，故本方法在切换状态后会**重新绑定当前纹理**，
		新状态自下一次绘制起生效；未初始化时静默返回。
		*/
		void texture_sampler_state_set(const Texture_Sampler_State& state);
		/*
		读取当前采样器状态（第七十七轮新增；只读诊断）
		返回门面归一后的生效值（各向异性已按设备能力裁剪、lod_bias 已钳制）；
		未初始化时返回默认状态（默认值即既有行为）。
		*/
		Texture_Sampler_State texture_sampler_state_get(void) const;

		/*
		登记材质（图形管线 ＋ 纹理 ＋ 混合档）
		data.pipeline 为 0 且 data.blend 为 Opaque 时沿用默认场景管线；
		data.pipeline 为 0 且 data.blend 非 Opaque 时，以默认场景着色器对（场景.vert / 场景.frag）登记一条具名管线；
		data.pipeline 非 0 时沿用该具名管线（其 blend 已在 pipeline_load 登记时确定）。
		成功写出句柄（≥ 1）；未初始化 / 具名管线登记失败时返回假、句柄保持 0、不留半初始化记录。
		*/
		bool material_load(Material_Handle& handle_out, const Material_Data& data);
		/*
		从 MTL 材质库创建材质（第四百二十三轮；资产加载方向第二阶段）
		解析 newmtl / Kd / Ks / Ke / Ns / d / Tr / map_Kd / map_Bump；
		换算口径：tint = Kd（第四分量为 d）、粗糙度 = √(2 / (Ns + 2))、自发光强度 = Ke 亮度、
		d < 1 时启用 alpha 裁剪（阈值 0.5）；map_Kd / map_Bump 相对 MTL 所在目录解码为纹理。
		句柄按文件内声明顺序写入 handles_out（与 OBJ 的 usemtl 顺序对齐，同名材质按名字自行匹配）。
		`名称输出`（第四百六十七轮；方向七阶段 B-B）非空时按同样顺序写出材质名，
		供关卡材质实例按「模板材质名」解析句柄。
		任一材质创建失败即整次回退（已建材质与纹理全部释放）并返回假。
		*/
		bool material_load_mtl(std::vector<Material_Handle>& handles_out, const std::string& path,
			std::vector<std::string>* names_out = nullptr);
		/*
		校验材质实例数据（第四百四十八轮；方向七阶段 A）
		规则：模板句柄须为已登记材质；`use_tint` 为真时 tint 四分量须有限且落在 [0,1]；
		`use_emissive` 为真时 emissive 须有限且落在 [0,1]。不建资源、不改变任何状态。
		*/
		bool material_instance_validate(const Material_Instance_Data& data) const;
		/*
		卸载材质（句柄无效则忽略）
		若材质自行登记了具名管线，则随卸载一并释放；卸载后其句柄失效。
		*/
		void material_unload(Material_Handle handle);
		/*
		设置后续绘制所用材质
		等价于 pipeline_bind_set(材质管线) ＋ 按材质纹理绑定（失败 / 为零回落内置默认白纹理）；
		同时按材质的 pbr_override 设置 / 清除逐物体 PBR 覆盖（第六十八轮）。
		handle 为 0 或无效句柄时回落「默认场景管线 ＋ 内置默认白纹理」并清除逐物体 PBR 覆盖。
		*/
		void material_bind_set(Material_Handle handle);
		/*
		设置逐物体 PBR 材质（第六十八轮）
		设置后，后续 mesh_draw / mesh_draw_instanced 的实例属性携带该材质：
		粗糙度缩放与金属度按物体生效（全局 pbr_params 仍须启用，PBR 未启用时本覆盖不参与）。
		roughness 合法域 (0.05, 4]、metallic 合法域 [0, 1]；越界 / 非数整次拒绝并返回假（状态不变）。
		*/
		bool material_pbr_set(float roughness, float metallic);
		/*
		清除逐物体 PBR 材质（第六十八轮）
		后续绘制回落到场景 UBO 的全局 pbr_params；未设置时为空操作。
		*/
		void material_pbr_clear(void);
		//当前是否启用逐物体 PBR 材质覆盖（第六十八轮）
		bool material_pbr_active(void) const;
		//当前逐物体材质粗糙度缩放（未启用时为 1；第六十八轮）
		float material_pbr_roughness(void) const;
		//当前逐物体材质金属度（未启用时为 0；第六十八轮）
		float material_pbr_metallic(void) const;
		/*
		设置逐物体清漆层（第二百三十九轮）
		设置后，后续 mesh_draw / mesh_draw_instanced 的推送块在 offset 112 携带该清漆参数，
		对全部直接光叠加一层独立粗糙度的 GGX 反射（材质中的同名字段优先，绑定材质时已覆盖本状态）。
		strength 合法域 (0, 1]、roughness 合法域 [0.03, 1]；越界 / 非数整次拒绝并返回假（状态不变）。
		*/
		bool material_clear_coat_set(float strength, float roughness);
		/*
		清除逐物体清漆层（第二百三十九轮）
		后续绘制回落「清漆关闭」（强度 0、粗糙度 0.15）；未设置时为空操作。
		*/
		void material_clear_coat_clear(void);
		//当前是否启用逐物体清漆层（第二百三十九轮）
		bool material_clear_coat_active(void) const;
		//当前逐物体清漆强度（未启用时为 0；第二百三十九轮）
		float material_clear_coat_strength(void) const;
		//当前逐物体清漆粗糙度（未启用时为 0.15；第二百三十九轮）
		float material_clear_coat_roughness(void) const;
		/*
		设置逐物体反照率色调（第六十九轮）
		设置后，后续**非文字**网格绘制把反照率乘上该色调（RGBA，各分量 0..1）；
		mesh_draw 显式给出的 color 形参优先于本吸附状态。
		任一分量越界 / 非数整次拒绝并返回假（状态不变）。
		*/
		bool material_tint_set(float red, float green, float blue, float alpha = 1.0f);
		/*
		清除逐物体反照率色调（第六十九轮）
		后续绘制回落白色（画面与未设置时一致）；未设置时为空操作。
		*/
		void material_tint_clear(void);
		//当前是否启用逐物体反照率色调（第六十九轮）
		bool material_tint_active(void) const;
		//读取当前逐物体色调（四个分量写入 out；未启用时为全 1；第六十九轮）
		void material_tint_get(float out[4]) const;
		/*
		设置逐物体自发光强度（第七十轮）
		设置后，后续绘制的实例属性携带自发光，片元着色器在受光结果上叠加 `反照率 × emissive`；
		合法域 [0, 8]，越界 / 非数整次拒绝并返回假。默认 0（无自发光，画面逐字节不变）。
		*/
		bool material_emissive_set(float emissive);
		//清除逐物体自发光（回落 0；第七十轮）
		void material_emissive_clear(void);
		//当前逐物体自发光强度（未初始化返回 0；第七十轮）
		float material_emissive(void) const;

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
		从 TTF 文件加载字体（字号 = 字形像素高度）
		字形被光栅化并打包为 RGBA8 字形图集（RGB 恒为白、A 为字形覆盖率），图集由纹理模块持有。
		成功写出句柄（≥ 1）；未初始化 / 文件不存在 / 为空 / 解析失败 / 图集创建失败时返回假、句柄保持 0。
		*/
		bool font_load_file(uint64_t& handle_out, const std::string& path, uint32_t pixel_height);
		//卸载字体（句柄无效则忽略；同时清除该字体的文本网格缓存）
		void font_unload(uint64_t handle);
		/*
		设置文字颜色（RGBA，分量 0..1；逐分量钳制：负值 / 非数归 0、大于 1 归 1）
		颜色由门面记录，自**下一次** text_draw / text_draw_layout 起生效（经推送常量 offset 64 下发）；
		未设置时默认白色 (1,1,1,1)；未初始化或 color 为空时静默返回。
		*/
		void text_color_set(const float color[4]);
		/*
		设置文字描边（第一百四十轮）
		uv_width 为描边半宽（图集 UV 单位，0 = 关闭；按 0..0.05 钳制），color 为描边色（分量 0..1 逐分量钳制）。
		自**下一次** text_draw / text_draw_layout 起生效（经推送常量 offset 80 下发）；width 为 0 时与旧版逐像素一致。
		未初始化或 color 为空时静默返回。
		*/
		void text_outline_set(float uv_width, const float color[3]);
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
		/*
		绘制文本（UTF-8 文本；**像素坐标**，原点在窗口左上、Y 向下，pixel_x / pixel_y 为文本左上角）
		内部：懒登记文字管线（Alpha 混合档）→ 绑字形图集为当前纹理 → 按 (字体, 文本) 缓存网格 →
		以「像素坐标 → NDC」矩阵登记一个绘制项；实际绘制在下一帧 frame_render 时发生。
		空文本返回真但不登记绘制项；未初始化 / 字体句柄无效 / 文本无可见字形时返回假。
		*/
		bool text_draw(uint64_t font, const std::string& text, float pixel_x, float pixel_y);
		/*
		度量单行文本（像素）
		宽度按字形推进累加、**不含尾随空白**；高度恒为字体像素高度。
		未初始化 / 字体句柄无效 / 文本无可见字形时两出参置 0 并返回假；出参指针可为空（该侧不写出）。
		*/
		bool text_measure(uint64_t font, const std::string& text, float* width_out, float* height_out) const;
		/*
		按布局绘制文本（UTF-8；像素坐标，原点在窗口左上、Y 向下，pixel_x / pixel_y 为**首行左上角**）
		换行：layout.max_width > 0 时逐字符断行（显式 '\n' 亦断行）；max_width == 0 时单行、align 无效果。
		对齐：以行为单位按 layout.align 平移（基准为 max_width）；行距取 layout.line_height（0 = 字体像素高度）。
		内部与 text_draw 共用「懒登记文字管线 → 绑图集 → 按 (字体, 文本, 布局) 缓存网格 → 登记绘制项」链路。
		空文本返回真但不登记绘制项；未初始化 / 字体句柄无效 / 文本无可见字形时返回假。
		*/
		bool text_draw_layout(uint64_t font, const std::string& text, float pixel_x, float pixel_y,
			const Text_Layout& layout);
		/*
		取该字体累计被跳过的码点数（诊断入口；未初始化 / 句柄无效返回 0）
		口径：只统计「补烘阶段判定为无法呈现」的码点（字体不含该码点、字形过宽、图集空间不足），
		按码点去重计（同一码点至多计一次），UTF-8 解码失败的字节不计；计数只增不减。
		*/
		uint64_t font_skipped_codepoints(uint64_t font) const;
		/*
		文字网格缓存命中 / 未命中次数（第七十五轮新增；自 init 起累计）
		同一（字体, 文本, 布局）第二次及以后绘制即计一次命中；缓存满淘汰不影响累计值。
		未初始化时均返回 0。
		*/
		uint64_t text_cache_hits(void) const;
		uint64_t text_cache_misses(void) const;
		/*
		文本网格缓存条目上限（第八十四轮新增）
		capacity 钳到 [1, 1024]（0 按 1）；缩容时立即淘汰最久未用项（网格延迟到无在途帧引用后归还）。
		默认 64。未初始化时 set 静默忽略、get 返回 0。
		*/
		void text_cache_capacity_set(uint32_t capacity);
		uint32_t text_cache_capacity(void) const;
		//当前文本网格缓存条目数（第八十四轮新增；只读诊断，未初始化为 0）
		uint32_t text_cache_size(void) const;

		//相机：设置位置
		void camera_position_set(float x, float y, float z);
		//相机：设置注视点
		void camera_target_set(float x, float y, float z);
		//相机：设置视场角（弧度）
		void camera_fov_set(float radians);
		//相机：设置近远平面
		void camera_near_far_set(float near_plane, float far_plane);
		/*
		切换正交 / 透视投影（第一百六十四轮新增）
		enabled 为真时改用正交投影（垂直可见高度 ortho_height 世界单位，宽按宽高比推出）；
		ortho_height 非正时忽略本次调用；未初始化时静默返回。
		*/
		void camera_ortho_set(bool enabled, float ortho_height);
		/*
		设置相机上方向（第一百八十六轮）
		(x, y, z) 为世界坐标上方向（默认 (0,1,0)）；全零或含非有限分量时整次忽略。
		数据侧对应 `Camera_Data::up` / `Scene_Data::up`。未初始化时静默返回。
		*/
		void camera_up_set(float x, float y, float z);
		/*
		设置宽高比覆盖（第一百八十六轮；0 = 跟随窗口）
		aspect > 0 时投影固定按该比例（离屏 / 非窗口比例输出）；≤ 0 或非有限值时回到跟随窗口。
		数据侧对应 `Camera_Data::aspect` / `Scene_Data::aspect`。
		*/
		void camera_aspect_set(float aspect);
		//查询当前生效宽高比（覆盖优先；未初始化返回 0；第一百八十六轮）
		float camera_aspect_get(void) const;
		//查询视场角（弧度；未初始化返回假且出参不变）
		bool camera_fov_get(float* radians_out) const;
		/*
		查询指定视深处可见的垂直世界高度（第一百六十五轮新增）
		透视：``2 · tan(fov/2) · 视深``；正交：恒为正交高度；视深非正按 0 处理。
		未初始化返回假且不改写出参；出参可为空（仅查询状态）。
		*/
		bool camera_world_height_at(float view_depth, float* height_out) const;
		//查询投影模式与正交高度（未初始化返回假且出参不变）
		bool camera_ortho_get(bool* enabled_out, float* height_out) const;
		/*
		世界点 → 屏幕坐标（第一百四十九轮新增）
		u / v 以左上角为原点（0..1 为视口内，允许越界），depth 为裁剪空间深度（0..1）。
		点位于相机背后（裁剪 w ≤ 0）或未初始化时返回假，出参保持原值。
		*/
		bool camera_project(const float world[3], float* u_out, float* v_out, float* depth_out) const;
		/*
		屏幕坐标 → 世界点（第一百四十九轮新增）
		与 camera_project 互逆；未初始化 / 矩阵不可逆 / 出参为空时返回假。
		*/
		bool camera_unproject(float u, float v, float depth, float world_out[3]) const;
		/*
		世界空间 AABB 是否与视锥相交（第一百六十轮新增）
		保守判定：任一面 8 角点全在外侧才判不可见（不会误剔）；未初始化 / 入参为空返回假。
		*/
		bool camera_aabb_visible(const float min[3], const float max[3]) const;
		/*
		导出相机「视图 × 投影」矩阵（第一百六十一轮新增）
		out 为列主序 16 浮点（同 UBO 布局，可直接用于外部几何 / 拾取计算）；
		未初始化或 out 为空返回假且不写出。
		*/
		bool camera_view_proj(float out[16]) const;

		/*
		应用场景描述（相机位置 / 注视点 / 视场角 / 近远平面 ＋ 清屏色）
		等价于逐项调用上方四个相机 setter 并设置清屏色；未初始化时静默返回。
		**参数校验**：视场角须落在 (0, π)、近平面须为正、远平面须大于近平面，任一非法即整次拒绝
		（记录告警并保持既有相机与清屏色不变）；清屏色分量一律钳制到 [0,1]（负 / 非数归 0）。
		*/
		void scene_apply(const Scene_Data& scene);
		/*
		应用场景描述（三组入参；第四十七轮新增）
		等价于把 camera / light / fog 三组分别提交：相机五项与清屏色、方向光与环境光、
		点 / 聚光灯组与高光 / 衰减模式 / 环境光颜色、距离雾。
		校验规则与 `scene_apply(const Scene_Data&)` 完全一致：相机参数非法即整次拒绝；
		单条光源非法只跳过该条；清屏色与雾参数按各 setter 的钳制 / 忽略规则处理。
		*/
		void scene_apply(const Camera_Data& camera, const Light_Data& light, const Fog_Data& fog);
		/*
		设置清屏色（RGBA）
		各分量钳制到 [0,1] 后生效（负 / 非数归 0、大于 1 归 1）；自下一次 frame_render 起生效；
		未初始化或指针为空时静默返回。
		*/
		void clear_color_set(const float color[4]);
		/*
		设置方向光与环境光（第二十九轮新增）
		direction 为**光传播方向**（指向场景内部；长度须非零，内部归一化），
		color 为光颜色（分量 0..1），intensity 为强度（≥ 0，与颜色相乘后使用），
		ambient 为环境光（0..1，直接叠加到照度）；照度最终钳制到 [0,1]。
		任一参数非法（空指针 / 零向量 / 非数 / 负强度 / 越界颜色）即**整次拒绝**并告警，
		保持既有光照不变；自下一次 frame_render 起生效。
		*/
		void light_set(const float direction[3], const float color[3], float intensity, float ambient);
		/*
		设置点光源（第三十一轮新增；与方向光二选一，后设者生效）
		position 为光源世界位置，radius 为线性衰减半径（> 0；距离达到半径时照度为 0），
		color / intensity / ambient 语义同 `light_set`。
		任一参数非法（空指针 / 半径非正或非数 / 颜色越界 / 强度或环境光越界）即整次拒绝并告警。
		*/
		void light_point_set(const float position[3], const float color[3], float radius, float intensity, float ambient);
		/*
		追加一个点光源（第三十二轮新增；最多 4 个，按登记顺序累加）
		不清空既有光源；超出上限、参数非法（空指针 / 半径非正 / 颜色越界 / 强度或环境光越界）时
		返回假并告警，既有光源保持不变。ambient 为全局环境光（多次调用取最后一次的合法值）。
		attenuation 为**逐光源距离衰减模式**（第四十六轮新增）：0 = 跟随全局（默认）、1 = 线性、2 = 平方反比；
		取值大于 2 时按 2 处理。
		specular 为**逐盏高光缩放**（第四十八轮新增）：1 = 跟随全局（默认）、0 = 该灯不产生高光；须落在 0..100。
		shininess 为**逐盏高光光泽度**（第五十一轮新增）：0 = 跟随全局（默认），> 0 为该灯自身光泽度（1..1000）。
		*/
		bool light_add_point(const float position[3], const float color[3], float radius, float intensity,
			float ambient, uint32_t attenuation = 0, float specular = 1.0f, float shininess = 0.0f);
		/*
		追加一个聚光灯（第三十四轮新增；与点光源同槽、共用 8 盏上限（第六十二轮扩为 8））
		direction 为**光传播方向**（从光源指向被照区域，长度须非零，内部归一化）；
		inner_deg / outer_deg 为内 / 外锥角（度）：须满足 0 < inner_deg < outer_deg ≤ 90，
		锥内照度不衰减、锥外为 0、两者之间按 smoothstep 平滑过渡。
		position / radius / color / intensity / ambient 语义同 `light_add_point`；
		attenuation 语义同 `light_add_point`（0 = 跟随全局、1 = 线性、2 = 平方反比）。
		specular 语义同 `light_add_point`（逐盏高光缩放，0..100）；shininess 语义同 `light_add_point`（逐盏光泽度，0..1000）。
		任一参数非法（空指针 / 方向退化 / 半径非正 / 颜色越界 / 锥角非法）或已满 8 盏时
		返回假并告警，既有光源保持不变。
		*/
		bool light_add_spot(const float position[3], const float direction[3], const float color[3],
			float radius, float intensity, float inner_deg, float outer_deg, float ambient,
			uint32_t attenuation = 0, float specular = 1.0f, float shininess = 0.0f);
		/*
		清空全部点光源并恢复方向光默认强度（第三十二轮新增）
		方向 / 颜色 / 环境光保持既有值；清空后画面回到「仅方向光」。
		*/
		void lights_clear(void);
		/*
		已登记点光源数量（第九十二轮新增；含聚光灯；未初始化返回 0）
		*/
		uint32_t point_light_count(void) const;
		/*
		读取第 index 盏点光源的状态快照（第九十二轮新增；只读诊断）
		成功写出状态并返回真；未初始化 / index 越界返回假（出参保持不变）。
		*/
		bool point_light_state_get(uint32_t index, Point_Light_State& state_out) const;
		/*
		光源记录快照（第二百六十五轮；阶段 D 数据底座）
		把当前光源写成 Light_Record 数组：索引 0 = 主方向光、1 = 补光，其后为逐盏点光 / 聚光；
		返回**写入条数**（≤ capacity；capacity 不足时按上述顺序截断），未初始化返回 0。
		out 为空且 capacity > 0 时返回 0（不写）。
		*/
		uint32_t light_records_get(Light_Record* out, uint32_t capacity) const;
		//当前光源记录总数（主光 ＋ 补光 ＋ 逐盏；不受 capacity 影响）
		uint32_t light_record_count(void) const;
		/*
		设置光源选择模式与预算（第六百四十五轮；A1）
		mode = 0 声明顺序（默认，与引入前逐字节一致）、1 = 重要性选择；其它取值按 0。
		budget = 0 跟随路径上限（旧前向 8 盏 / 簇路径全量），>0 为参与光照的光源预算（钳到容量上限）。
		选择在 scene_apply 时对「候选集合」生效，命中后仍按声明顺序登记（聚光槽位映射不变）。
		*/
		void light_select_set(uint32_t mode, uint32_t budget);
		/*
		设置镜面高光参数（第三十一轮新增；对方向光与点光源均生效）
		intensity 为高光强度（≥ 0，0 = 关闭），shininess 为光泽度（≥ 1，越大高光越集中）。
		任一参数非法（NaN / 强度为负 / 光泽度小于 1）即整次拒绝并告警。
		*/
		void specular_set(float intensity, float shininess);
		/*
		设置距离衰减模式（第三十六轮新增；对点光源与聚光灯共同生效）
		mode = 0 线性衰减（`clamp(1 − d / radius, 0, 1)`，默认）；mode = 1 平方反比（`1 / (1 + (d / radius)²)`）。
		仅接受 0 与 1；其它取值返回假并告警（保持原模式）。自下一次 frame_render 起生效。
		*/
		bool light_attenuation_set(uint32_t mode);
		//读取当前距离衰减模式（第三十六轮新增；未初始化返回 0）
		uint32_t light_attenuation_get(void) const;
		/*
		粒子绘制数据源（第三百零七轮迁入引擎核心层后的职责分离）
		渲染层**不持有也不推进**粒子模拟状态：模拟（发射 / 力场 / 碰撞 / 生命周期）在系统层与
		应用层完成，本层只负责把状态数组上传 GPU 并登记 billboard 绘制。
		绑定后才可绘制；未绑定 / 空指针 / 容量为 0 时各绘制入口返回假且不登记绘制项。
		*/
		/*
		绑定粒子绘制数据源
		states / alive / capacity 由应用层持有（长度须一致）；states 与 alive 均可写
		（GPU 推进回读会覆盖它们，活跃标记置 0 表示该索引已在 GPU 侧死亡，索引回收由调用方负责）。
		传入 nullptr 或 capacity 为 0 表示解绑（此后绘制入口不可用）。
		*/
		void particle_source_bind(Particle_State* states, uint8_t* alive, uint32_t capacity);
		//当前绑定容量（未绑定返回 0）
		uint32_t particle_source_capacity(void) const;
		//粒子路径是否可用（已初始化且计算路径可用即为真；第三百一十一轮）
		bool particle_supported(void) const;
		//开关（仅初始化后允许；关闭不释放容量，重新开启沿用绑定数据）
		bool particle_enable_set(bool enabled);
		//开关状态
		bool particle_enable_get(void) const;
		//GPU 缓冲容量（钳制 [1, 1 048 576]；容量变化重建 GPU 缓冲，不触碰绑定数据）
		bool particle_capacity_set(uint32_t capacity);
		uint32_t particle_capacity_get(void) const;
		//发射器（GPU 推进输入；参数非法整次拒绝，保持既有配置；须先开启）
		bool particle_emitter_set(const Particle_Emitter& emitter);
		//当前发射器快照（观测 / 应用层模拟用；未初始化返回默认构造值）
		Particle_Emitter particle_emitter_get(void) const;
		/*
		模拟统计回报（应用层模拟计数；只累加帧统计，不参与绘制）
		出生 / 死亡计数由模拟方（应用层 CPU 路径或 GPU 推进）上报，供帧统计观测。
		*/
		void particle_stats_add(uint32_t born, uint32_t died);
		//模拟参数（GPU 推进与外观输入；参数非法整次拒绝；须先开启）
		bool particle_params_set(const Particle_Params& params);
		/*
		当前模拟参数快照（第三百七十轮；观测用）
		返回最近一次被接受的参数（含软粒子档位 / 颜色 / 软边等外观字段）；
		未初始化返回默认构造值（各字段为 0）。
		*/
		Particle_Params particle_params_get(void) const;
		//GPU 推进是否就绪（需计算路径可用且粒子已开启；首次调用惰性创建缓冲与管线）
		bool particle_gpu_available(void);
		/*
		GPU 单步推进（第三百一十二轮；第三百一十三轮补生命周期）
		上传当前 CPU 镜像 → 推进着色器（重力 / 阻尼 / 积分 / 年龄）→ 回读覆盖 CPU 镜像；
		回读后按 `lifetime ≤ 0` 判定 GPU 侧死亡并同步回收镜像中的活跃标记。
		返回假表示路径不可用或步长非法。
		*/
		bool particle_gpu_step(float dt);
		//最近一次 GPU 单步判定的死亡数（第三百一十三轮；未开启 / 未推进为 0）
		uint32_t particle_gpu_died_last(void) const;
		//常驻 GPU 推进的活跃粒子数（只读观测；未开启常驻返回 0）
		uint32_t particle_gpu_active_count(void) const;
		/*
		粒子 billboard 绘制（第三百一十四轮）
		把当前活跃粒子按相机朝向四边形一次性实例化登记为**一个绘制项**（实际绘制在下一帧 frame_render）；
		size 为四边形世界边长；未开启 / 无活跃粒子 / 资源创建失败 / size 非法时返回假且不登记任何绘制项。
		*/
		bool particle_draw(float size);
		/*
		粒子混合档（第三百一十六轮；默认 Alpha）
		仅接受 Alpha / Additive；切换即释放旧粒子管线，下次 particle_draw 惰性重建（不改变既有画面行为）。
		*/
		bool particle_blend_set(Blend_Mode mode);
		Blend_Mode particle_blend_get(void) const;
		//粒子深度写入（第三百一十六轮；默认关闭；切换同样重建粒子管线）
		bool particle_depth_write_set(bool enabled);
		bool particle_depth_write_get(void) const;
		/*
		粒子深度分桶近似排序（第三百一十七轮；默认 0 = 关闭）
		0 = 按池索引顺序单次实例化；1..16 = 按视线距离分桶、桶间由远及近登记（桶内保持池索引顺序）。
		本层**不做逐粒子排序**：同桶粒子的叠加上限为整桶一次绘制，结果稳定（同一输入逐帧一致）。
		*/
		bool particle_sort_buckets_set(uint32_t buckets);
		uint32_t particle_sort_buckets_get(void) const;
		/*
		当前粒子 GPU 资源占用字节（第三百二十轮；只读观测；第三百五十二轮补间接命令缓冲）
		双缓冲（容量 × 32 × 2）＋ 力场缓冲（8 × 40）＋ 控制 / 紧凑索引与计数 ＋ 间接命令缓冲（按帧槽 20 B）；资源未创建时为 0。
		*/
		uint64_t particle_gpu_bytes(void) const;
		/*
		常驻推进开关（第三百二十七轮；默认关闭）
		为真时发射与活跃数由 GPU 侧「活跃位图 ＋ 环形写指针」维护，CPU 镜像不参与；
		计算路径不可用 / 未开启粒子时返回假。切换不重建状态缓冲（容量变化仍按既有规则重建）。
		*/
		bool particle_resident_set(bool enabled);
		bool particle_resident_get(void) const;
		/*
		常驻紧凑索引（第三百三十三轮；阶段 D；绘制机制的一部分，保留在渲染层）
		把活跃位图压缩为连续索引数组（供 GPU 侧绘制按 gl_InstanceIndex 取粒子），
		紧凑计数由原子自增产出；返回假表示未开启常驻 / 计算不可用。
		*/
		bool particle_gpu_compact(void);
		//最近一次紧凑结果的索引数（未紧凑 / 未开启常驻为 0）
		uint32_t particle_compact_count_get(void) const;
		/*
		回读紧凑索引（第三百三十三轮；诊断 / 测试用）
		写出前 max_count 项（按紧凑顺序 = 池索引升序），返回实际写出的项数。
		*/
		uint32_t particle_compact_indices_get(uint32_t* out, uint32_t max_count);
		/*
		粒子 GPU 绘制（第三百四十一轮；常驻模式专用）
		先紧凑索引，再以「紧凑索引 → 状态 → 着色器内 billboard」登记**一个不消费实例属性**的绘制项：
		顶点来自单位四边形，实例数 = 紧凑计数；相机位置与尺寸经推送常量下发。
		返回假表示未开启常驻 / 资源或管线不可用 / 无活跃粒子 / size 非法。
		*/
		bool particle_gpu_draw(float size);
		/*
		粒子 GPU 间接绘制（第三百四十二轮；常驻模式专用）
		紧凑索引 → 计算着色器把「紧凑计数」写成一条 VkDrawIndexedIndirectCommand →
		登记一个不消费实例属性的绘制项，录制阶段以**单条** vkCmdDrawIndexedIndirect 下达（实例数取自命令）。
		命令缓冲按帧槽划分，绘制前等待该槽上一轮帧完成，避免覆盖仍在读取的命令。
		返回假表示未开启常驻 / 资源或管线不可用 / 无活跃粒子 / size 非法。
		*/
		bool particle_indirect_draw(float size);
		/*
		粒子间接绘制命令回读（第三百四十二轮；诊断 / 测试）
		写出最近一次 particle_indirect_draw 写入的 5 个 uint32：
		index_count / instance_count / first_index / vertex_offset / first_instance。
		返回假表示未开启常驻 / 尚未写入 / 回读失败。
		*/
		bool particle_indirect_command_get(uint32_t command_out[5]);
		//GPU 推进自检（上传当前 CPU 状态 → 零步推进 → 回读比对；仅验证调度通路与拷贝语义）
		bool particle_gpu_probe(void);
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
		//当前聚光灯数量（第一百一十五轮；is_spot 为真的光源数）
		uint32_t spot_light_count(void) const;
		//资源计数快照（第一百一十三 / 一百一十四轮；未初始化时全零）
		Renderer_Resource_Counts resource_counts(void) const;
		/*
		累计场景 UBO 上传字节数（第六十轮新增）
		场景 UBO 现按 16 字节单元比较、仅写入变化区间；本查询供上层观测每帧上传开销
		（静态场景下除各帧槽首次预热外应接近 0）。未初始化返回 0。
		*/
		uint64_t scene_upload_bytes(void) const;
		/*
		PBR-lite 镜面开关（第六十四轮新增）
		开启后四类光源的高光改用 GGX（粗糙度由逐盏光泽度换算），关闭时沿用 Blinn-Phong；
		默认关闭，故既有画面逐字节不变。
		*/
		void pbr_enable_set(bool enabled);
		//PBR-lite 是否启用（未初始化时为默认 false）
		bool pbr_enabled(void) const;
		/*
		PBR-lite 粗糙度缩放（第六十四轮新增；合法 0.05..4，越界整次忽略）
		最终粗糙度 = clamp(sqrt(2 / (shininess + 2)) × 缩放, 0.03, 1)；默认 1。
		*/
		void pbr_roughness_set(float scale);
		//当前 PBR-lite 粗糙度缩放（未初始化时为默认 1）
		float pbr_roughness(void) const;
		/*
		PBR 金属度（第六十五轮新增；合法 0..1，越界整次忽略；默认 0）
		PBR 开启时 F0 由 0.04 向反照率插值，漫反射按 (1 − metallic) 做能量守恒；关闭时不生效。
		*/
		void pbr_metallic_set(float metallic);
		//当前 PBR 金属度（未初始化时为默认 0）
		float pbr_metallic(void) const;
		/*
		二次元风格化 · 光照色阶量化与边缘光（第一百七十四轮新增）
		steps：色阶数（0 = 关闭，原样光照；1..8 = 把各光源的 N·L 量化到 N 档，形成硬边明暗分界）；
		rim：菲涅尔边缘光强度（0 = 关闭；0..1，轮廓处叠加反照率亮边）。
		默认 (0, 0) 使画面与引入前逐字节不变；数据侧对应 `Light_Data::toon_steps` / `toon_rim`。
		*/
		void toon_set(uint32_t steps, float rim);
		//当前色阶数（0 = 关闭；未初始化时为 0）
		uint32_t toon_steps(void) const;
		//当前边缘光强度（0 = 关闭；未初始化时为 0）
		float toon_rim(void) const;
		/*
		设置二次元暗部色调（第一百七十八轮新增）
		rgb 为暗部（主方向光量化受光系数低于 0.5 的区域）乘用的色调，各分量 0..1（非有限值按 1）；
		(1,1,1) 为关闭（乘性恒等），默认值即 (1,1,1)，故不调用时画面逐字节不变。
		数据侧对应 `Light_Data::toon_shadow_tint` / `Scene_Data::toon_shadow_tint`。
		*/
		void toon_shadow_tint_set(float r, float g, float b);
		//读取暗部色调（第一百七十八轮新增；未初始化写默认 (1,1,1)）
		void toon_shadow_tint(float out[3]) const;
		/*
		设置卡通高光阈值（第一百七十九轮新增）
		threshold 为高光亮度二值化阈值（0 = 关闭；0..1）：高于阈值的高光整块保留、低于阈值整块归零，
		形成硬边高光块；默认 0 使画面与引入前逐字节不变。数据侧对应 `Light_Data::toon_specular`。
		*/
		void toon_specular_set(float threshold);
		//读取卡通高光阈值（第一百七十九轮新增；未初始化返回 0）
		float toon_specular(void) const;
		/*
		设置反向外扩描边（第二百二十八轮）
		width 为参考距离（3 世界单位）处沿顶点法线的外扩宽度（0 = 关闭；0..1，越界钳制，非有限值按 0）；
		更远处的顶点按「视距 / 3」等比放大外扩量（第二百三十轮），使轮廓的屏幕像素宽度基本恒定。
		color 为描边色（RGB 0..1，非有限分量按 0）。开启后每个场景绘制项额外登记一条描边项
		（剔除正面、只写外轮廓），故 `Frame_Stats::draw_items` 相应增加。
		法线为零向量的顶点（如平面）不外扩；文字绘制不参与描边；描边管线登记失败时本设置无效。
		数据侧对应 `Light_Data::outline_width` / `outline_color`。
		*/
		void outline_set(float width, const float color[3]);
		//关闭反向外扩描边（等价于 outline_set(0, 任意色)；第二百二十八轮）
		void outline_clear(void);
		//当前外扩宽度（未初始化返回 0；第二百二十八轮）
		float outline_width(void) const;
		//读取当前描边色（未初始化写 (0,0,0)；第二百二十八轮）
		void outline_color(float out[3]) const;
		/*
		累计实例缓冲上传字节数（第六十一轮新增）
		实例缓冲现按「一个实例 = 80 字节」比较、仅写入变化区间（含阴影 pass 的直接写入）；
		静态场景下除各帧槽首次预热外应接近 0。未初始化返回 0。
		*/
		uint64_t instance_upload_bytes(void) const;
		/*
		读回阴影图（第五十七轮新增）
		输出为阴影目标的 RGBA8 打包深度（非线性的可视深度值，空区域为全 0），
		逐行自上而下紧排、无行填充；capacity 须不小于「边长 × 边长 × 4」。
		仅阴影开启时可用；读回前会等尽在途帧，读回后颜色自动回到着色器可读布局。
		*/
		bool shadow_readback(uint8_t* pixels, uint32_t capacity, uint32_t* width_out, uint32_t* height_out);
		/*
		设置补光（第四十七轮新增；第二盏方向光）
		direction 为光传播方向（长度须非零，内部归一化），color 为颜色（分量 0..1），intensity 为强度（0 = 关闭），
		specular 为逐光高光缩放（第五十轮新增；0..100，1 = 跟随全局，0 = 该光不产生高光）。
		补光只参与漫反射累加与高光取最大，与主方向光互不影响；任一参数非法即整次拒绝并告警。
		*/
		void light_fill_set(const float direction[3], const float color[3], float intensity, float specular = 1.0f);

		/*
		UI 通道（供上层接入 ImGui Vulkan 后端）
		返回渲染层持有的 UI 通道（完整定义见 渲染器_UI通道.h，含 Vulkan / SDL 类型，
		需上层自行包含该扩展头）；未初始化时返回空。
		*/
		Ui_Channel_Context* ui_channel(void);
	private:
		//实现体（把 SDL3 / Vulkan / VMA 全部关在 .cpp 里）
		class Impl;
		std::unique_ptr<Impl> impl;
		//按需创建并绑定聚光灯阴影瓦片 1..max_tiles-1（scene_apply 与运行期开关共用）
		void spot_shadow_tiles_ensure(std::size_t max_tiles);
		//层内 GPU 粒子推进（实现位于 EngineSystem/src/effect/Particle/粒子GPU推进.cpp）
		friend uint32_t particle_gpu::emit(Renderer&, uint32_t);
		friend bool particle_gpu::step_resident(Renderer&, float);
	};
}
