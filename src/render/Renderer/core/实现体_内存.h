#pragma once
//依赖：core/渲染器实现.h 已在本头之前提供模块类型

//游戏引擎命名空间
namespace engine
{
	//渲染器实现体子结构：内存
	struct Renderer_Impl_内存
	{
		//内存模块
		Memory_Context memory;

		//累计场景 UBO 上传字节数（第六十轮；仅供 scene_upload_bytes 观测，不随帧重置）
		uint64_t scene_upload_bytes = 0;
	};
}
