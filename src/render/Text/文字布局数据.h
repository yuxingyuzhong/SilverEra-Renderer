#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"

//游戏引擎命名空间
namespace engine
{
	//文字行内对齐方式
	enum class Text_Align
	{
		//左对齐（每行起始笔位落在文本原点）
		Left,
		//居中（每行按自身宽度居中于 [原点, 原点 + max_width)）
		Center,
		//右对齐（每行右端对齐到 原点 + max_width）
		Right,
	};

	/*
	文字布局参数（Vulkan-free 公共语义）
	max_width 为 0 表示**不换行**：整段为单行，此时 align 无效果（原点即锚点）；
	line_height 为 0 表示取字体的像素高度（Font_Record::pixel_height）。

	换行口径：**逐字符**按字形 advance 累加判定（不识别单词边界），显式 '\n' 同样断行；
	单个字形宽度超过 max_width 时独占一行。对齐基准为「该行 advance 之和」，
	故行尾存在空白字符时对齐结果会略偏右（空白也计入行宽）。
	*/
	struct Text_Layout
	{
		//换行宽度上限（像素；0 = 不换行）
		float max_width = 0.0f;
		//行高（像素；0 = 取字体像素高度）
		float line_height = 0.0f;
		//行内对齐
		Text_Align align = Text_Align::Left;
	};
}
