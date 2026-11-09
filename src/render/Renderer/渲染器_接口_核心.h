#pragma once
//公共语义类型与共享实现体访问
#include "渲染器实现访问.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器能力接口：核心
	*/
	class Renderer_Core : public virtual Renderer_Impl_Holder
	{
	public:
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

		//资源计数快照（第一百一十三 / 一百一十四轮；未初始化时全零）
		Renderer_Resource_Counts resource_counts(void) const;
		/*
		累计场景 UBO 上传字节数（第六十轮新增）
		场景 UBO 现按 16 字节单元比较、仅写入变化区间；本查询供上层观测每帧上传开销
		（静态场景下除各帧槽首次预热外应接近 0）。未初始化返回 0。
		*/
		uint64_t scene_upload_bytes(void) const;

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

	};
}
