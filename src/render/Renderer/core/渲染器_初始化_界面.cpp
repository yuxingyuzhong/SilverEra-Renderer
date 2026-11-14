#include "../局部命名空间使用.h"
//实现体定义（持有各模块实例）
#include "渲染器实现.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//初始化阶段函数声明
#include "渲染器内部工具.h"

//游戏引擎命名空间
namespace engine
{
	//初始化阶段：UI 通道 / 事件桥接 / 原生 UI（实现见 渲染器_界面.cpp）
	bool renderer_init_ui(Renderer* owner, Renderer_Impl_Holder::Impl* impl, Renderer_Config& effective)
	{
		//实现体或门面缺失则无法装配
		if (owner == nullptr || impl == nullptr)
			return false;
		//转交界面域
		owner->ui().init(effective);
		return true;
	}
}
