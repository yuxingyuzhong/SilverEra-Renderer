#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
/*
网格数据（Vulkan-free）
公共面只暴露引擎语义类型（Vertex / Mesh_Data / Mesh_Handle），
其定义置于 Mesh 模块的共用头，层外看不到任何 Vulkan / VMA / SDL3 / glm 类型。
*/
#include "Application/Renderer/src/render/Mesh/网格数据.h"
//管线公共语义类型（Vulkan-free）：混合档
#include "Application/Renderer/src/render/Pipeline/管线数据.h"
//纹理公共语义类型（Vulkan-free）：采样器状态
#include "Application/Renderer/src/render/Texture/纹理数据.h"
//材质公共语义类型（Vulkan-free）：材质描述与句柄
#include "Application/Renderer/src/render/Pipeline/材质数据.h"
//场景公共语义类型（Vulkan-free）：场景描述
#include "Application/Renderer/src/render/Scene/场景数据.h"
//文字布局公共语义类型（Vulkan-free）：对齐方式与换行宽度
#include "Application/Renderer/src/render/Text/文字布局数据.h"

//游戏引擎命名空间
namespace engine
{
	//UI 通道（扩展公共头 渲染器_UI通道.h 定义；此处仅前置声明，保持本公共头零 Vulkan / SDL 类型）
	class Ui_Channel_Context;

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
		//着色器目录（空 → 相对可执行文件目录的 "shaders"）
		std::string shader_dir = "";
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
		//累计主机侧耗时（毫秒；自 init 以来各帧 frame_ms 之和）
		double frame_total_ms = 0.0;
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
	};

	/*
	渲染器（门面）
	生命周期：init → frame_render × N → shutdown
	状态机：未初始化 → 已初始化 → 已销毁（shutdown 幂等，可重复调用）
	非线程安全：要求所有调用来自同一线程（见 §4.5）
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
		是否支持 GPU 时间戳（设备与图形队列族均支持时为真；未初始化时为假）
		为假时帧统计的 gpu_frame_ms 恒 0（已优雅降级，不影响渲染）。
		*/
		bool gpu_timestamp_supported(void) const;
		//是否支持像素回读（交换链图像具备传输源用法；未初始化时为假）
		bool readback_supported(void) const;
		//是否支持采样器各向异性过滤（设备创建时按特性支持情况启用；未初始化时为假）
		bool sampler_anisotropy_supported(void) const;
		//采样器最大各向异性倍数（设备上限；未初始化或未启用该特性时为 1）
		float sampler_max_anisotropy(void) const;
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
		加载网格（顶点 / 索引缓冲上传至设备本地）
		失败返回假，句柄保持无效（0）。
		*/
		bool mesh_load(Mesh_Handle& handle_out, const Mesh_Data& data);
		//卸载网格
		void mesh_unload(Mesh_Handle handle);
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
		//设置当前绘制所用纹理（传 0 或无效句柄则回落内置默认白纹理）
		void texture_bind_set(uint64_t texture_handle);
		/*
		设置纹理采样器状态（寻址模式 / 是否启用 mip 过滤）
		采样器为模块共享且被烘焙进各帧槽描述符集，故本方法在切换状态后会**重新绑定当前纹理**，
		新状态自下一次绘制起生效；未初始化时静默返回。
		*/
		void texture_sampler_state_set(const Texture_Sampler_State& state);

		/*
		登记材质（图形管线 ＋ 纹理 ＋ 混合档）
		data.pipeline 为 0 且 data.blend 为 Opaque 时沿用默认场景管线；
		data.pipeline 为 0 且 data.blend 非 Opaque 时，以默认场景着色器对（场景.vert / 场景.frag）登记一条具名管线；
		data.pipeline 非 0 时沿用该具名管线（其 blend 已在 pipeline_load 登记时确定）。
		成功写出句柄（≥ 1）；未初始化 / 具名管线登记失败时返回假、句柄保持 0、不留半初始化记录。
		*/
		bool material_load(Material_Handle& handle_out, const Material_Data& data);
		/*
		卸载材质（句柄无效则忽略）
		若材质自行登记了具名管线，则随卸载一并释放；卸载后其句柄失效。
		*/
		void material_unload(Material_Handle handle);
		/*
		设置后续绘制所用材质
		等价于 pipeline_bind_set(材质管线) ＋ 按材质纹理绑定（失败 / 为零回落内置默认白纹理）。
		handle 为 0 或无效句柄时回落「默认场景管线 ＋ 内置默认白纹理」。
		*/
		void material_bind_set(Material_Handle handle);

		/*
		设置后续绘制所用混合档（Opaque / Alpha）
		切档会触发一次图形管线重建（内部先等设备空闲），故应在帧与帧之间调用而非帧内；
		同档重复设置为空操作；重建失败时记录错误并保持档位记录不变（不改变帧循环时序）。
		注意：本方法只作用于**默认场景管线**；具名管线的混合档在 pipeline_load 登记时确定，不受影响。
		*/
		void blend_mode_set(Blend_Mode mode);

		/*
		登记具名图形管线（顶点 / 片元着色器文件名 ＋ 混合档）
		成功写出句柄（≥ 1）；具名管线与默认场景管线共享同一管线布局与描述符集布局，
		仅「着色器对 ＋ 混合档」不同；未初始化 / 着色器加载失败 / 管线创建失败时返回假、句柄保持 0。
		*/
		bool pipeline_load(uint64_t& handle_out, const std::string& vert_shader, const std::string& frag_shader,
			Blend_Mode blend_mode = Blend_Mode::Opaque);
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
		启用 / 关闭视锥剔除（第二十五轮新增；默认开启）
		关闭后网格绘制项一律照常登记（含完全在视锥外的项），culled_items 不再递增；
		用于调试 / 对照与「上层自行剔除」的场景。未初始化时静默忽略。
		*/
		void cull_enabled_set(bool enabled);
		//查询视锥剔除是否启用（未初始化返回默认值 true）
		bool cull_enabled(void) const;
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

		//相机：设置位置
		void camera_position_set(float x, float y, float z);
		//相机：设置注视点
		void camera_target_set(float x, float y, float z);
		//相机：设置视场角（弧度）
		void camera_fov_set(float radians);
		//相机：设置近远平面
		void camera_near_far_set(float near_plane, float far_plane);

		/*
		应用场景描述（相机位置 / 注视点 / 视场角 / 近远平面 ＋ 清屏色）
		等价于逐项调用上方四个相机 setter 并设置清屏色；未初始化时静默返回。
		**参数校验**：视场角须落在 (0, π)、近平面须为正、远平面须大于近平面，任一非法即整次拒绝
		（记录告警并保持既有相机与清屏色不变）；清屏色分量一律钳制到 [0,1]（负 / 非数归 0）。
		*/
		void scene_apply(const Scene_Data& scene);
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
		*/
		bool light_add_point(const float position[3], const float color[3], float radius, float intensity, float ambient);
		/*
		清空全部点光源并恢复方向光默认强度（第三十二轮新增）
		方向 / 颜色 / 环境光保持既有值；清空后画面回到「仅方向光」。
		*/
		void lights_clear(void);
		/*
		设置镜面高光参数（第三十一轮新增；对方向光与点光源均生效）
		intensity 为高光强度（≥ 0，0 = 关闭），shininess 为光泽度（≥ 1，越大高光越集中）。
		任一参数非法（NaN / 强度为负 / 光泽度小于 1）即整次拒绝并告警。
		*/
		void specular_set(float intensity, float shininess);

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
	};
}
