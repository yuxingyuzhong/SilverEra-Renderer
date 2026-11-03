#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
/*
Vulkan 头
本头属本层「实现细节」：只被本层 .cpp 与层内模块头包含，不进公共头，
故可以合法暴露 Vulkan 类型（层外仍看不到 —— 见 cmake/对外接口.cmake 的私有包含根）。
*/
#include <vulkan/vulkan.h>
//管线公共语义类型（Vulkan-free）：混合档
#include "Application/Renderer/src/render/Pipeline/管线数据.h"

//游戏引擎命名空间
namespace engine
{
	//单个实例的浮点数（模型矩阵 16 ＋ 材质 4 ＋ 逐物体卡通 4 ＋ 逐物体暗部色调 4；第四百零八 / 四百零九轮）
	constexpr uint32_t INSTANCE_FLOAT_COUNT = 28;
	//单个实例的字节步长（图形管线实例绑定步长与渲染层实例缓冲共用，避免两侧漂移）
	constexpr uint32_t INSTANCE_STRIDE_BYTES = INSTANCE_FLOAT_COUNT * 4;
	//实例材质在单个实例内的浮点偏移（模型矩阵之后）
	constexpr uint32_t INSTANCE_MATERIAL_OFFSET = 16;
	//逐物体卡通参数在单个实例内的浮点偏移（实例材质之后；第四百零八轮）
	constexpr uint32_t INSTANCE_TOON_OFFSET = 20;
	//逐物体卡通暗部色调在单个实例内的浮点偏移（卡通参数之后；第四百零九轮）
	constexpr uint32_t INSTANCE_TOON_TINT_OFFSET = 24;

	//图形管线配置
	struct Pipeline_Config
	{
		//颜色附件格式（交换链格式）
		VkFormat color_format = VK_FORMAT_UNDEFINED;
		//深度附件格式
		VkFormat depth_format = VK_FORMAT_UNDEFINED;
		//在途帧数（描述符集按帧槽分配）
		uint32_t frame_overlap = 2;
		//多重采样数（第九十六轮；1 = 关闭，2/4/8 = MSAA；所有跟随配置的管线按此烘焙）
		uint32_t sample_count = 1;
		/*
		推送常量块大小（第四百零一轮）
		144 = 在 128 之后再留一组 vec4（后处理逐级 extra，offset 128..143）；
		设备 maxPushConstantsSize < 144 时由门面降为 128（此时该组不可用，写入接口拒绝）。
		*/
		uint32_t max_push_constant_size = 128;
	};

