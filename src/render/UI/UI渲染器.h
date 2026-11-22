#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
/*
Vulkan 与 VMA 头
本头属渲染层「实现细节」：只被本层 .cpp 与层内模块头包含，不进公共头。
*/
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
//UI 控件树（SDL-free 公共头）
#include "Engine/EngineRenderer/src/render/UI/用户界面.h"
//网格数据类型（UI 顶点与文字几何）
#include "Engine/EngineRenderer/src/render/Mesh/网格数据.h"
//依赖模块（仅作引用入参）
#include "Engine/EngineRenderer/src/render/Memory/内存上下文.h"
#include "Engine/EngineRenderer/src/render/Pipeline/管线.h"
#include "Engine/EngineRenderer/src/render/Texture/纹理.h"
#include "Engine/EngineRenderer/src/render/Text/字体.h"

//游戏引擎命名空间
namespace engine
{
	/*
	UI 渲染器（每帧把控件树转成绘制命令）
	只负责 GPU 侧：每帧槽一组动态顶点 / 索引缓冲、两个 UI 管线（纯色 / 文字）与字形图集绑定；
	控件树与交互由 Ui_Context 维护。
	绘制发生在帧链路的 UI 阶段（无深度附件、载入保留），故管线必须关闭深度测试。
	*/
	class Ui_Renderer
	{
	public:
		//构造（不创建任何 GPU 资源）
		Ui_Renderer(void) = default;
		//析构（内部调用 shutdown，保证资源不泄漏）
		~Ui_Renderer(void);
		//不可拷贝、不可移动（持有 GPU 资源）
		Ui_Renderer(const Ui_Renderer&) = delete;
		Ui_Renderer& operator=(const Ui_Renderer&) = delete;

		//绑定依赖模块与 UI 管线句柄（frame_overlap 决定每帧槽缓冲数）
		bool build(VkDevice device, Memory_Context& memory, Pipeline_Context& pipeline,
			Texture_Context& texture, Font_Context& font,
			uint64_t solid_pipeline, uint64_t text_pipeline, uint32_t frame_overlap);
		//销毁（幂等；归还全部每帧槽缓冲）
		void shutdown(void);
		//是否已构建完成
		bool valid(void) const;
		/*
		把控件树绘制到给定命令缓冲
		slot 为当前帧槽；extent 为 UI 阶段颜色附件尺寸（像素）；
		无可见控件时不产生任何绘制命令；字体未设置或字符无字形时只绘制纯色部分。
		*/
		void draw(const Ui_Context& ui, VkCommandBuffer command_buffer, VkExtent2D extent, uint32_t slot);
	private:
		//单帧槽缓冲
		struct Slot_Buffers
		{
			//顶点缓冲
			VkBuffer vertex_buffer = VK_NULL_HANDLE;
			VmaAllocation vertex_allocation = VK_NULL_HANDLE;
			void* vertex_mapped = nullptr;
			uint32_t vertex_capacity = 0;
			//索引缓冲
			VkBuffer index_buffer = VK_NULL_HANDLE;
			VmaAllocation index_allocation = VK_NULL_HANDLE;
			void* index_mapped = nullptr;
			uint32_t index_capacity = 0;
			//已绑定的字形图集纹理句柄（避免逐帧重写描述符）
			uint64_t bound_atlas = 0;
		};
		//单条绘制记录
		struct Draw_Record
		{
			//索引起点与数量
			uint32_t first_index = 0;
			uint32_t index_count = 0;
			//颜色（纯色 / 文字共用）
			Ui_Color color{};
			//纹理句柄（0 = 纯色；文字与图片使用纹理管线）
			uint64_t texture = 0;
			//是否文字绘制（真 = 文字管线 + 图集采样）
			bool text = false;
			//该条绘制的裁剪矩形（窗口像素；阶段录制时转为 scissor）
			Ui_Rect clip{};
		};

		//确保帧槽缓冲容量足够（不足时重建）
		bool slot_ensure(Slot_Buffers& slot, uint32_t vertex_count, uint32_t index_count);
		//追加一个矩形（像素坐标）并登记纯色绘制
		void quad_append(const Ui_Rect& rect, const Ui_Color& color);
		//追加一个纹理矩形（UV 铺满 0..1）并登记纹理绘制（第八百九十三轮）
		void quad_textured_append(uint64_t texture, const Ui_Rect& rect, const Ui_Color& color);
		/*
		追加一个圆角矩形（第八百八十七轮）
		radius <= 0 时退化为 quad_append（保证直角路径逐字节不变）；
		否则按四角圆弧分段 + 中心扇形三角化。
		*/
		void rounded_quad_append(const Ui_Rect& rect, float radius, const Ui_Color& color);
		//追加带描边的背景（先描边后填充；border_width <= 0 时只填充）
		void background_append(const Ui_Rect& rect, float radius, float border_width,
			const Ui_Color& border_color, const Ui_Color& fill_color);
		//追加文本（返回是否登记了绘制；字体未设置 / 无字形时返回假）
		bool text_append(uint64_t font_handle, const std::string& text, float pixel_x, float pixel_y,
			float center_area_width, const Ui_Color& color);
		//结束一笔文本绘制（记录当前字形图集句柄）
		void text_atlas_mark(uint64_t font_handle);

	private:
		//逻辑设备
		VkDevice device = VK_NULL_HANDLE;
		//内存模块（不持有）
		Memory_Context* memory = nullptr;
		//管线模块（不持有）
		Pipeline_Context* pipeline = nullptr;
		//纹理模块（不持有）
		Texture_Context* texture = nullptr;
		//字体模块（不持有）
		Font_Context* font = nullptr;
		//UI 纯色管线句柄
		uint64_t solid_pipeline = 0;
		//UI 文字管线句柄
		uint64_t text_pipeline = 0;
		//每帧槽缓冲
		std::vector<Slot_Buffers> slots{};
		//本帧 CPU 顶点
		std::vector<Vertex> cpu_vertices{};
		//本帧 CPU 索引
		std::vector<uint32_t> cpu_indices{};
		//本帧绘制记录
		std::vector<Draw_Record> draws{};
		//文字几何临时数据（复用）
		Mesh_Data text_scratch{};
		//本帧使用的字形图集纹理句柄（0 = 无文字）
		uint64_t text_atlas = 0;
		//各槽位的继承裁剪（下标 = 句柄 - 1；逐帧复用，第八百八十八轮）
		std::vector<Ui_Rect> inherited_clips{};
		//当前控件的裁剪矩形（追加几何时写入绘制记录）
		Ui_Rect current_clip{};
	};
}
