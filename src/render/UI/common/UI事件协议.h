#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"

//游戏引擎命名空间
namespace engine
{
	/*
	UI 域内事件协议（2026-11-21 · UI 局域网改造 批次 1）

	本协议只服务渲染层 UI 域内部的局域网：控件树、交互、UI 渲染器与事件桥之间
	以 category = "UI" 的事件通信，不登记到全局事件中转站，外部模块结构上订阅不到。
	需要出域的信息一律由 UI 事件桥翻译为 EngineSystem 的 Input/* 协议后发出。
	*/
	namespace ui_event
	{
		//事件分类（域内专用，不进入事件总线）
		inline constexpr const char* category = "UI";

		// ———— 域内语义事件 ————

		//按钮点击
		inline constexpr const char* click = "Click";
		//复选框切换
		inline constexpr const char* toggle = "Toggle";
		//滑动条取值变化
		inline constexpr const char* value = "Value";
		//悬停控件变化（帧末合并）
		inline constexpr const char* hover = "Hover";
		//文本框提交
		inline constexpr const char* submit = "Submit";
		//文本输入（系统文本语义回填）
		inline constexpr const char* text_input = "TextInput";
		//模态根打开 / 关闭
		inline constexpr const char* modal_opened = "ModalOpened";
		inline constexpr const char* modal_closed = "ModalClosed";
		//焦点投影变化（系统为权威，域内只做投影）
		inline constexpr const char* focus_changed = "FocusChanged";
		//命令激活控件（网关把 Input/Command 翻译为域内事件）
		inline constexpr const char* activate = "Activate";
		//文本模式变化（网关把 Input/TextMode 翻译为域内事件）
		inline constexpr const char* text_mode = "TextMode";

		// ———— 载荷字段 ————

		//控件句柄
		inline constexpr const char* field_handle = "handle";
		//复选框选中状态
		inline constexpr const char* field_checked = "checked";
		//滑动条取值
		inline constexpr const char* field_value = "value";
		//是否悬停
		inline constexpr const char* field_inside = "inside";
		//文本内容
		inline constexpr const char* field_text = "text";
		//文本光标（字节偏移）
		inline constexpr const char* field_cursor = "cursor";
		//语义命令名
		inline constexpr const char* field_command = "command";
		//系统焦点主对象名
		inline constexpr const char* field_main = "main";
		//系统焦点子对象名
		inline constexpr const char* field_child = "child";
		//模态根对象名
		inline constexpr const char* field_modal = "modal";
		//文本模式开关
		inline constexpr const char* field_active = "active";
		//事件来源标记
		inline constexpr const char* field_origin = "origin";

		//来源标记：网关翻译产出的事件，网关不再回译（防环）
		inline constexpr const char* origin_gateway = "gateway";
	}
}
