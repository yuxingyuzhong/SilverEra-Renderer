#pragma once
//预编译头
#include "Engine/EngineSystem/src/core/Renderer/common/前置头文件包含.h"
/*
SDL3 头
本头属本层「实现细节」：只被本层 .cpp 与层内模块头包含，不进公共头，
故可以合法暴露 SDL3 类型（层外仍看不到 —— 见 cmake/对外接口.cmake 的私有包含根）。
*/
#include <SDL3/SDL.h>
/*
Vulkan 头
SDL3 的 Vulkan 接口未随 SDL.h 一并引入，故显式包含 SDL_vulkan.h；
且必须排在 vulkan.h 之后：vulkan.h 会定义 VULKAN_CORE_H_，令 SDL_vulkan.h
跳过其自带的 Vulkan 句柄 typedef，避免 VkSurfaceKHR 被重复定义。
*/
#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>

//游戏引擎命名空间
namespace engine
{
	/*
	平台上下文
	持有 SDL3 视频子系统、窗口与 VkSurfaceKHR，并提供事件泵与实例扩展清单。
	生命周期：build → 使用 → destroy；destroy 幂等，析构兜底。
	创建顺序：视频子系统 → 窗口 →（实例建立后）表面；任一步失败即逆序回退，不留下半初始化状态。
	事件泵只置标志，不做任何 Vulkan 操作（交换链重建由上层帧链路统一决策）。
	*/
	class Platform_Context
	{
	public:
		//构造（不创建任何 SDL / Vulkan 对象）
		Platform_Context(void) = default;
		//析构（内部调用 destroy，保证资源不泄漏）
		~Platform_Context(void);
		//不可拷贝、不可移动（持有 SDL / Vulkan 句柄）
		Platform_Context(const Platform_Context&) = delete;
		Platform_Context& operator=(const Platform_Context&) = delete;

		//初始化 SDL 视频子系统并创建窗口（hidden 为真 → 附加 SDL_WINDOW_HIDDEN）
		bool build(uint32_t width, uint32_t height, const std::string& title, bool hidden);
		//销毁（幂等；按创建逆序：表面 → 窗口 → 视频子系统）
		void destroy(void);
		//是否已构建完成（窗口已创建）
		bool valid(void) const;

		//轮询 SDL 事件，只置标志（不在此处重建交换链）
		void events_pump(void);
		//设置 SDL 事件转发回调（事件泵逐个事件回调；供上层平台后端处理输入）
		void event_forward_set(void (*callback)(const SDL_Event* event, void* user_data), void* user_data);
		//清除 SDL 事件转发回调
		void event_forward_clear(void);
		//窗口是否请求退出
		bool close_requested(void) const;
		//取走并清空「窗口尺寸已变化，交换链待重建」标志
		bool resized_consume(void);
		//窗口是否处于最小化（挂起）
		bool minimized(void) const;
		//取窗口客户区尺寸（像素）
		void window_size_get(uint32_t& width_out, uint32_t& height_out) const;
		/*
		请求改变窗口客户区尺寸（像素）
		成功即同步置「交换链待重建」标志：隐藏窗口下 SDL 不一定派发尺寸变化事件，
		故此处显式置标志，使「尺寸变化 → 交换链重建」在任意窗口形态下均可被上层驱动。
		*/
		bool window_size_set(uint32_t width, uint32_t height);
		/*
		全屏切换（第一百一十一轮）
		成功返回真并置「交换链待重建」标志（全屏往往伴随客户区尺寸变化）；
		隐藏窗口下后端可能拒绝全屏，失败返回假且保持窗口状态不变。
		*/
		bool fullscreen_set(bool fullscreen);
		//当前是否处于全屏（窗口未创建时为假）
		bool fullscreen_get(void) const;
		//窗口显示缩放比例（DPI；窗口未创建或查询失败时为 1）
		float display_scale_get(void) const;
		//原生窗口句柄（供上层调试；SDL 类型属本层实现细节）
		SDL_Window* window_get(void) const;
		//组装实例所需扩展清单（内部调 SDL_Vulkan_GetInstanceExtensions；返回是否成功）
		bool instance_extensions_get(std::vector<const char*>& extensions_out) const;
		//创建 VkSurfaceKHR（必须在 VkInstance 创建之后调用）
		bool surface_build(VkInstance instance);
		//获取表面句柄
		VkSurfaceKHR surface_get(void) const;
	private:
		//创建窗口（实现见 core/窗口创建.cpp）
		bool window_create(uint32_t width, uint32_t height, const std::string& title, bool hidden);
		//销毁窗口与视频子系统（实现见 core/窗口创建.cpp）
		void window_destroy(void);
	private:
		//窗口句柄
		SDL_Window* window = nullptr;
		//表面句柄
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		//实例句柄（销毁表面时必需；由 surface_build 记录）
		VkInstance instance = VK_NULL_HANDLE;
		//视频子系统是否已初始化
		bool video_initialized = false;
		//退出标志
		bool close_flag = false;
		//交换链待重建标志
		bool resize_flag = false;
		//窗口最小化（挂起）标志
		bool minimize_flag = false;
		//事件转发回调（为空表示不转发）
		void (*event_callback)(const SDL_Event* event, void* user_data) = nullptr;
		//事件转发回调的用户数据
		void* event_callback_data = nullptr;
	};
}
