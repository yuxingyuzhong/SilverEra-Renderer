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
	主机侧计时区间为「录制 ＋ 提交 ＋ 呈现调用」，不含 GPU 时间戳（本版次不引入 GPU 计时）。
	*/
	struct Frame_Stats
	{
		//已推进帧数
		uint64_t frames = 0;
		//最近一帧主机侧耗时（毫秒）
		double frame_ms = 0.0;
		//累计绘制项数（含同帧多 pass 路径的后处理绘制项）
		uint64_t draw_items = 0;
		//累计实例数
		uint64_t instances = 0;
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
		本调用只登记一个待绘制项，实际绘制在下一帧 frame_render 时发生；
		同一帧内可多次调用以登记多个网格（各自独立的模型矩阵）。
		*/
		bool mesh_draw(Mesh_Handle handle, const float* transform);

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
		要求目标范围与交换链范围一致（后处理按帧缓冲坐标 1:1 取源，故通常以当前交换链尺寸创建目标）。
		句柄无效时回落原路径（不报错）。
		*/
		void render_target_present_set(uint64_t handle);
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
		绘制文本（UTF-8 文本；**像素坐标**，原点在窗口左上、Y 向下，pixel_x / pixel_y 为文本左上角）
		内部：懒登记文字管线（Alpha 混合档）→ 绑字形图集为当前纹理 → 按 (字体, 文本) 缓存网格 →
		以「像素坐标 → NDC」矩阵登记一个绘制项；实际绘制在下一帧 frame_render 时发生。
		空文本返回真但不登记绘制项；未初始化 / 字体句柄无效 / 文本无可见字形时返回假。
		*/
		bool text_draw(uint64_t font, const std::string& text, float pixel_x, float pixel_y);

		//相机：设置位置
		void camera_position_set(float x, float y, float z);
		//相机：设置注视点
		void camera_target_set(float x, float y, float z);
		//相机：设置视场角（弧度）
		void camera_fov_set(float radians);
		//相机：设置近远平面
		void camera_near_far_set(float near_plane, float far_plane);

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