	/*
	管线条目
	一条图形管线 = 顶点 / 片元着色器模块 ＋ 混合档（颜色 / 深度格式由配置统一烘焙）。
	句柄 0 保留给「默认场景管线」，具名管线句柄自 1 起由本模块分配。
	着色器模块所有权一律在调用方（门面）：本模块只借用，销毁时不在内部释放。
	*/
	struct Pipeline_Entry
	{
		//句柄（0 = 默认场景管线；≥ 1 = 具名管线）
		uint64_t handle = 0;
		//顶点着色器模块（所有权在调用方）
		VkShaderModule vert_module = VK_NULL_HANDLE;
		//片元着色器模块（所有权在调用方）
		VkShaderModule frag_module = VK_NULL_HANDLE;
		//混合档（登记时确定）
		Blend_Mode blend_mode = Blend_Mode::Opaque;
		/*
		颜色附件格式（第五十四轮新增）
		VK_FORMAT_UNDEFINED = 跟随管线上下文的 config.color_format（既有行为）；
		非未定义 = 该管线固定按此格式创建（阴影管线用 R8G8B8A8_UNORM 的阴影目标）。
		*/
		VkFormat color_format = VK_FORMAT_UNDEFINED;
		/*
		是否关闭背面剔除（第五十四轮新增）
		阴影 pass 从光源视角观察，与相机视角的可见绕序相反；若沿用背面剔除，
		朝光源的正面会被判为背面而整体剔除，阴影图将只剩清屏值。
		*/
		bool cull_none = false;
		/*
		是否改为剔除正面（第二百二十八轮）
		反向外扩描边沿法线把几何外扩后只保留背面（正面被剔除），使外扩轮廓落在物体之外；
		cull_none 优先（两者同时为真时按「不剔除」处理）。
		*/
		bool cull_front = false;
		/*
		采样数覆盖（第九十六轮）
		0 = 跟随管线上下文的 config.sample_count（主场景 / 后处理管线）；
		非 0 = 该管线固定按此采样数创建（阴影管线固定 1，其目标为单采样）。
		*/
		uint32_t sample_count = 0;
		/*
		顶点输入位置位集（第九十七轮）
		位 i = 声明 location i；只声明着色器真正消费的属性，避免校验层「属性未被消费」告警。
		0xFF = 全量（0..7）；0 = 同 0xFF（解析失败时的保守回落）。
		*/
		uint32_t input_locations = 0xFF;
		/*
		深度状态（第一百二十五轮）
		depth_write = 关闭深度写入（天空盒等只读深度参与的管线）；
		depth_lequal = 比较算子改用 LESS_OR_EQUAL（配合 z = 1 的全屏天空绘制，只在深度仍为清屏值处着色）。
		*/
		bool depth_write = true;
		bool depth_lequal = false;
		/*
		覆盖率写入（第一百二十六轮）
		alphaToCoverageEnable = 真时，片元 alpha 转化为多重采样覆盖率（仅采样数 > 1 的管线有效）；
		采样数为 1 时自动关闭，等价于既有 alpha 混合路径。
		*/
		bool alpha_to_coverage = false;
		//图形管线
		VkPipeline pipeline = VK_NULL_HANDLE;
	};

	/*
	管线上下文
	持有描述符集布局、管线布局、描述符池与若干图形管线；
	set0 = 场景 UBO / set1 = 采样器，**全部图形管线共享同一管线布局**。
	生命周期：build → pipeline_build / pipeline_register → 使用 → destroy；destroy 幂等，析构兜底。
	*/
	class Pipeline_Context
	{
	public:
		//构造（不创建任何 Vulkan 对象）
		Pipeline_Context(void) = default;
		//析构（内部调用 destroy，保证资源不泄漏）
		~Pipeline_Context(void);
		Pipeline_Context(const Pipeline_Context&) = delete;
		Pipeline_Context& operator=(const Pipeline_Context&) = delete;

		//构建（建立描述符集布局 set0=场景UBO / set1=采样器、管线布局、描述符池并分配每帧槽描述符集）
		bool build(VkDevice device, const Pipeline_Config& config);
		//销毁（幂等；销毁全部图形管线与基础设施）
		void destroy(void);
		//是否已构建完成
		bool valid(void) const;

