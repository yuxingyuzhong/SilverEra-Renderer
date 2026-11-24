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
			//记录基准矩形（滚动偏移前的坐标）
			node.rect_base = node.rect;
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
			//内容区尺寸（交叉轴对齐与拉伸用）
			const float content_width = container.rect.w - padding * 2.0f > 0.0f
				? container.rect.w - padding * 2.0f : 0.0f;
			const float content_height = container.rect.h - padding * 2.0f > 0.0f
				? container.rect.h - padding * 2.0f : 0.0f;
			//交叉轴对齐系数（Stretch 单独处理）
			float align_factor = 0.0f;
			if (container.flow_align == Ui_Align::Center)
				align_factor = 0.5f;
			else if (container.flow_align == Ui_Align::End)
				align_factor = 1.0f;
			//换行流式排列（第九百四十九 / 九百五十轮）
			if (container.flow_wrap)
			{
				//主轴可用长度
				const float content_main = container.flow == Ui_Flow::Horizontal ? content_width : content_height;
				//主轴游标、累计交叉轴偏移与当前行子项
				float cursor = 0.0f;
				float cross_offset = 0.0f;
				std::vector<Ui_Node*> row{};
				//放置当前行（先量行交叉尺寸，再按 flow_align 对齐 / 拉伸）
				const auto place_row = [&](void)
				{
					//行内最大交叉占用（含两端外边距）
					float line_cross = 0.0f;
					for (const Ui_Node* child : row)
					{
						const float margin = child->margin > 0.0f ? child->margin : 0.0f;
						const float child_cross = (container.flow == Ui_Flow::Horizontal
							? child->rect.h : child->rect.w) + margin * 2.0f;
						if (child_cross > line_cross)
							line_cross = child_cross;
					}
					//行内主轴总占用
					float row_main = 0.0f;
					std::size_t row_count = 0;
					for (const Ui_Node* child : row)
					{
						const float margin = child->margin > 0.0f ? child->margin : 0.0f;
						row_main += (container.flow == Ui_Flow::Horizontal ? child->rect.w : child->rect.h)
							+ margin * 2.0f;
						++row_count;
					}
					//行内间距计入占用
					if (row_count > 1)
						row_main += gap * static_cast<float>(row_count - 1);
					//主轴剩余与分布偏移 / 额外间隔（第九百五十一轮）
					const float leftover = content_main - row_main > 0.0f ? content_main - row_main : 0.0f;
					float justify_offset = 0.0f;
					float extra_gap = 0.0f;
					if (container.flow_justify == Ui_Justify::Center)
						justify_offset = leftover * 0.5f;
					else if (container.flow_justify == Ui_Justify::End)
						justify_offset = leftover;
					else if (container.flow_justify == Ui_Justify::Space_Between && row.size() > 1)
						extra_gap = leftover / static_cast<float>(row.size() - 1);
					//逐项放置
					float cursor_main = justify_offset;
					for (Ui_Node* child : row)
					{
						const float margin = child->margin > 0.0f ? child->margin : 0.0f;
						const float child_main = (container.flow == Ui_Flow::Horizontal
							? child->rect.w : child->rect.h) + margin * 2.0f;
						const float child_cross = (container.flow == Ui_Flow::Horizontal
							? child->rect.h : child->rect.w) + margin * 2.0f;
						//交叉轴起点（对齐 / 拉伸）
						float cross_pos = cross_offset + margin;
						if (container.flow_align == Ui_Align::Stretch)
						{
							const float target = line_cross - margin * 2.0f > 0.0f
								? line_cross - margin * 2.0f : 0.0f;
							if (container.flow == Ui_Flow::Horizontal)
								child->rect.h = target;
							else
								child->rect.w = target;
						}
						else
							cross_pos += (line_cross - child_cross) * align_factor;
						//按方向落位
						if (container.flow == Ui_Flow::Horizontal)
						{
							child->rect.x = container.rect.x + padding + cursor_main + margin;
							child->rect.y = container.rect.y + padding + cross_pos;
						}
						else
						{
							child->rect.y = container.rect.y + padding + cursor_main + margin;
							child->rect.x = container.rect.x + padding + cross_pos;
						}
						//排列结果即滚动偏移前的基准矩形
						child->rect_base = child->rect;
						cursor_main += child_main + gap + extra_gap;
					}
					//行交叉尺寸与行间距计入累计偏移（第九百五十二轮）
					cross_offset += line_cross + gap;
					row.clear();
				};
				for (Ui_Node& child : nodes)
				{
					//非本容器可见子项跳过
					if (child.handle == 0 || !child.alive || child.parent != container.handle || !child.visible)
						continue;
					const float margin = child.margin > 0.0f ? child.margin : 0.0f;
					//主轴占用（含两端外边距）
					const float child_main = (container.flow == Ui_Flow::Horizontal ? child.rect.w : child.rect.h)
						+ margin * 2.0f;
					//当前行非空且放不下：先放置当前行再另起一行（列）
					if (!row.empty() && cursor + child_main > content_main)
					{
						place_row();
						cursor = 0.0f;
					}
					row.push_back(&child);
					cursor += child_main + gap;
				}
				//收束最后一行
				if (!row.empty())
					place_row();
				continue;
			}
			//主轴剩余空间按弹性权重分配（第九百四十八轮）
			{
				//主轴可用长度
				const float content_main = container.flow == Ui_Flow::Horizontal ? content_width : content_height;
				//基准占用与权重和
				float total_main = 0.0f;
				float weight_sum = 0.0f;
				std::size_t child_count = 0;
				for (const Ui_Node& child : nodes)
				{
					//非本容器可见子项跳过
					if (child.handle == 0 || !child.alive || child.parent != container.handle || !child.visible)
						continue;
					const float margin = child.margin > 0.0f ? child.margin : 0.0f;
					total_main += (container.flow == Ui_Flow::Horizontal ? child.rect.w : child.rect.h)
						+ margin * 2.0f;
					//累计弹性权重
					if (child.stretch > 0.0f)
						weight_sum += child.stretch;
					++child_count;
				}
				//间距计入基准占用
				if (child_count > 1)
					total_main += gap * static_cast<float>(child_count - 1);
				//剩余主轴长度
				const float leftover = content_main - total_main;
				//有剩余且有弹性项时按权重追加主轴尺寸
				if (leftover > 0.0f && weight_sum > 0.0f)
				{
					for (Ui_Node& child : nodes)
					{
						//非本容器可见子项或未开启弹性
						if (child.handle == 0 || !child.alive || child.parent != container.handle
							|| !child.visible || child.stretch <= 0.0f)
							continue;
						const float extra = leftover * child.stretch / weight_sum;
						if (container.flow == Ui_Flow::Horizontal)
							child.rect.w += extra;
						else
							child.rect.h += extra;
					}
				}
			}
			//内容区起点
			float cursor = container.flow == Ui_Flow::Horizontal
				? container.rect.x + padding : container.rect.y + padding;
			//按创建顺序排列直接子节点（跳过不可见）
			for (Ui_Node& child : nodes)
			{
				if (child.handle == 0 || !child.alive || child.parent != container.handle || !child.visible)
					continue;
				//横向：固定纵坐标并依次右移；纵向：固定横坐标并依次下移
				const float margin = child.margin > 0.0f ? child.margin : 0.0f;
				if (container.flow == Ui_Flow::Horizontal)
				{
					child.rect.x = cursor + margin;
					child.rect.y = container.rect.y + padding;
					if (container.flow_align == Ui_Align::Stretch)
						child.rect.h = content_height;
					else
						child.rect.y += (content_height - child.rect.h) * align_factor;
					cursor = child.rect.x + child.rect.w + margin + gap;
				}
				else
				{
					child.rect.y = cursor + margin;
					child.rect.x = container.rect.x + padding;
					if (container.flow_align == Ui_Align::Stretch)
						child.rect.w = content_width;
					else
						child.rect.x += (content_width - child.rect.w) * align_factor;
					cursor = child.rect.y + child.rect.h + margin + gap;
				}
				//排列结果即滚动偏移前的基准矩形
				child.rect_base = child.rect;
			}
		}
	}

	//按祖先滚动偏移平移各节点矩形（逐帧执行，幂等）
	void Ui_Context::scroll_apply(void)
	{
		//各槽位的累计滚动偏移（下标 = 句柄 - 1；父先于子，一次正序即可传播）
		std::vector<float> offset_x(nodes.size(), 0.0f);
		std::vector<float> offset_y(nodes.size(), 0.0f);
		for (std::size_t index = 0; index < nodes.size(); ++index)
		{
			Ui_Node& node = nodes[index];
			//跳过空槽与已销毁节点
			if (node.handle == 0 || !node.alive)
				continue;
			//继承父节点的累计偏移（无父节点时为零）
			float inherited_x = 0.0f;
			float inherited_y = 0.0f;
			if (node.parent != 0 && node.parent <= nodes.size())
			{
				inherited_x = offset_x[static_cast<std::size_t>(node.parent - 1)];
				inherited_y = offset_y[static_cast<std::size_t>(node.parent - 1)];
			}
			//最终矩形 = 基准矩形 - 继承偏移
			node.rect = Ui_Rect{ node.rect_base.x - inherited_x, node.rect_base.y - inherited_y,
				node.rect_base.w, node.rect_base.h };
			//本节点滚动继续作用于后代
			offset_x[index] = inherited_x + node.scroll_x;
			offset_y[index] = inherited_y + node.scroll_y;
		}
	}
}
