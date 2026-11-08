#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
/*
SDL3 头
本头属渲染层「公共扩展头」：含 SDL3 类型，供需要喂入平台事件的调用方（测试层 / 应用层）包含；
公共主头 渲染器.h 只前置声明本类，不引入 SDL3。
*/
#include <SDL3/SDL.h>
//帧输入快照（SDL-free）
#include "Engine/EngineRenderer/src/render/Input/输入快照.h"

//游戏引擎命名空间
namespace engine
{
	/*
	输入聚合器
	把 SDL 事件累积为「每帧一份输入快照」：
	    事件喂入（event_feed）只累积待生效状态与边沿；
	    帧推进（frame_advance）把待生效状态锁存为当前帧并清空边沿；
	    读取（frame_get）供 UI 命中测试与渲染层内部消费。
	键位编码与 SDL_Scancode 一致；滚动 / 指针增量均按 SDL 事件原值累积。
	*/
	class Input_Aggregator
	{
	public:
		//构造（不启用文本输入）
		Input_Aggregator(void) = default;
		//析构（若文本输入处于启用状态则关闭）
		~Input_Aggregator(void);
		//不可拷贝、不可移动
		Input_Aggregator(const Input_Aggregator&) = delete;
		Input_Aggregator& operator=(const Input_Aggregator&) = delete;

		//喂入单个 SDL 事件（空指针忽略）
		void event_feed(const SDL_Event* event);
		//绑定平台窗口（文本输入启停需要窗口；空指针表示未绑定）
		void window_set(SDL_Window* window);
		/*
		帧推进：锁存待生效状态为本帧快照并清空单帧边沿
		window_width / window_height / dpi_scale 由平台侧逐帧给出（0 表示未知，保持上一帧值）。
		*/
		void frame_advance(double dt, uint32_t window_width, uint32_t window_height, float dpi_scale);
		//当前帧快照
		const Input_Frame& frame_get(void) const;
		//启用 / 关闭平台文本输入（幂等；关闭时清空预编辑串）
		void text_input_enable(bool enabled);
		//平台文本输入是否启用
		bool text_input_enabled(void) const;
		//复位全部状态（关闭文本输入）
		void reset(void);
	private:
		//当前帧快照
		Input_Frame frame{};
		//待生效按下沿
		std::vector<Input_Key_Code> pending_pressed{};
		//待生效抬起沿
		std::vector<Input_Key_Code> pending_released{};
		//待生效重复沿
		std::vector<Input_Key_Code> pending_repeat{};
		//待生效鼠标按下沿（下标 = 按键）
		bool pending_button_pressed[INPUT_MOUSE_BUTTON_COUNT] = {};
		//待生效鼠标抬起沿
		bool pending_button_released[INPUT_MOUSE_BUTTON_COUNT] = {};
		//待生效指针位移增量
		float pending_dx = 0.0f;
		float pending_dy = 0.0f;
		//待生效滚轮增量
		float pending_wheel_x = 0.0f;
		float pending_wheel_y = 0.0f;
		//待生效滚轮齿数（向上 / 向下）
		uint32_t pending_wheel_up_ticks = 0;
		uint32_t pending_wheel_down_ticks = 0;
		//按住状态（跨帧保持；下标 = 键位编码）
		std::array<uint8_t, INPUT_KEY_CAPACITY> keys_held{};
		//鼠标按住状态（跨帧保持）
		bool buttons_held[INPUT_MOUSE_BUTTON_COUNT] = {};
		//待生效窗口尺寸是否来自事件
		bool pending_window_valid = false;
		//待生效窗口尺寸
		uint32_t pending_window_width = 0;
		uint32_t pending_window_height = 0;
		//待生效文本提交
		std::string pending_committed{};
		//待生效预编辑串是否已变化
		bool pending_preedit_changed = false;
		//待生效预编辑串
		std::string pending_preedit{};
		//平台文本输入是否启用
		bool text_active = false;
		//平台窗口（文本输入启停需要）
		SDL_Window* window = nullptr;
	};
}