		/*
		登记 / 替换并激活句柄 0「默认场景管线」
		顶点输入 position vec3 + uv vec2；实例输入 mat4 模型矩阵；推送常量 mat4；深度测试开 / 写开；背面剔除 BACK。
		*/
		bool pipeline_build(VkShaderModule vert_module, VkShaderModule frag_module, Blend_Mode blend_mode = Blend_Mode::Opaque,
			uint32_t input_locations = 0xFF);
		/*
		登记具名图形管线
		句柄自 1 起递增且稳定不变；不改变当前激活管线；成功时写出句柄。
		具名管线与默认管线共享同一管线布局与描述符集布局，仅允许「着色器对 + 混合档」不同。
		*/
		bool pipeline_register(uint64_t& handle_out, VkShaderModule vert_module, VkShaderModule frag_module, Blend_Mode blend_mode = Blend_Mode::Opaque,
			VkFormat color_format = VK_FORMAT_UNDEFINED, bool cull_none = false, uint32_t sample_count = 0,
			uint32_t input_locations = 0xFF, bool depth_write = true, bool depth_lequal = false,
			bool alpha_to_coverage = false, bool cull_front = false);
		//切换当前激活管线（句柄不存在则返回假并保持激活不变）
		bool pipeline_activate(uint64_t handle);
		//注销具名管线（句柄 0 忽略；一并销毁其图形管线，不触碰着色器模块 —— 所有权在调用方）
		void pipeline_release(uint64_t handle);
		/*
		按新颜色 / 深度格式与采样数重建全部已登记图形管线
		颜色 / 深度格式被烘焙进 VkPipelineRenderingCreateInfo、采样数被烘焙进 VkPipelineMultisampleStateCreateInfo，
		交换链换格式或 MSAA 采样数变化后所有管线都必须重建；
		着色器模块与各自混合档保留；三者均未变时为空操作（无等待、无重建开销）。
		*/
		bool pipeline_rebuild(VkFormat color_format, VkFormat depth_format, uint32_t sample_count = 1);
		//重建句柄 0 默认管线的混合档（供门面 blend_mode_set；无默认管线时为空操作）
		bool pipeline_default_blend_set(Blend_Mode blend_mode);
		//已登记图形管线数量（第一百一十三轮；含默认管线）
		uint32_t pipeline_count(void) const;
		//句柄是否已登记
		bool pipeline_has(uint64_t handle) const;

