#include "../局部命名空间使用.h"
//六域完整状态与核心模块类型
#include "渲染器内部状态.h"
//帧内部辅助类型与函数
#include "渲染器内部工具.h"
//帧内共享辅助（合批 / 阴影签名）
#include "渲染器_帧内部.h"

namespace engine
{
	bool Render_Device::frame_stage_scene_ubo(Renderer* owner, Device_State& state, uint32_t slot, const float view_proj[16], bool spot_shadow_light_valid)
	{
		/*
		场景 UBO 整块组装（1184 字节 = 296 浮点；第六十二轮 960、第六十四轮追加 PBR-lite 参数；
		第一百二十一轮追加远区级联矩阵与级联参数；第一百二十五轮追加天空盒参数；
		第一百四十六轮追加第三级级联矩阵与第三级参数；第一百七十九轮追加卡通高光参数）
		各段浮点偏移与 std140 契约一致：0 视图投影、16 光照块、32 点光源位置、64 点光源颜色、
		96 聚光方向、128 聚光锥参数、160 雾、168 环境光颜色、172 半球环境光、176 补光方向与强度、
		180 补光颜色、184 逐盏光泽度（8 盏 × 4）、216 光源空间矩阵、232 阴影参数、236 阴影强度、
		240 PBR-lite 参数（x 启用 / y 粗糙度缩放）、244 远区级联矩阵、260 级联参数、264 相机近远平面、
		268 天空盒参数（x 启用 / y 漫反射强度 / z 镜面强度）、272 第三级级联矩阵、288 第三级参数、
		292 卡通高光参数（x 阈值）。
		*/
		float scene_block[Device_State::SCENE_BLOCK_FLOATS] = {};
		//场景域填充自身参数及资源、计算域的只读帧值
		owner->scene().scene_frame_block_fill(scene_block, view_proj, spot_shadow_light_valid);
		//设备域写入本域计算得到的方向光矩阵与深度跨度
		for (std::size_t index = 0; index < 16; ++index)
		{
			scene_block[216 + index] = state.light_view_proj[index];
			scene_block[244 + index] = state.light_view_proj_far[index];
			scene_block[272 + index] = state.light_view_proj_far2[index];
		}
		scene_block[262] = state.cascade_depth_span[0];
		scene_block[263] = state.cascade_depth_span[1];
		scene_block[289] = state.cascade_depth_span[2];
		scene_block[290] = state.cascade_blend_effective;
		//设备域写入聚光图集实际状态、矩阵、跨度和槽位映射
		scene_block[300] = state.spot_shadow_atlas ? 1.0f : 0.0f;
		for (std::size_t tile = 0; tile < Device_State::SPOT_SHADOW_TILES; ++tile)
		{
			for (std::size_t index = 0; index < 16; ++index)
				scene_block[304 + tile * 16 + index] = state.spot_view_proj_tiles[tile][index];
			scene_block[432 + tile] = state.spot_shadow_spans[tile];
			scene_block[448 + tile] = state.spot_shadow_bias_tiles[tile];
			scene_block[Device_State::SCENE_SPOT_SOFTNESS_BASE + tile] = state.spot_shadow_softness_tiles[tile];
			scene_block[Device_State::SCENE_SPOT_STRENGTH_BASE + tile] = state.spot_shadow_strength_tiles[tile];
		}
		for (std::size_t slot = 0; slot < 8; ++slot)
			scene_block[440 + slot] = state.spot_shadow_slot_tiles[slot];
		for (std::size_t slot = 0; slot < 24; ++slot)
			scene_block[Device_State::SCENE_SPOT_SLOTS_HI_BASE + slot] = state.spot_shadow_slot_tiles[8 + slot];
		/*
		场景 UBO 按需写入（第六十轮）
		逐帧槽缓存上一次写入的全部浮点（300），按 16 字节（4 浮点）单元比较；
		连续变化单元合并为一段，仅对变化区间调用 buffer_write；
		首次访问某帧槽（缓存未建立）仍整块写入 1200 字节，保证内容完整。
		写入内容与整块写逐字节一致，故画面不变。
		*/
		const std::size_t scene_slot_count = state.scene_ubos.size();
		//若缓存规模与帧槽数不一致则重建（帧槽数由在途帧数决定，运行期恒定）
		if (state.scene_block_cache.size() != scene_slot_count * Device_State::SCENE_BLOCK_FLOATS)
		{
			state.scene_block_cache.assign(scene_slot_count * Device_State::SCENE_BLOCK_FLOATS, 0.0f);
			state.scene_block_cached.assign(scene_slot_count, 0);
		}
		float* slot_cache = state.scene_block_cache.data() + static_cast<std::size_t>(slot) * Device_State::SCENE_BLOCK_FLOATS;
		//本帧实际上传字节数
		std::size_t uploaded_bytes = 0;
		//若该帧槽尚未建立缓存
		if (state.scene_block_cached[slot] == 0)
		{
			//整块写入（与既有行为一致）
			if (!state.memory.buffer_write(state.scene_ubos[slot], state.scene_ubo_allocs[slot], scene_block,
				Device_State::SCENE_BLOCK_BYTES, 0))
			{
				logger.error("渲染器:: 场景 UBO 写入失败（帧槽 {}）", slot);
				return false;
			}
			uploaded_bytes = Device_State::SCENE_BLOCK_BYTES;
		}
		else
		{
			//比较单元：4 浮点 = 16 字节（std140 vec4 对齐）
			constexpr std::size_t UNIT_FLOATS = 4;
			constexpr std::size_t UNIT_BYTES = 16;
			constexpr std::size_t UNIT_COUNT = Device_State::SCENE_BLOCK_FLOATS / UNIT_FLOATS;
			//当前变化段的起始单元与是否处于段内
			std::size_t run_start = 0;
			bool in_run = false;
			//逐单元扫描并合并连续变化区间
			for (std::size_t unit = 0; unit < UNIT_COUNT; ++unit)
			{
				//该单元是否有任一浮点变化（非数视为变化，保证安全上传）
				bool changed = false;
				for (std::size_t offset = 0; offset < UNIT_FLOATS; ++offset)
					if (scene_block[unit * UNIT_FLOATS + offset] != slot_cache[unit * UNIT_FLOATS + offset])
						changed = true;
				//若变化段遇到未变化单元则收束并写入
				if (!changed && in_run)
				{
					const std::size_t segment_bytes = (unit - run_start) * UNIT_BYTES;
					if (!state.memory.buffer_write(state.scene_ubos[slot], state.scene_ubo_allocs[slot],
						scene_block + run_start * UNIT_FLOATS, segment_bytes, run_start * UNIT_BYTES))
					{
						logger.error("渲染器:: 场景 UBO 分段写入失败（帧槽 {}）", slot);
						return false;
					}
					uploaded_bytes += segment_bytes;
					in_run = false;
				}
				//若未在段内且该单元变化则开启新段
				if (changed && !in_run)
				{
					run_start = unit;
					in_run = true;
				}
			}
			//收尾：最后一段延伸到末尾
			if (in_run)
			{
				const std::size_t segment_bytes = (UNIT_COUNT - run_start) * UNIT_BYTES;
				if (!state.memory.buffer_write(state.scene_ubos[slot], state.scene_ubo_allocs[slot],
					scene_block + run_start * UNIT_FLOATS, segment_bytes, run_start * UNIT_BYTES))
				{
					logger.error("渲染器:: 场景 UBO 分段写入失败（帧槽 {}）", slot);
					return false;
				}
				uploaded_bytes += segment_bytes;
			}
		}
		//更新该帧槽缓存（与已写入内容一致；第一百七十九轮起 296 浮点）
		for (std::size_t index = 0; index < Device_State::SCENE_BLOCK_FLOATS; ++index)
			slot_cache[index] = scene_block[index];
		state.scene_block_cached[slot] = 1;
		//累计上传字节（观测用）
		state.scene_upload_bytes += uploaded_bytes;
		return true;
	}
}
