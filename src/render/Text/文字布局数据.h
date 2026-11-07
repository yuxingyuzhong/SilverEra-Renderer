#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"

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
		/*
		字距（像素；每个字形推进后额外增加的间距）
		tracking **计入行宽**，故同时影响换行判定与对齐结果；
		注意 text_measure 为「无字距基线」，不包含 tracking（度量为单行纯推进量）。
		*/
		float tracking = 0.0f;
		/*
		首行缩进（像素；仅首行起始笔位额外右移）
		indent **不计入换行判据**、也不改变行宽（故右对齐 / 居中时首行整体右移，可能超出 max_width）；
		只对第一行生效，其余行为 0。
		*/
		float first_line_indent = 0.0f;
		/*
		行距倍数（第六十三轮新增；1 = 单倍行距，即按 line_height 逐行下移）
		实际行间步进 = line_height × line_spacing；合法区间 (0, 8]，越界或非数按 1 处理；
		只改变纵向排布，不影响换行判定、行宽与对齐。
		*/
		float line_spacing = 1.0f;
	};
}
