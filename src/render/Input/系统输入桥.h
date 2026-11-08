#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"
//系统中立键位与原始输入事件字段（渲染层 → 系统层输入的唯一契约）
#include "Engine/EngineSystem/src/input/common/输入类型.h"
#include "Engine/EngineSystem/src/input/common/输入事件.h"
//帧输入快照（SDL-free）
#include "Engine/EngineRenderer/src/render/Input/输入快照.h"
//JSON 载荷
#include <nlohmann/json.hpp>

//游戏引擎命名空间
namespace engine
{
	/*
	系统输入桥
	把渲染层自持的帧输入翻译成 EngineSystem 输入模块的 Input/Raw 载荷：
	    键位：SDL 扫描码 → 系统中立键位位域（main / control / keypad / mouse / wheel）
	    指针：直接取快照的窗口像素坐标与位移增量
	    按住快照：把跨帧按住的物理键写入 held，供系统侧组合修饰匹配与失帧重同步
	本头属本层实现细节（含系统层头与 JSON），不进公共主头。
	*/
	namespace input_bridge
	{
		//SDL 扫描码 → 系统中立键位（无法翻译返回 0）
		Input_Key key_of_scancode(uint32_t scancode);
		//SDL 鼠标按键（1..5）→ 系统中立键位（越界返回 0）
		Input_Key key_of_mouse_button(uint32_t button);
		//滚轮方向 → 系统中立键位
		Input_Key key_of_wheel(bool up);
		/*
		构造 Input/Raw 载荷
		text_mode 为真时系统侧挂起键位映射（UI 文本框获焦时使用）；
		载荷字段：frame / dt / keys / held / pointer / text_mode（text_mode 为真才写）。
		返回假表示帧快照不可用（当前无此情形，保留给未来扩展）。
		*/
		bool raw_build(const Input_Frame& frame, bool text_mode, nlohmann::json& payload_out);
	}
}
