#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"

//游戏引擎命名空间
namespace engine
{
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
		//每帧推进（泵事件 → 取图像 → 录制 → 提交 → 呈现）
		bool frame_render(void);
		//窗口是否请求退出
		bool close_requested(void) const;
		//已呈现帧数（调试与用例断言用）
		uint64_t frame_count(void) const;
		//当前交换链尺寸（窗口缩放后可能变化）
		uint32_t extent_width(void) const;
		uint32_t extent_height(void) const;
	private:
		//实现体（把 SDL3 / Vulkan / VMA 全部关在 .cpp 里）
		class Impl;
		std::unique_ptr<Impl> impl;
	};
}