		//获取当前激活图形管线
		VkPipeline pipeline_get(void) const;
		/*
		获取当前激活图形管线的混合档（第一百三十轮）
		无激活管线时返回 Blend_Mode::Opaque；供门面标记「透明绘制项不参与合批」。
		*/
		Blend_Mode pipeline_blend_mode_get(void) const;
		//获取指定句柄的图形管线（未登记返回空；不改变当前激活管线）
		VkPipeline pipeline_by_handle_get(uint64_t handle) const;
		/*
		获取指定句柄图形管线实际烘焙的采样数（第九十八轮）
		条目固定采样数优先，否则为上下文配置值；未登记返回 1。
		供帧模块按级选择单采样 / 多重采样宿附件。
		*/
		uint32_t pipeline_sample_count(uint64_t handle) const;
		/*
		管线缓存（第一百零六轮）
		cache_set 用外部字节数据重建缓存（须在创建图形管线之前调用）；cache_get 取回缓存字节。
		缓存对象在 build 时创建、destroy 时销毁；图形管线创建均使用该缓存。
		*/
		bool cache_set(const void* data, std::size_t size);
		bool cache_get(std::vector<uint8_t>& bytes_out) const;
		//获取管线布局（全管线共享）
		VkPipelineLayout layout_get(void) const;
		//第 slot 帧槽的场景 UBO 描述符集（set 0）
		VkDescriptorSet scene_set_get(uint32_t slot) const;
		//第 slot 帧槽的采样器描述符集（set 1）
		VkDescriptorSet sampler_set_get(uint32_t slot) const;
		/*
		粒子专用场景集（第三百四十一轮；set 0 同布局、独立分配）
		绑定 0 = 场景 UBO、1 = 粒子状态 SSBO、2 = 紧凑索引 SSBO、3/4 = 占位（与簇缓冲同位但集合独立，
		故不覆盖场景 pass 的绑定）；供「粒子GPU.vert」在着色器内取状态并构造 billboard。
		*/
		VkDescriptorSet particle_set_get(uint32_t slot) const;
		//向第 slot 帧槽的粒子专用集写绑定 0..4（ubo / state / index；绑定 3、4 以 state / index 占位）
		bool particle_buffers_bind(uint32_t slot, VkBuffer ubo_buffer, VkBuffer state_buffer, VkBuffer index_buffer);
		/*
		每帧槽的后处理采样器集数量
		链式后处理中第 i 级的源 = 第 (i % 目标数) 个中间目标，故每帧槽需「目标数」个后处理集；
		目标数上限即本常量（4）：下标 0 恒为场景目标，1..3 为额外中间目标。
		*/
		static constexpr uint32_t POST_SET_PER_SLOT = 4;
		/*
		第 slot 帧槽的后处理采样器描述符集（set 1；index ∈ [0, POST_SET_PER_SLOT)）
		与 sampler_set_get 同布局，但独立分配：使「同帧多 pass」中，场景 pass 用 sampler_set
		采样场景纹理，后处理 pass 用 post_set 采样离屏目标，两 pass 互不覆盖对方绑定；
		链式后处理中 index = 级序 % 目标数，各级绑定各自的源图像。
		*/
		VkDescriptorSet post_set_get(uint32_t slot, uint32_t index = 0) const;
		//向第 slot 帧槽写场景 UBO（set 0）
		bool scene_bind(uint32_t slot, VkBuffer ubo_buffer);
		/*
		向第 slot 帧槽写簇数据存储缓冲（set 0 binding 1..4；第二百八十八轮）
		binding 1 = 光源记录、2 = 逐簇计数、3 = 逐簇排他偏移、4 = 光源列表（均只读）；
		Cluster 关闭时由门面传入同一占位缓冲，保证四条绑定始终有完整描述符。
		*/
		bool scene_cluster_bind(uint32_t slot, VkBuffer light_buffer, VkBuffer counts_buffer,
			VkBuffer offsets_buffer, VkBuffer list_buffer);
		//向第 slot 帧槽写采样器（set 1 binding 0；binding 1 由 shadow_bind 独立维护）
		bool texture_bind(uint32_t slot, VkSampler sampler, VkImageView view);
		/*
		逐绘制项采样器集（第一百七十四轮）
		每个帧槽预留 DRAW_SET_PER_SLOT 条与 set1 同布局的独立描述符集；
		draw_set_bind 一次性写入反照率、五张材质贴图、阴影图与天空盒，
		使同帧内不同绘制项可各自采样不同纹理（不再被最后一次绑定覆盖）。
		*/
		VkDescriptorSet draw_set_get(uint32_t slot, uint32_t index) const;
		bool draw_set_bind(uint32_t slot, uint32_t index, VkSampler sampler, VkImageView albedo_view,
			VkImageView normal_view, VkImageView roughness_view, VkImageView emissive_view,
			VkImageView ao_view, VkImageView metallic_view, VkImageView shadow_view, VkImageView sky_view,
			VkImageView spot_shadow_view);
		/*
		逐绘制项采样集的批量写入（第二百二十一轮，优化；第四百八十三轮增 binding 9；第五百零七轮 binding 9 扩为 4 张）
		每项的采样器与视图先收集到 Draw_Set_Views，再由 draw_sets_bind_batch 一次性
		提交 count × 13 条描述符写（9 条单值绑定 ＋ binding 9 的 4 个数组元素）。
		写入顺序与逐项调用完全一致（下标 0..count-1 对应 draw_set_get 的同一批集），故画面逐字节不变。
		*/
		struct Draw_Set_Views
		{
			//采样器（全部绑定共用）
			VkSampler sampler = VK_NULL_HANDLE;
			//反照率（binding 0；binding 2 也以它占位）
			VkImageView albedo = VK_NULL_HANDLE;
			//法线（binding 3）
			VkImageView normal = VK_NULL_HANDLE;
			//粗糙度（binding 4）
			VkImageView roughness = VK_NULL_HANDLE;
			//自发光（binding 6）
			VkImageView emissive = VK_NULL_HANDLE;
			//环境光遮蔽（binding 7）
			VkImageView ao = VK_NULL_HANDLE;
			//金属度（binding 8）
			VkImageView metallic = VK_NULL_HANDLE;
			//阴影图（binding 1）
			VkImageView shadow = VK_NULL_HANDLE;
			//天空盒（binding 5）
			VkImageView sky = VK_NULL_HANDLE;
			//聚光灯阴影图数组（binding 9 的 8 个数组元素；第五百零七轮 4 张，第五百一十三轮扩为 8 张）
			VkImageView spot_shadow[8] = {};
			/*
			场景深度（binding 2；第三百六十六轮）
			后处理用当前帧解析深度；粒子软粒子用 HiZ 历史深度（上一轮给 history 视图与最近邻采样器）。
			为空时回落反照率视图 ＋ 本项采样器（与旧行为逐字节一致）。
			*/
			VkImageView depth = VK_NULL_HANDLE;
			VkSampler depth_sampler = VK_NULL_HANDLE;
		};
		//批量写入第 slot 帧槽的前 count 条逐绘制项采样集（返回是否成功；count ≤ DRAW_SET_PER_SLOT）
		bool draw_sets_bind_batch(uint32_t slot, const Draw_Set_Views* views, uint32_t count);
		//每帧槽预留的逐绘制项采样器集数量
		static constexpr uint32_t DRAW_SET_PER_SLOT = 128;
		/*
		向第 slot 帧槽的场景采样器集写阴影图（set 1 binding 1；第五十二轮）
		场景片元以 binding 1 采样阴影图；后处理集的 binding 1 由 post_scene_bind 独立维护。
		*/
		bool shadow_bind(uint32_t slot, VkSampler sampler, VkImageView view);
		/*
		向第 slot 帧槽的场景采样器集写聚光灯阴影图（set 1 binding 9 的数组元素 index）
		index 取 0..7（第五百零七轮由单张扩为 4 张，第五百一十三轮扩为 8 张）；默认写元素 0，与旧调用兼容。
		*/
		bool spot_shadow_bind(uint32_t slot, VkSampler sampler, VkImageView view, uint32_t index = 0);
		//向第 slot 帧槽的后处理采样器集写采样器（set 1 binding 0；index ∈ [0, POST_SET_PER_SLOT)）
		bool post_texture_bind(uint32_t slot, VkSampler sampler, VkImageView view, uint32_t index = 0);
		/*
		向第 slot 帧槽的后处理采样器集写「第二源」（set 1 binding 1；index ∈ [0, POST_SET_PER_SLOT)）
		用于双源后处理片元（如 泛光叠加.frag）：binding 0 为上一级输出、binding 1 恒为场景目标。
		*/
		bool post_scene_bind(uint32_t slot, VkSampler sampler, VkImageView view, uint32_t index = 0);
		/*
		向第 slot 帧槽的后处理采样器集写「场景深度」（set 1 binding 2；index ∈ [0, POST_SET_PER_SLOT)）
		供深度采样类后处理片元（如 景深.frag）；调用前须保证该深度图像处于 SHADER_READ_ONLY_OPTIMAL。
		*/
		bool post_depth_bind(uint32_t slot, VkSampler sampler, VkImageView view, uint32_t index = 0);
		/*
		向第 slot 帧槽的后处理采样器集写「方向光阴影图」（set 1 binding 4；第三百九十九轮）
		布局中 binding 4 一直存在（与场景集共用一份九绑定布局），此前后处理侧从未写入；
		供 体积光.frag 等需要阴影判定的后处理片元采样，
		调用前须保证该阴影图像处于 SHADER_READ_ONLY_OPTIMAL。
		*/
		bool post_shadow_bind(uint32_t slot, VkSampler sampler, VkImageView view, uint32_t index = 0);
		/*
		向第 slot 帧槽的后处理采样器集写「3D LUT 条带」（set 1 binding 3；第一百四十四轮）
		与场景集的 binding 3（材质法线）共用布局但分属不同描述符集，互不影响；
		供 色彩查找.frag 采样，调用前须保证该图像处于 SHADER_READ_ONLY_OPTIMAL。
		*/
		bool post_lut_bind(uint32_t slot, VkSampler sampler, VkImageView view, uint32_t index = 0);
		/*
		向第 slot 帧槽的场景采样器集批量写全部材质贴图
		（set 1 binding 3 = 法线、4 = 粗糙度、6 = 自发光、7 = 环境光遮蔽、8 = 金属度；第一百七十轮）
		一次 vkUpdateDescriptorSets 更新五个绑定；无贴图时由门面传入内置默认白纹理，
		使各乘子恒为 1 从而不影响画面。纹理模块全部纹理共用同一采样器，故只接收一个采样器。
		*/
		bool material_textures_bind(uint32_t slot, VkSampler sampler, VkImageView normal_view,
			VkImageView roughness_view, VkImageView emissive_view, VkImageView ao_view, VkImageView metallic_view);
		/*
		向第 slot 帧槽的场景采样器集写「天空盒立方体贴图」（set 1 binding 5；第一百二十五轮）
		view 须为 CUBE 视图；未绑定时场景片元不会采样该 binding（由 UBO 的 sky_params 开关控制）。
		*/
		bool sky_bind(uint32_t slot, VkSampler sampler, VkImageView view);
	private:
		//描述符集布局创建（实现见 core/描述符布局.cpp）
		bool descriptor_layouts_create(void);
		//管线布局创建（实现见 core/管线布局.cpp）
		bool pipeline_layout_create(void);
		//图形管线创建（实现见 core/图形管线.cpp；结果写入 pipeline_out，不改动条目；color_format 未定义时跟随 config，sample_count 为 0 时跟随 config）
		bool graphics_pipeline_create(VkShaderModule vert_module, VkShaderModule frag_module, Blend_Mode blend_mode, VkPipeline& pipeline_out,
			VkFormat color_format = VK_FORMAT_UNDEFINED, bool cull_none = false, uint32_t sample_count = 0,
			uint32_t input_locations = 0xFF, bool depth_write = true, bool depth_lequal = false,
			bool alpha_to_coverage = false, bool cull_front = false);
		//按句柄查找条目（未找到返回 nullptr）
		Pipeline_Entry* entry_find(uint64_t handle);
		//按句柄查找条目（只读）
		const Pipeline_Entry* entry_find(uint64_t handle) const;
		//按当前配置格式结合条目自身着色器 / 混合档创建图形管线（失败时条目管线保持空）
		bool entry_pipeline_create(Pipeline_Entry& entry);
	private:
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//配置（颜色 / 深度格式）
		Pipeline_Config config;
		//set 0 布局：场景 UBO
		VkDescriptorSetLayout scene_set_layout = VK_NULL_HANDLE;
		//set 1 布局：采样器
		VkDescriptorSetLayout sampler_set_layout = VK_NULL_HANDLE;
		//管线布局（全管线共享）
		VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
		//管线缓存（第一百零六轮；图形管线创建共用）
		VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
		//描述符池
		VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
		//场景 UBO 描述符集（长度 = frame_overlap）
		std::vector<VkDescriptorSet> scene_sets;
		//采样器描述符集（长度 = frame_overlap）
		std::vector<VkDescriptorSet> sampler_sets;
		//后处理采样器描述符集（长度 = frame_overlap × POST_SET_PER_SLOT；同布局、独立分配；下标 = slot × 2 + index）
		std::vector<VkDescriptorSet> post_sets;
		//逐绘制项采样器描述符集（长度 = frame_overlap × DRAW_SET_PER_SLOT；第一百七十四轮）
		std::vector<VkDescriptorSet> draw_sets;
		//粒子专用场景集（set 0 同布局、独立分配；第三百四十一轮；长度 = frame_overlap）
		std::vector<VkDescriptorSet> particle_sets;
		//批量写入复用的图像描述与写记录（第二百二十一轮；避免每帧堆分配，长度按需增长）
		std::vector<VkDescriptorImageInfo> draw_batch_infos;
		std::vector<VkWriteDescriptorSet> draw_batch_writes;
		//图形管线条目（句柄 0 为默认场景管线，其余为具名管线）
		std::vector<Pipeline_Entry> pipelines;
		//下一个待分配的具名管线句柄（自 1 起递增）
		uint64_t next_handle = 1;
		//当前激活管线句柄
		uint64_t active_handle = 0;
	};
}
