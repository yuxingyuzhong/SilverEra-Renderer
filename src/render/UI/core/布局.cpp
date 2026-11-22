#include "../局部命名空间使用.h"

//游戏引擎命名空间
namespace engine
{
	//按父矩形与布局求子矩形
	Ui_Rect ui_layout_rect_resolve(const Ui_Rect& parent, const Ui_Layout& layout)
	{
		//父矩形宽高非正时无可布局空间
		if (parent.w <= 0.0f || parent.h <= 0.0f)
			return Ui_Rect{};
		//期望尺寸：像素优先，其次父尺寸百分比
		float width = layout.width > 0.0f ? layout.width : 0.0f;
		float height = layout.height > 0.0f ? layout.height : 0.0f;
		if (width <= 0.0f && layout.width_percent > 0.0f)
		{
			const float percent = layout.width_percent > 1.0f ? 1.0f : layout.width_percent;
			width = parent.w * percent;
		}
		if (height <= 0.0f && layout.height_percent > 0.0f)
		{
			const float percent = layout.height_percent > 1.0f ? 1.0f : layout.height_percent;
			height = parent.h * percent;
		}
		//最小尺寸夹紧
		if (layout.min_width > 0.0f && width < layout.min_width)
			width = layout.min_width;
		if (layout.min_height > 0.0f && height < layout.min_height)
			height = layout.min_height;
		//最大尺寸夹紧
		if (layout.max_width > 0.0f && width > layout.max_width)
			width = layout.max_width;
		if (layout.max_height > 0.0f && height > layout.max_height)
			height = layout.max_height;
		//父矩形边界
		const float right = parent.x + parent.w;
		const float bottom = parent.y + parent.h;
		//锚点基准位置
		float x = parent.x;
		float y = parent.y;
		switch (layout.anchor)
		{
		case Ui_Anchor::Top_Left:
			break;
		case Ui_Anchor::Top:
			x = parent.x + (parent.w - width) * 0.5f;
			break;
		case Ui_Anchor::Top_Right:
			x = right - width;
			break;
		case Ui_Anchor::Left:
			y = parent.y + (parent.h - height) * 0.5f;
			break;
		case Ui_Anchor::Center:
			x = parent.x + (parent.w - width) * 0.5f;
			y = parent.y + (parent.h - height) * 0.5f;
			break;
		case Ui_Anchor::Right:
			x = right - width;
			y = parent.y + (parent.h - height) * 0.5f;
			break;
		case Ui_Anchor::Bottom_Left:
			y = bottom - height;
			break;
		case Ui_Anchor::Bottom:
			x = parent.x + (parent.w - width) * 0.5f;
			y = bottom - height;
			break;
		case Ui_Anchor::Bottom_Right:
			x = right - width;
			y = bottom - height;
			break;
		}
		//叠加相对锚点的偏移
		return Ui_Rect{ x + layout.offset_x, y + layout.offset_y, width, height };
	}

	//按创建顺序重算启用布局的节点矩形
	void Ui_Context::layout_apply(void)
	{
		//创建顺序保证父先于子，一次正序即可完成层级传播
		for (Ui_Node& node : nodes)
		{
			//跳过空槽、已销毁、未启用布局与根节点
			if (node.handle == 0 || !node.alive || !node.layout.active || node.parent == 0)
				continue;
			//父节点缺失时保持原矩形
			const Ui_Node* parent = node_find(node.parent);
			if (parent == nullptr)
				continue;
			//按父矩形重算
			node.rect = ui_layout_rect_resolve(parent->rect, node.layout);
		}
	}

	//按创建顺序排列各容器的直接子节点
	void Ui_Context::flow_apply(void)
	{
		//容器按创建顺序处理，嵌套容器先定位再排其子节点
		for (Ui_Node& container : nodes)
		{
			//跳过空槽、已销毁与未启用排列的节点
			if (container.handle == 0 || !container.alive || container.flow == Ui_Flow::None)
				continue;
			//内边距与间距（负值按 0）
			const float padding = container.flow_padding > 0.0f ? container.flow_padding : 0.0f;
			const float gap = container.flow_gap > 0.0f ? container.flow_gap : 0.0f;
			//内容区起点
			float cursor = container.flow == Ui_Flow::Horizontal
				? container.rect.x + padding : container.rect.y + padding;
			//按创建顺序排列直接子节点（跳过不可见）
			for (Ui_Node& child : nodes)
			{
				if (child.handle == 0 || !child.alive || child.parent != container.handle || !child.visible)
					continue;
				//横向：固定纵坐标并依次右移；纵向：固定横坐标并依次下移
				if (container.flow == Ui_Flow::Horizontal)
				{
					child.rect.x = cursor;
					child.rect.y = container.rect.y + padding;
					cursor += child.rect.w + gap;
				}
				else
				{
					child.rect.y = cursor;
					child.rect.x = container.rect.x + padding;
					cursor += child.rect.h + gap;
				}
			}
		}
	}
}
