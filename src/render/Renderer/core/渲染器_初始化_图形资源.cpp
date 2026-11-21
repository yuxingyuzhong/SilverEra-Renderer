#include "../局部命名空间使用.h"
//六域完整状态与核心模块类型
#include "渲染器内部状态.h"
//获取结果检查出口（含日志系统）
#include "src/render/Renderer/core/结果检查.h"
//OBJ 网格文件解析（第四百二十二轮；资产加载模块）
#include "src/render/Asset/资产加载.h"
//系统输入桥（原始输入载荷构造）
#include "Engine/EngineRenderer/src/render/Input/系统输入桥.h"
//中转站协议（订阅者退出事件）
#include "Engine/EngineFoundation/src/event/Common/事件协议.h"
//std::nothrow
//std::isfinite（第一百七十一轮：材质 UV 变换的有限性校验）
//管线缓存文件读写（第一百零六轮）
//异步资产加载（第四百二十六轮；std::async 与轮询）
//glm 数学库（第五十二轮：光源空间视图投影矩阵）
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "渲染器内部工具.h"

namespace engine
{

	//初始化阶段：管线 / 描述符 / 逐项采样 / 粒子 / 文字管线 / 阴影目标 / 后处理 / 天空 / 阴影资源
	//（从 init 抽出；失败时由调用方统一 shutdown）
	bool renderer_init_graphics(Renderer* owner, Device_State& state, const Renderer_Config& effective)
	{
		//⑫ 管线：图形管线（初始档位 = 不透明）
		if (!owner->resource().scene_pipeline_build(state.scene_input_locations))
		{
			logger.error("渲染器:: 图形管线创建失败");
			owner->device().shutdown();
			return false;
		}
		/*
		⑫.1 反向外扩描边管线（第二百二十八轮）
		剔除正面的场景管线（沿法线外扩后只剩背面），与默认场景管线共享布局与描述符集布局；
		着色器缺失（未随构建拷贝）时不阻断初始化，仅告警并保持描边关闭。
		*/
		{
			uint64_t outline_handle = 0;
			if (owner->resource().pipeline_load_ex(outline_handle, "反向外扩.vert.spv", "反向外扩.frag.spv",
				Blend_Mode::Opaque, false, false, true, true))
				state.outline_pipeline = outline_handle;
			else
				logger.warn("渲染器:: 外扩描边管线登记失败（描边将保持关闭）");
		}
		/*
		⑫.2 计算管线（第二百四十一轮；计算域承接）
		计算与图形共用同一队列族；能力探测、上下文构建与健康检查管线登记由计算域完成，
		队列族不含 COMPUTE 位时初始化不失败（GPU 路径整体关闭）。
		*/
		if (!owner->compute().init(effective))
			return false;
		/*
		⑬ 场景 UBO：每帧槽一个主机可见缓冲（64 字节 mat4），
		写入相机视图 × 投影并把该帧槽 set0 绑定到对应 UBO。
		*/
		state.scene_ubos.resize(effective.frame_overlap);
		state.scene_ubo_allocs.resize(effective.frame_overlap);
		//实例缓冲按帧槽各一（主机可见，逐帧写入本帧全部实例模型矩阵）
		state.instance_buffers.resize(effective.frame_overlap);
		state.instance_allocations.resize(effective.frame_overlap);
		//间接命令缓冲按帧槽各一（第二百五十二轮；主机可见，仅在开关开启时创建）
		state.indirect_buffers.resize(effective.frame_overlap);
		state.indirect_allocations.resize(effective.frame_overlap);
		/*
		簇数据占位存储缓冲（第二百八十八轮）
		set0 binding 1..4 在 Cluster 关闭时无真实数据，仍须有完整描述符；64 字节占位缓冲统一占位。
		*/
		if (!state.cluster_placeholder_ensure())
		{
			logger.error("渲染器:: 簇数据占位缓冲创建失败");
			owner->device().shutdown();
			return false;
		}
		for (uint32_t slot = 0; slot < effective.frame_overlap; ++slot)
		{
			//创建场景 UBO（主机可见、统一缓冲用途；第一百二十五轮由 1072 扩为 1088 字节：追加天空盒参数；第一百四十六轮扩为 1168；第一百七十九轮扩为 1184：追加卡通高光参数）
			if (!state.memory.buffer_create(state.scene_ubos[slot], state.scene_ubo_allocs[slot],
				static_cast<VkDeviceSize>(Device_State::SCENE_BLOCK_BYTES),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true))
			{
				logger.error("渲染器:: 场景 UBO 创建失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			//场景域只组装初始数据；设备域仍按原布局分段上传
			float initial_block[Device_State::SCENE_BLOCK_FLOATS] = {};
			owner->scene().scene_initial_block_fill(initial_block);
			if (!state.memory.buffer_write(state.scene_ubos[slot], state.scene_ubo_allocs[slot],
				initial_block + 0, 64, 0))
			{
				logger.error("渲染器:: 场景 UBO 写入失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			if (!state.memory.buffer_write(state.scene_ubos[slot], state.scene_ubo_allocs[slot],
				initial_block + 16, 64, 64))
			{
				logger.error("渲染器:: 场景光照写入失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			if (!state.memory.buffer_write(state.scene_ubos[slot], state.scene_ubo_allocs[slot],
				initial_block + 32, 256, 128))
			{
				logger.error("渲染器:: 场景点光源写入失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			if (!state.memory.buffer_write(state.scene_ubos[slot], state.scene_ubo_allocs[slot],
				initial_block + 96, 256, 384))
			{
				logger.error("渲染器:: 场景聚光参数写入失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			if (!state.memory.buffer_write(state.scene_ubos[slot], state.scene_ubo_allocs[slot],
				initial_block + 160, 32, 640))
			{
				logger.error("渲染器:: 场景雾参数写入失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			if (!state.memory.buffer_write(state.scene_ubos[slot], state.scene_ubo_allocs[slot],
				initial_block + 168, 16, 672))
			{
				logger.error("渲染器:: 场景环境光颜色写入失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			if (!state.memory.buffer_write(state.scene_ubos[slot], state.scene_ubo_allocs[slot],
				initial_block + 172, 16, 688))
			{
				logger.error("渲染器:: 场景半球环境光写入失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			if (!state.memory.buffer_write(state.scene_ubos[slot], state.scene_ubo_allocs[slot],
				initial_block + 176, 16, 704))
			{
				logger.error("渲染器:: 场景补光方向写入失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			if (!state.memory.buffer_write(state.scene_ubos[slot], state.scene_ubo_allocs[slot],
				initial_block + 180, 16, 720))
			{
				logger.error("渲染器:: 场景补光颜色写入失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			if (!state.memory.buffer_write(state.scene_ubos[slot], state.scene_ubo_allocs[slot],
				initial_block + 184, 128, 736))
			{
				logger.error("渲染器:: 场景逐盏光泽度写入失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			if (!state.memory.buffer_write(state.scene_ubos[slot], state.scene_ubo_allocs[slot],
				initial_block + 460, 1920, static_cast<VkDeviceSize>(Device_State::SCENE_HIGH_SLOTS_BASE) * sizeof(float)))
			{
				logger.error("渲染器:: 场景高 8 槽写入失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			if (!state.memory.buffer_write(state.scene_ubos[slot], state.scene_ubo_allocs[slot],
				initial_block + 216, 96, 864))
			{
				logger.error("渲染器:: 场景阴影参数写入失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			if (!state.memory.buffer_write(state.scene_ubos[slot], state.scene_ubo_allocs[slot],
				initial_block + 240, 16, 960))
			{
				logger.error("渲染器:: 场景 PBR 参数写入失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			//把该帧槽 set0 绑定到对应 UBO
			if (!owner->resource().scene_buffer_bind(slot, reinterpret_cast<uint64_t>(state.scene_ubos[slot])))
			{
				logger.error("渲染器:: 场景 UBO 绑定失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			//Cluster 关闭时四条绑定统一指向同一个占位缓冲
			const uint64_t placeholder = owner->compute().cluster_placeholder_native();
			if (!owner->resource().scene_cluster_bind(slot, placeholder, placeholder, placeholder, placeholder))
			{
				logger.error("渲染器:: 簇数据占位绑定写入失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			/*
			蒙皮调色板缓冲（方向十 · 收敛轮 858；资源域持有）
			主机可见存储缓冲，容量 = 每条 64 字节 × 1024 条；创建时写满单位矩阵并绑定 set0 binding 5。
			*/
			uint64_t palette_buffer = 0;
			if (!owner->resource().animation_palette_resource_create(static_cast<uint32_t>(slot), palette_buffer))
			{
				logger.error("渲染器:: 蒙皮调色板缓冲创建失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			if (!owner->resource().scene_palette_bind(slot, palette_buffer))
			{
				logger.error("渲染器:: 蒙皮调色板绑定失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			//创建该帧槽的实例缓冲（主机可见、顶点缓冲用途；容量 = instance_capacity 个实例，一个实例 80 字节）
			if (!state.memory.buffer_create(state.instance_buffers[slot], state.instance_allocations[slot],
				static_cast<VkDeviceSize>(state.instance_capacity) * INSTANCE_STRIDE_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true))
			{
				logger.error("渲染器:: 实例缓冲创建失败（帧槽 {}）", slot);
				owner->device().shutdown();
				return false;
			}
			/*
			创建该帧槽的间接命令缓冲（第二百五十二轮）
			仅在开关开启且设备支持 firstInstance 偏移时建立；否则该槽保持空缓冲、录制走逐项绘制。
			主机一致内存使命令写入后无需显式冲刷；创建或映射失败仅告警并回落逐项，不阻断初始化。
			*/
			if (effective.gpu_indirect_enable && state.indirect_first_instance_supported)
			{
				if (!state.memory.buffer_create(state.indirect_buffers[slot], state.indirect_allocations[slot],
					static_cast<VkDeviceSize>(Device_State::INDIRECT_COMMAND_CAPACITY) * Device_State::INDIRECT_COMMAND_BYTES,
					VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, true, false, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
					logger.warn("渲染器:: 间接命令缓冲创建失败（帧槽 {}，将回落逐项绘制）", slot);
				else
				{
					//取持久映射指针（命令由主机侧录制时写入）
					void* indirect_mapped = nullptr;
					if (!state.memory.buffer_map(state.indirect_buffers[slot], state.indirect_allocations[slot], indirect_mapped))
						logger.warn("渲染器:: 间接命令缓冲映射失败（帧槽 {}，将回落逐项绘制）", slot);
					else if (!state.frame.slot_indirect_buffer_set(slot, state.indirect_buffers[slot], indirect_mapped,
						Device_State::INDIRECT_COMMAND_CAPACITY))
						logger.warn("渲染器:: 间接命令缓冲登记失败（帧槽 {}，将回落逐项绘制）", slot);
				}
			}
		}
		//⑭ 纹理与内置默认资源（资源域；失败时资源域内部已关闭门面）
		if (!owner->resource().init(effective))
			return false;
		return true;
	}
}
