#include "../局部命名空间使用.h"
//实现体定义（提供 Vulkan / 模块类型）
#include "渲染器实现.h"
//内部工具与帧阶段声明
#include "渲染器内部工具.h"
//帧内共享辅助（合批 / 阴影签名）
#include "渲染器_帧内部.h"

namespace engine
{
	bool frame_stage_scene_ubo(Renderer_Impl_Holder::Impl* impl, uint32_t slot, const float view_proj[16], bool spot_shadow_light_valid)
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
		float scene_block[Renderer_Impl_Holder::Impl::SCENE_BLOCK_FLOATS] = {};
		//视图 × 投影矩阵（列主序 16 浮点）
		for (std::size_t index = 0; index < 16; ++index)
			scene_block[index] = view_proj[index];
		//光照公共块：环境光 / 高光强度 / 光泽度 / 点光源数量
		scene_block[16] = impl->light_ambient;
		scene_block[17] = impl->specular_intensity;
		scene_block[18] = impl->specular_shininess;
		scene_block[19] = static_cast<float>(impl->point_light_count);
		//方向光方向 / 强度 / 颜色 / 衰减模式
		scene_block[20] = impl->light_direction[0];
		scene_block[21] = impl->light_direction[1];
		scene_block[22] = impl->light_direction[2];
		scene_block[23] = impl->light_intensity;
		scene_block[24] = impl->light_color[0];
		scene_block[25] = impl->light_color[1];
		scene_block[26] = impl->light_color[2];
		scene_block[27] = static_cast<float>(impl->attenuation_mode);
		//相机世界位置（镜面高光的视线方向来源）；w = 方向光逐光高光缩放（第五十轮）
		float camera_position[3] = {};
		impl->camera.position_get(camera_position);
		scene_block[28] = camera_position[0];
		scene_block[29] = camera_position[1];
		scene_block[30] = camera_position[2];
		scene_block[31] = impl->directional_specular;
		//点光源：旧 8 槽沿用原偏移；8..15 槽追加到块尾（位置段起点 = 高段基址）
		for (uint32_t light = 0; light < Renderer_Impl_Holder::Impl::POINT_LIGHT_UBO_MAX; ++light)
		{
			const std::size_t base = light < Renderer_Impl_Holder::Impl::UBO_LEGACY_SLOTS
				? 32 + static_cast<std::size_t>(light) * 4
				: Renderer_Impl_Holder::Impl::SCENE_HIGH_SLOTS_BASE
					+ static_cast<std::size_t>((light - Renderer_Impl_Holder::Impl::UBO_LEGACY_SLOTS) / 8) * Renderer_Impl_Holder::Impl::SCENE_HIGH_GROUP_FLOATS
					+ static_cast<std::size_t>((light - Renderer_Impl_Holder::Impl::UBO_LEGACY_SLOTS) % 8) * 4;
			scene_block[base + 0] = impl->point_lights[light].position[0];
			scene_block[base + 1] = impl->point_lights[light].position[1];
			scene_block[base + 2] = impl->point_lights[light].position[2];
			scene_block[base + 3] = impl->point_lights[light].radius;
			//颜色段起点 = 同段位置 32 浮点之后
			scene_block[base + 32 + 0] = impl->point_lights[light].color[0];
			scene_block[base + 32 + 1] = impl->point_lights[light].color[1];
			scene_block[base + 32 + 2] = impl->point_lights[light].color[2];
			scene_block[base + 32 + 3] = impl->point_lights[light].intensity;
		}
		//聚光参数：旧 8 槽前 32 浮点为「方向 ＋ 有效标志」、后 32 浮点为锥参数；8..15 槽追加到块尾
		for (uint32_t light = 0; light < Renderer_Impl_Holder::Impl::POINT_LIGHT_UBO_MAX; ++light)
		{
			const std::size_t base = light < Renderer_Impl_Holder::Impl::UBO_LEGACY_SLOTS
				? 96 + static_cast<std::size_t>(light) * 4
				: Renderer_Impl_Holder::Impl::SCENE_HIGH_SLOTS_BASE + 64
					+ static_cast<std::size_t>((light - Renderer_Impl_Holder::Impl::UBO_LEGACY_SLOTS) / 8) * Renderer_Impl_Holder::Impl::SCENE_HIGH_GROUP_FLOATS
					+ static_cast<std::size_t>((light - Renderer_Impl_Holder::Impl::UBO_LEGACY_SLOTS) % 8) * 4;
			scene_block[base + 0] = impl->point_lights[light].spot_direction[0];
			scene_block[base + 1] = impl->point_lights[light].spot_direction[1];
			scene_block[base + 2] = impl->point_lights[light].spot_direction[2];
			scene_block[base + 3] = impl->point_lights[light].is_spot ? 1.0f : 0.0f;
			//锥参数段起点 = 同段方向 32 浮点之后
			scene_block[base + 32 + 0] = impl->point_lights[light].spot_inner_cos;
			scene_block[base + 32 + 1] = impl->point_lights[light].spot_outer_cos;
			scene_block[base + 32 + 2] = static_cast<float>(impl->point_lights[light].attenuation);
			//w = 逐盏高光缩放（第四十九轮）
			scene_block[base + 32 + 3] = impl->point_lights[light].specular_scale;
		}
		//雾参数（雾色 ＋ 密度 ＋ 区间；第六十二轮起点后移 64 浮点）
		scene_block[160] = impl->fog_color[0];
		scene_block[161] = impl->fog_color[1];
		scene_block[162] = impl->fog_color[2];
		scene_block[163] = impl->fog_density;
		scene_block[164] = impl->fog_start;
		scene_block[165] = impl->fog_end;
		//雾模式（第一百四十三轮：0 = 线性区间、1 = 指数平方）
		scene_block[166] = static_cast<float>(impl->fog_mode);
		//雾高度衰减系数（第一百四十三轮）
		scene_block[167] = impl->fog_height_falloff;
		//环境光颜色（第四十轮）
		scene_block[168] = impl->ambient_color[0];
		scene_block[169] = impl->ambient_color[1];
		scene_block[170] = impl->ambient_color[2];
		//半球环境光（第四十七轮：rgb = 地面色、w = 混合强度）
		for (std::size_t index = 0; index < 4; ++index)
			scene_block[172 + index] = impl->ambient_ground[index];
		//补光方向与强度（第四十七轮）
		scene_block[176] = impl->fill_direction[0];
		scene_block[177] = impl->fill_direction[1];
		scene_block[178] = impl->fill_direction[2];
		scene_block[179] = impl->fill_intensity;
		//补光颜色（第四十七轮）＋ 逐光高光缩放（第五十轮；w）
		scene_block[180] = impl->fill_color[0];
		scene_block[181] = impl->fill_color[1];
		scene_block[182] = impl->fill_color[2];
		scene_block[183] = impl->fill_specular;
		//逐盏光泽度（第五十一轮；旧 8 槽在偏移 184 + light × 4；8..15 槽追加到块尾 588 起）
		for (uint32_t light = 0; light < Renderer_Impl_Holder::Impl::POINT_LIGHT_UBO_MAX; ++light)
		{
			const std::size_t offset = light < Renderer_Impl_Holder::Impl::UBO_LEGACY_SLOTS
				? 184 + static_cast<std::size_t>(light) * 4
				: Renderer_Impl_Holder::Impl::SCENE_HIGH_SLOTS_BASE + 128
					+ static_cast<std::size_t>((light - Renderer_Impl_Holder::Impl::UBO_LEGACY_SLOTS) / 8) * Renderer_Impl_Holder::Impl::SCENE_HIGH_GROUP_FLOATS
					+ static_cast<std::size_t>((light - Renderer_Impl_Holder::Impl::UBO_LEGACY_SLOTS) % 8) * 4;
			scene_block[offset] = impl->point_lights[light].shininess;
		}
		/*
		光源空间矩阵沿用 ⑤a 结果（第五十二轮；第四百八十四轮起不再被聚光灯覆盖）
		写入 UBO 供场景片元解包比较；阴影关闭时字段保留但不参与计算。
		*/
		for (std::size_t index = 0; index < 16; ++index)
			scene_block[216 + index] = impl->light_view_proj[index];
		//阴影参数（第五十二轮：x = 启用、y = 偏置、z = 阴影图边长；第五十三轮 w = 软度）
		/*
		阴影总开关（第四百七十四 / 484 轮）
		方向光阴影与聚光阴影各自独立：任一开启即 shadow_params.x = 1；
		cascade_params.x 仅表示方向光阴影是否启用（0 = 方向光阴影关闭，着色器让位）。
		*/
		scene_block[232] = (impl->shadow_enabled || spot_shadow_light_valid) ? 1.0f : 0.0f;
		scene_block[233] = impl->shadow_bias;
		scene_block[234] = static_cast<float>(impl->shadow_size);
		scene_block[235] = impl->shadow_softness;
		//阴影强度（第五十三轮：0 = 阴影无效、1 = 全遮蔽）
		scene_block[236] = impl->shadow_strength;
		//曝光补偿（第六百六十轮；EV，0 = 关闭）
		scene_block[237] = impl->exposure;
		//PBR-lite 参数（第六十四轮：x = 启用、y = 粗糙度缩放；第六十五轮 z = 金属度）
		scene_block[240] = impl->pbr_enabled ? 1.0f : 0.0f;
		scene_block[241] = impl->pbr_roughness;
		scene_block[242] = impl->pbr_metallic;
		/*
		场景缓冲尾部扩展（第六百七十二轮；D1 第一步）
		456 = PBR 掠射高光能量修正开关；457..459 保留（保持 vec4 对齐）。
		*/
		scene_block[456] = impl->pbr_energy ? 1.0f : 0.0f;
		//PBR 粗糙度绝对口径（第七百轮；1 = 材质 / 全局粗糙度直接作为 GGX 粗糙度）
		scene_block[457] = impl->pbr_roughness_absolute ? 1.0f : 0.0f;
		//二次元风格化色阶数（第一百七十四轮；w 槽；0 = 关闭）
		scene_block[243] = static_cast<float>(impl->toon_steps);
		/*
		级联阴影参数（第一百二十一轮；第一百四十六轮扩为三级）
		244..259 为远区级联矩阵（单级时为零矩阵）；260..263 为级联参数：
		x = 级联数（1 / 2 / 3）、y = 第 1 个切换距离（与相机的世界距离）、
		z = 第 1 片深度跨度、w = 第 2 片深度跨度（供着色器换算法线偏移；单级时均为 59）。
		272..287 为第三级矩阵、288..291 为第三级参数：
		x = 第 2 个切换距离（= 切换距离 × 远区倍数）、y = 第 3 片深度跨度。
		*/
		for (std::size_t index = 0; index < 16; ++index)
			scene_block[244 + index] = impl->light_view_proj_far[index];
		/*
		级联参数 x（第四百七十四 / 484 轮）
		仅表示方向光阴影是否启用：方向光关闭时写 0（着色器 shadow_factor 让位）；
		聚光阴影的开关与序号由 spot_shadow_params.w 表达，两图互不依赖。
		*/
		scene_block[260] = impl->shadow_enabled ? static_cast<float>(impl->cascade_count) : 0.0f;
		scene_block[261] = impl->cascade_split_distance;
		scene_block[262] = impl->cascade_depth_span[0];
		scene_block[263] = impl->cascade_depth_span[1];
		for (std::size_t index = 0; index < 16; ++index)
			scene_block[272 + index] = impl->light_view_proj_far2[index];
		scene_block[288] = impl->cascade_split_distance * impl->cascade_far_scale;
		scene_block[289] = impl->cascade_depth_span[2];
		//级联接缝淡化带宽比例（第一百四十八轮；z 槽；第六百六十一轮改用「生效比例」）
		scene_block[290] = impl->cascade_blend_effective;
		/*
		相机近远平面（第一百二十三轮）
		264 = 近平面、265 = 远平面（世界单位）；供景深片元把深度缓冲换算成以相机为原点的距离。
		*/
		float clip_near = 0.0f;
		float clip_far = 0.0f;
		impl->camera.clip_get(&clip_near, &clip_far);
		scene_block[264] = clip_near;
		scene_block[265] = clip_far;
		/*
		二次元暗部色调（第一百七十八轮；复用三个保留分量）
		266 = 红、267 = 绿（camera_range.zw），291 = 蓝（cascade_params2.w）；
		默认 (1,1,1) 时着色器乘性恒等，画面与引入前逐字节一致。
		*/
		scene_block[266] = impl->toon_shadow_tint[0];
		scene_block[267] = impl->toon_shadow_tint[1];
		/*
		天空盒参数（第一百二十五轮）
		268 = 启用（> 0.5 生效）、269 = 漫反射（环境）强度、270 = 镜面强度、271 保留；
		未启用时着色器整段跳过，画面与引入前逐字节一致。
		*/
		scene_block[268] = impl->sky_enabled ? 1.0f : 0.0f;
		scene_block[269] = impl->sky_ambient;
		scene_block[270] = impl->sky_specular;
		//二次元风格化边缘光强度（第一百七十四轮；w 槽；0 = 关闭）
		scene_block[271] = impl->toon_rim;
		//二次元暗部色调蓝分量（第一百七十八轮；w 槽；与 266 / 267 共同构成 RGB）
		scene_block[291] = impl->toon_shadow_tint[2];
		/*
		卡通高光参数（第一百七十九轮；x = 阈值 0..1、0 = 关闭；yzw 保留）
		场景片元按「高光亮度 ≥ 阈值」二值化高光，形成硬边高光块；阈值为 0 时整段短路。
		*/
		scene_block[292] = impl->toon_specular;
		/*
		PCSS 软阴影参数（第六百五十七轮；复用卡通高光参数的空闲分量）
		293 = 开关（> 0.5 生效）、294 = 阻塞物搜索尺度（纹素）、295 = PCF 半径上限（纹素）；
		293 为 0（默认）时片元走旧硬边 / 3×3 软度口径，画面逐字节不变。
		*/
		scene_block[293] = impl->shadow_pcss_enabled ? 1.0f : 0.0f;
		scene_block[294] = impl->shadow_pcss_search;
		scene_block[295] = impl->shadow_pcss_max;
		/*
		簇参数（第二百八十五轮写入；第二百八十九轮由场景片元读取）
		296 = Cluster 启用（> 0.5 时片元改读逐簇光源列表）、297..299 = 网格 x / y / z；
		296 为 0（默认）时着色器走原 8 盏前向循环，画面逐字节不变。
		*/
		scene_block[296] = impl->cluster_enabled ? 1.0f : 0.0f;
		scene_block[297] = static_cast<float>(impl->cluster_grid.x);
		scene_block[298] = static_cast<float>(impl->cluster_grid.y);
		scene_block[299] = static_cast<float>(impl->cluster_grid.z);
		/*
		聚光灯阴影参数（第四百七十六 / 479 轮；x = 图集开关（第六百七十轮）、y = 软度、z = 强度、w = 开关）
		x > 0.5 时着色器按瓦片号换算图集像素原点；w > 0.5 表示本帧存在有效聚光阴影。
		*/
		scene_block[300] = impl->spot_shadow_atlas ? 1.0f : 0.0f;
		scene_block[301] = impl->spot_shadow_softness;
		scene_block[302] = impl->spot_shadow_strength;
		//w = 开关（第五百零七轮起不再编码序号，序号改由槽位映射表达）
		scene_block[303] = spot_shadow_light_valid ? 1.0f : 0.0f;
		/*
		聚光灯阴影矩阵 / 跨度 / 槽位映射（第五百零七轮；第五百一十三轮扩为 8 瓦片；第五百二十一轮追加逐瓦片偏置）
		304..431 = 8 份透视矩阵；432..439 = 8 份深度跨度；440..447 = 8 个前向槽位的瓦片号（-1 = 不投影）；448..455 = 8 份有效偏置。
		*/
		for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
			for (std::size_t index = 0; index < 16; ++index)
				scene_block[304 + tile * 16 + index] = impl->spot_view_proj_tiles[tile][index];
		for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
			scene_block[432 + tile] = impl->spot_shadow_spans[tile];
		for (std::size_t slot = 0; slot < 8; ++slot)
			scene_block[440 + slot] = impl->spot_shadow_slot_tiles[slot];
		//槽位 8..31 的瓦片映射（第七百零九轮；956..979）
		for (std::size_t slot = 0; slot < 24; ++slot)
			scene_block[Renderer_Impl_Holder::Impl::SCENE_SPOT_SLOTS_HI_BASE + slot] = impl->spot_shadow_slot_tiles[8 + slot];
		//逐瓦片有效偏置（第五百二十一轮；448..455）
		for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
			scene_block[448 + tile] = impl->spot_shadow_bias_tiles[tile];
		/*
		逐瓦片质量缩放（第七百零八轮）
		940..947 = 软度缩放、948..955 = 强度缩放；默认 1（与全局参数相同，画面逐字节不变）。
		*/
		for (std::size_t tile = 0; tile < Renderer_Impl_Holder::Impl::SPOT_SHADOW_TILES; ++tile)
		{
			scene_block[Renderer_Impl_Holder::Impl::SCENE_SPOT_SOFTNESS_BASE + tile] = impl->spot_shadow_softness_tiles[tile];
			scene_block[Renderer_Impl_Holder::Impl::SCENE_SPOT_STRENGTH_BASE + tile] = impl->spot_shadow_strength_tiles[tile];
		}
		/*
		场景 UBO 按需写入（第六十轮）
		逐帧槽缓存上一次写入的全部浮点（300），按 16 字节（4 浮点）单元比较；
		连续变化单元合并为一段，仅对变化区间调用 buffer_write；
		首次访问某帧槽（缓存未建立）仍整块写入 1200 字节，保证内容完整。
		写入内容与整块写逐字节一致，故画面不变。
		*/
		const std::size_t scene_slot_count = impl->scene_ubos.size();
		//若缓存规模与帧槽数不一致则重建（帧槽数由在途帧数决定，运行期恒定）
		if (impl->scene_block_cache.size() != scene_slot_count * Renderer_Impl_Holder::Impl::SCENE_BLOCK_FLOATS)
		{
			impl->scene_block_cache.assign(scene_slot_count * Renderer_Impl_Holder::Impl::SCENE_BLOCK_FLOATS, 0.0f);
			impl->scene_block_cached.assign(scene_slot_count, 0);
		}
		float* slot_cache = impl->scene_block_cache.data() + static_cast<std::size_t>(slot) * Renderer_Impl_Holder::Impl::SCENE_BLOCK_FLOATS;
		//本帧实际上传字节数
		std::size_t uploaded_bytes = 0;
		//若该帧槽尚未建立缓存
		if (impl->scene_block_cached[slot] == 0)
		{
			//整块写入（与既有行为一致）
			if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot], scene_block,
				Renderer_Impl_Holder::Impl::SCENE_BLOCK_BYTES, 0))
			{
				logger.error("渲染器:: 场景 UBO 写入失败（帧槽 {}）", slot);
				return false;
			}
			uploaded_bytes = Renderer_Impl_Holder::Impl::SCENE_BLOCK_BYTES;
		}
		else
		{
			//比较单元：4 浮点 = 16 字节（std140 vec4 对齐）
			constexpr std::size_t UNIT_FLOATS = 4;
			constexpr std::size_t UNIT_BYTES = 16;
			constexpr std::size_t UNIT_COUNT = Renderer_Impl_Holder::Impl::SCENE_BLOCK_FLOATS / UNIT_FLOATS;
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
					if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
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
				if (!impl->memory.buffer_write(impl->scene_ubos[slot], impl->scene_ubo_allocs[slot],
					scene_block + run_start * UNIT_FLOATS, segment_bytes, run_start * UNIT_BYTES))
				{
					logger.error("渲染器:: 场景 UBO 分段写入失败（帧槽 {}）", slot);
					return false;
				}
				uploaded_bytes += segment_bytes;
			}
		}
		//更新该帧槽缓存（与已写入内容一致；第一百七十九轮起 296 浮点）
		for (std::size_t index = 0; index < Renderer_Impl_Holder::Impl::SCENE_BLOCK_FLOATS; ++index)
			slot_cache[index] = scene_block[index];
		impl->scene_block_cached[slot] = 1;
		//累计上传字节（观测用）
		impl->scene_upload_bytes += uploaded_bytes;
		return true;
	}
}
