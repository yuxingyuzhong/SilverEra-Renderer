#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
//网格数据（文字几何与绘制句柄；Vulkan-free）
#include "Application/Renderer/src/render/Mesh/网格数据.h"
//文字布局参数（Vulkan-free）：对齐方式与换行宽度
#include "Application/Renderer/src/render/Text/文字布局数据.h"
//按字体分桶的缓存索引（第九十四轮；unordered_map）
#include <unordered_map>
#include <vector>

//游戏引擎命名空间
namespace engine
{
	//字体上下文（前置声明；文字绘制按需查询 / 补烘字形）
	class Font_Context;

	/*
	文字绘制宿主（由门面注入）
	文字绘制子系统只持有「字体 ＋ 网格缓存 ＋ 颜色」，不直接触碰 Vulkan 资源；
	管线登记、管线 / 纹理绑定、网格创建与归还、绘制项登记、目标尺寸查询均经本结构的回调完成。
	*/
	struct Text_Host
	{
		//回调上下文（门面传入自身）
		void* user = nullptr;
		//确保文字管线就绪（返回句柄；0 = 失败）
		uint64_t (*pipeline_ensure)(void* user) = nullptr;
		//绑定管线
		void (*pipeline_bind)(void* user, uint64_t pipeline) = nullptr;
		//绑定纹理（字形图集）
		void (*texture_bind)(void* user, uint64_t texture) = nullptr;
		//创建网格（上传文字几何）
		bool (*mesh_create)(void* user, Mesh_Handle& mesh, const Mesh_Data& data) = nullptr;
		//归还网格
		void (*mesh_destroy)(void* user, Mesh_Handle mesh) = nullptr;
		//登记绘制项（transform / color 语义同门面 mesh_draw）
		bool (*mesh_draw)(void* user, Mesh_Handle mesh, const float* transform, const float* color) = nullptr;
		//取当前绘制目标尺寸（像素）
		void (*extent_get)(void* user, float& width, float& height) = nullptr;
	};

	/*
	文字绘制子系统（第四十七轮下沉）
	把原先门面 text_draw_layout 的六件事（字形补烘、管线、网格缓存、纹理绑定、像素→NDC 矩阵、绘制登记）
	收在此处；门面只做一行转发。缓存键为（字体句柄, 文本, 布局五项）。
	*/
	class Text_Renderer
	{
	public:
		//绑定字体模块与宿主回调（不持有资源；重复调用以最后一次为准）
		void init(Font_Context& font, const Text_Host& host);
		/*
		按布局绘制文本（像素坐标，原点在绘制目标左上、Y 向下）
		返回假：尚未绑定宿主 / 字体句柄无效 / 字形补烘失败 / 管线登记失败 /
		        几何生成失败 / 网格创建失败 / 目标尺寸非法 / 绘制登记失败。
		*/
		bool draw_layout(uint64_t font, const std::string& text, float pixel_x, float pixel_y,
			const Text_Layout& layout);
		//设置文字颜色（逐分量钳制到 [0,1]；空指针忽略）
		void color_set(const float color[4]);
		//当前文字颜色（4 浮点，RGBA）
		const float* color(void) const;
		//文字管线句柄（0 = 尚未登记）
		uint64_t pipeline_get(void) const;
		//回填文字管线句柄（由宿主回调登记成功后调用）
		void pipeline_set(uint64_t value);
		//归还指定字体的全部缓存网格并移除记录
		void font_meshes_destroy(uint64_t font);
		/*
		归还所有待销毁网格（第四十八轮）
		缓存淘汰与字体卸载只把条目移入待销毁列表，实际 `mesh_destroy` 推迟到下一帧开始前调用本方法，
		避免销毁本帧已登记绘制项仍引用的网格。
		*/
		void retired_flush(void);
		/*
		帧开始（第四十八轮）
		推进内部帧时钟，并归还「退役帧 ＋ 在途帧数 < 当前帧」的待销毁网格（此时已无在途帧引用）；
		overlap 为配置的在途帧数（最小按 1 处理）。
		*/
		void frame_begin(uint32_t overlap);
		//归还全部缓存网格并复位（管线句柄 / 颜色回到默认）
		void shutdown(void);
		//文本网格缓存命中次数（第七十五轮；自 init 起累计）
		uint64_t mesh_cache_hits(void) const { return cache_hits; }
		//文本网格缓存未命中次数（第七十五轮；自 init 起累计）
		uint64_t mesh_cache_misses(void) const { return cache_misses; }
		/*
		设置文本网格缓存条目上限（第八十四轮）
		钳到 [1, 1024]；缩小时立即把最久未用项移入待销毁列表（待无在途帧引用后归还）。
		*/
		void mesh_cache_capacity_set(uint32_t capacity);
		//当前文本网格缓存条目上限（默认 64）
		uint32_t mesh_cache_capacity(void) const { return mesh_cache_max; }
		//当前文本网格缓存条目数（只读诊断）
		uint32_t mesh_cache_size(void) const { return static_cast<uint32_t>(records.size()); }
	private:
		//字体模块（不持有）
		Font_Context* font = nullptr;
		//宿主回调
		Text_Host host;
		//文字管线句柄（0 = 尚未登记）
		uint64_t pipeline = 0;
		//文字颜色（RGBA；默认白）
		float text_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		//文本网格缓存条目（(字体句柄, 文本, 布局) → 网格句柄；避免逐帧重建几何）
		struct Record
		{
			//字体句柄
			uint64_t font = 0;
			//文本（UTF-8）
			std::string text;
			//布局（五项参与缓存键，避免同文本不同布局命中旧网格）
			Text_Layout layout;
			//网格句柄
			Mesh_Handle mesh = 0;
			//最近使用序号（第四十八轮；用于缓存满时淘汰最久未用项）
			uint64_t last_used = 0;
			//退役帧号（第四十八轮；仅待销毁条目使用）
			uint64_t retired_tick = 0;
		};
		//缓存集合
		std::vector<Record> records;
		/*
		按字体分桶的缓存索引（第九十四轮）
		字体句柄 → 该字体在 records 中的下标列表；查找只扫描同字体条目，
		避免每次绘制遍历全部字体。records 增删后须重建（entries 少，重建成本低）。
		*/
		std::unordered_map<uint64_t, std::vector<std::size_t>> font_index;
		//重建分桶索引（records 增删后调用）
		void font_index_rebuild(void);
		//待销毁网格（第四十八轮：淘汰 / 卸载时先移入，待无在途帧引用后统一归还）
		std::vector<Record> retired;
		//最近使用序号发生器（第四十八轮）
		uint64_t use_clock = 0;
		//帧时钟（第四十八轮；每帧开始自增，用于判断待销毁网格是否已无在途引用）
		uint64_t frame_clock = 0;
		//在途帧数（第四十八轮）
		uint32_t frame_overlap = 1;
		//文本网格缓存条目上限（第四十八轮默认 64；第八十四轮改为可配，超出即淘汰最久未用项）
		uint32_t mesh_cache_max = 64;
		//缓存命中次数（第七十五轮）
		uint64_t cache_hits = 0;
		//缓存未命中次数（第七十五轮）
		uint64_t cache_misses = 0;
	};
}
