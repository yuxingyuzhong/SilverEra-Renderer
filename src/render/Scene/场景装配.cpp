//场景装配实现：消费系统层中立描述，落到渲染层资源与绘制登记
#include "src/render/Scene/场景装配.h"

//日志系统
#include "Engine/EngineFoundation/src/log/日志系统运行包.h"
//骨架 / 片段资产加载与姿态求值（演员装配）
#include "Engine/EngineRenderer/src/render/Animation/动画片段.h"
#include "Engine/EngineRenderer/src/render/Animation/动画姿态.h"

//浮动与整数

namespace engine
{
	namespace
	{
		//材质库项（材质名与句柄按下标对齐，供实例按模板名解析）
		struct 材质库项
		{
			//材质库 ID
			std::string id;
			//材质名
			std::vector<std::string> names;
			//句柄组
			std::vector<Material_Handle> handles;
		};

		//回退本次已建的演员调色板区间
		void 演员回退(Renderer& renderer, Asset_Scene& scene_out)
		{
			for (const Asset_Actor& 演员 : scene_out.actors)
				renderer.animation().actor_destroy(演员.palette_offset);
			scene_out.actors.clear();
		}

		//回退本次已建的网格与材质
		void 资产回退(Renderer& renderer, const std::vector<std::pair<std::string, Mesh_Handle>>& 网格表,
			const std::vector<材质库项>& 材质表, Asset_Scene& scene_out)
		{
			//回退材质
			for (const 材质库项& 库 : 材质表)
				for (Material_Handle 句柄 : 库.handles)
					renderer.resource().material_unload(句柄);
			//回退网格
			for (const std::pair<std::string, Mesh_Handle>& item : 网格表)
				renderer.resource().mesh_unload(item.second);
			//回退演员调色板区间
			演员回退(renderer, scene_out);
			scene_out.meshes.clear();
			scene_out.materials.clear();
			scene_out.objects.clear();
		}
	}

	namespace
	{
		/*
		按清单装配资产（与旧 `asset_scene_build` 逐条对应）
		调用方已完成清单解析与目录计算；本函数负责资源创建、对象装配与失败整体回退。
		同一网格 / 材质库只加载一次，place 按顺序复用句柄。
		*/
		bool 资产装配(Renderer& renderer, const Scene_Manifest& 清单, const std::string& 目录,
			const std::vector<Level_Material>& 实例声明, Asset_Scene& scene_out)
		{
			scene_out.objects.clear();
			scene_out.meshes.clear();
			scene_out.materials.clear();

			//网格（ID → 句柄）
			std::vector<std::pair<std::string, Mesh_Handle>> 网格表;
			网格表.reserve(清单.meshes.size());
			for (const Scene_Mesh_Ref& ref : 清单.meshes)
			{
				Mesh_Handle 句柄 = 0;
				if (!renderer.resource().mesh_load_obj(句柄, 目录 + ref.path))
				{
					logger.warn("关卡装配:: 清单网格加载失败（{}），整体回退", ref.path);
					//此时尚未建任何材质库
					const std::vector<材质库项> 空材质表;
					资产回退(renderer, 网格表, 空材质表, scene_out);
					return false;
				}
				网格表.emplace_back(ref.id, 句柄);
				scene_out.meshes.push_back(句柄);
			}

			//材质库（ID → 名称 ＋ 句柄组）
			std::vector<材质库项> 材质表;
			材质表.reserve(清单.materials.size());
			for (const Scene_Mtl_Ref& ref : 清单.materials)
			{
				材质库项 项;
				项.id = ref.id;
				if (!renderer.resource().material_load_mtl(项.handles, 目录 + ref.path, &项.names))
				{
					logger.warn("关卡装配:: 清单材质库加载失败（{}），整体回退", ref.path);
					资产回退(renderer, 网格表, 材质表, scene_out);
					return false;
				}
				for (Material_Handle 句柄 : 项.handles)
					scene_out.materials.push_back(句柄);
				材质表.push_back(std::move(项));
			}

			//材质实例表（实例名 → 覆盖数据）
			std::vector<std::pair<std::string, Material_Instance_Data>> 实例表;
			实例表.reserve(实例声明.size());
			for (const Level_Material& 声明 : 实例声明)
			{
				//按模板材质名在全库中找句柄
				Material_Handle 模板 = 0;
				for (const 材质库项& 库 : 材质表)
				{
					for (std::size_t index = 0; index < 库.names.size() && index < 库.handles.size(); ++index)
					{
						if (库.names[index] == 声明.模板)
						{
							模板 = 库.handles[index];
							break;
						}
					}
					if (模板 != 0)
						break;
				}
				//模板未命中
				if (模板 == 0)
				{
					logger.warn("关卡装配:: 材质实例 {} 的模板 {} 未命中，整体回退", 声明.实例名, 声明.模板);
					资产回退(renderer, 网格表, 材质表, scene_out);
					return false;
				}
				Material_Instance_Data 数据;
				数据.模板 = 模板;
				for (std::size_t index = 0; index < 4; ++index)
					数据.tint[index] = 声明.tint[index];
				数据.use_tint = 声明.use_tint;
				数据.emissive = 声明.emissive;
				数据.use_emissive = 声明.use_emissive;
				数据.use_toon = 声明.use_toon;
				数据.toon_steps = 声明.toon_steps;
				数据.toon_rim = 声明.toon_rim;
				数据.toon_specular = 声明.toon_specular;
				数据.use_pbr = 声明.use_pbr;
				数据.pbr_roughness = 声明.pbr_roughness;
				数据.pbr_metallic = 声明.pbr_metallic;
				数据.use_clearcoat = 声明.use_clearcoat;
				数据.clear_coat = 声明.clear_coat;
				数据.clear_coat_roughness = 声明.clear_coat_roughness;
				//实例数据校验（模板已登记 ＋ 覆盖值合法）
				if (!renderer.resource().material_instance_validate(数据))
				{
					logger.warn("关卡装配:: 材质实例 {} 数据非法，整体回退", 声明.实例名);
					资产回退(renderer, 网格表, 材质表, scene_out);
					return false;
				}
				实例表.emplace_back(声明.实例名, 数据);
			}

			//按 place 顺序装配对象
			for (const Scene_Place& place : 清单.places)
			{
				Asset_Object object;
				//网格句柄（解析阶段已保证 ID 存在）
				for (const std::pair<std::string, Mesh_Handle>& item : 网格表)
				{
					if (item.first == place.mesh_id)
					{
						object.mesh = item.second;
						break;
					}
				}
				//材质列（可选）：先查实例表，未命中再查材质库
				if (!place.material_id.empty())
				{
					for (const std::pair<std::string, Material_Instance_Data>& item : 实例表)
					{
						if (item.first == place.material_id)
						{
							object.materials.push_back(item.second.模板);
							object.material_instances.push_back(item.second);
							break;
						}
					}
					//实例未命中：按材质库 ID 取整组句柄
					if (object.materials.empty())
						for (const 材质库项& 库 : 材质表)
						{
							if (库.id == place.material_id)
							{
								object.materials = 库.handles;
								break;
							}
						}
				}
				//位置 / 缩放 / 旋转
				for (std::size_t index = 0; index < 3; ++index)
				{
					object.position[index] = place.position[index];
					object.scale[index] = place.scale[index];
					object.rotation_deg[index] = place.rotation_deg[index];
				}
				scene_out.objects.push_back(object);
			}
			return true;
		}

		/*
		按关卡演员声明装配（方向十 · 收敛轮 861）
		逐演员加载骨架（必需）与片段（可选），分配调色板区间并登记到场景。
		任一演员失败即返回假；调用方统一回退（资产回退会一并回收已登记的演员区间）。
		*/
		bool 演员装配(Renderer& renderer, const std::vector<Level_Actor>& 声明, const std::string& 目录,
			Asset_Scene& scene_out)
		{
			for (const Level_Actor& 项 : 声明)
			{
				//骨架路径未声明
				if (项.骨架.empty())
				{
					logger.warn("关卡装配:: 演员 {} 未声明骨架", 项.名称);
					return false;
				}
				//骨架资产
				auto 骨架 = std::make_shared<Animation_Skeleton>();
				if (!animation_skeleton_load_file(目录 + 项.骨架, *骨架))
				{
					logger.warn("关卡装配:: 演员 {} 骨架加载失败（{}）", 项.名称, 项.骨架);
					return false;
				}
				//片段资产（可选）
				std::shared_ptr<const Animation_Clip> 片段;
				if (!项.动画.empty())
				{
					auto 数据 = std::make_shared<Animation_Clip>();
					if (!animation_clip_load_file(目录 + 项.动画, *数据))
					{
						logger.warn("关卡装配:: 演员 {} 片段加载失败（{}）", 项.名称, 项.动画);
						return false;
					}
					片段 = 数据;
				}
				//分配调色板区间
				uint32_t 偏移 = 0;
				if (!renderer.animation().actor_create(骨架->bone_count(), 偏移))
				{
					logger.warn("关卡装配:: 演员 {} 调色板区间分配失败（骨骼数 {}）", 项.名称, 骨架->bone_count());
					return false;
				}
				Asset_Actor 演员;
				演员.name = 项.名称;
				演员.palette_offset = 偏移;
				演员.bone_count = 骨架->bone_count();
				演员.skeleton = 骨架;
				演员.clip = 片段;
				scene_out.actors.push_back(std::move(演员));
			}
			return true;
		}
	}

	//装载关卡
	bool level_bridge_load(Renderer& renderer, Level_Data& level_out, Asset_Scene& scene_out,
		const std::string& path, bool apply_scene)
	{
		//默认无结果
		level_out = Level_Data();
		scene_out = Asset_Scene();
		//读取关卡文本
		if (!level_text_read(path, level_out))
			return false;

		/*
		相机参数校验（与 `scene_apply` 三入参版同口径）
		`scene_apply` 为 void，非法时静默拒绝；关卡装载要求显式失败，故在此先判一次。
		*/
		const Camera_Data& camera = level_out.camera;
		const bool camera_valid = (camera.fov > 0.0f) && (camera.fov < 3.14159265f)
			&& (camera.near_plane > 0.0f) && (camera.far_plane > camera.near_plane)
			&& (!camera.ortho || camera.ortho_height > 0.0f);
		//若相机参数非法
		if (!camera_valid)
		{
			logger.warn("关卡装配:: 相机非法（视场角 {}、近平面 {}、远平面 {}），装载拒绝",
				camera.fov, camera.near_plane, camera.far_plane);
			level_out = Level_Data();
			return false;
		}

		//关卡所在目录（资产路径相对它解析）
		const std::size_t cut = path.find_last_of("/\\");
		const std::string 目录 = (cut == std::string::npos) ? std::string() : path.substr(0, cut + 1);

		//资产段落（为空表示空场景）
		if (!level_out.asset_lines.empty())
		{
			Scene_Manifest 清单;
			//材质实例名表（place 材质列可引用实例）
			std::vector<std::string> 实例名表;
			实例名表.reserve(level_out.materials.size());
			for (const Level_Material& material : level_out.materials)
				实例名表.push_back(material.实例名);
			if (!scene_manifest_parse_lines(level_out.asset_lines, 清单, 实例名表))
			{
				level_out = Level_Data();
				return false;
			}
			//装配资产（失败整体回退）
			if (!资产装配(renderer, 清单, 目录, level_out.materials, scene_out))
			{
				level_out = Level_Data();
				return false;
			}
		}

		//装配演员（方向十 · 收敛轮 861；失败时连同已建资产整体回退）
		if (!level_out.actors.empty() && !演员装配(renderer, level_out.actors, 目录, scene_out))
		{
			level_bridge_unload(renderer, scene_out);
			level_out = Level_Data();
			return false;
		}

		//应用相机 / 光照 / 雾（相机已校验，三组必然生效；仅当调用方要求时）
		if (apply_scene)
			renderer.scene().scene_apply(level_out.camera, level_out.light, level_out.fog);
		return true;
	}

	//释放装配创建的全部资源（幂等）
	void level_bridge_unload(Renderer& renderer, Asset_Scene& scene)
	{
		//网格
		for (Mesh_Handle 句柄 : scene.meshes)
			renderer.resource().mesh_unload(句柄);
		//材质
		for (Material_Handle 句柄 : scene.materials)
			renderer.resource().material_unload(句柄);
		//演员调色板区间（方向十 · 收敛轮 861）
		演员回退(renderer, scene);
		scene.meshes.clear();
		scene.materials.clear();
		scene.objects.clear();
	}

	//装载场景清单文件
	bool scene_bundle_load(Renderer& renderer, Asset_Scene& scene_out, const std::string& path)
	{
		//默认无结果
		scene_out = Asset_Scene();
		//解析清单（清单文件没有材质实例声明）
		Scene_Manifest 清单;
		if (!scene_manifest_parse(path, 清单))
			return false;
		//清单所在目录（保留 UTF-8 编码；相对路径直接前缀拼接）
		const std::size_t cut = path.find_last_of("/\\");
		const std::string 目录 = (cut == std::string::npos) ? std::string() : path.substr(0, cut + 1);
		return 资产装配(renderer, 清单, 目录, {}, scene_out);
	}

	//逐对象构造矩阵并逐段绑材质（返回成功登记的对象数）
	uint32_t level_bridge_draw(Renderer& renderer, const Asset_Scene& scene)
	{
		uint32_t 登记数 = 0;
		for (const Asset_Object& object : scene.objects)
		{
			//对象未持有网格
			if (object.mesh == 0)
				continue;
			//按位置 / 旋转（度）/ 缩放构造变换
			float transform[16] = {};
			level_transform_build(object.position, object.rotation_deg, object.scale, transform);
			/*
			材质实例覆盖
			实例数组与材质数组按段对齐时才套用；否则按旧路径绘制（逐字节不变）。
			*/
			const Material_Instance_Data* 实例指针 = nullptr;
			if (!object.material_instances.empty() && object.material_instances.size() == object.materials.size())
				实例指针 = object.material_instances.data();
			if (renderer.resource().mesh_draw_sections(object.mesh, transform, object.materials.data(),
				static_cast<uint32_t>(object.materials.size()), 实例指针))
				登记数 += 1;
		}
		return 登记数;
	}

	//推进演员动画并写入调色板
	uint32_t level_bridge_animation_update(Renderer& renderer, Asset_Scene& scene, float delta_seconds)
	{
		//非法步长按静止处理
		if (!std::isfinite(delta_seconds) || delta_seconds < 0.0f)
			delta_seconds = 0.0f;
		uint32_t 更新数 = 0;
		for (Asset_Actor& 演员 : scene.actors)
		{
			//骨架缺失（装配阶段不会出现，防御直接跳过）
			if (!演员.skeleton)
				continue;
			//过渡权重推进（到 1 后清空源片段）
			if (演员.blend_from && 演员.blend_speed > 0.0f)
			{
				演员.blend_weight += 演员.blend_speed * delta_seconds;
				if (演员.blend_weight >= 1.0f)
				{
					演员.blend_weight = 1.0f;
					演员.blend_from.reset();
					演员.blend_speed = 0.0f;
				}
			}
			//推进主片段时间（循环回绕 / 非循环夹紧）
			if (演员.clip && 演员.clip->duration > 0.0f)
			{
				const float 时长 = 演员.clip->duration;
				演员.time_seconds += delta_seconds;
				if (演员.loop)
					演员.time_seconds = std::fmod(演员.time_seconds, 时长);
				else if (演员.time_seconds > 时长)
					演员.time_seconds = 时长;
			}
			//推进过渡源片段时间
			if (演员.blend_from && 演员.blend_from->duration > 0.0f)
			{
				const float 时长 = 演员.blend_from->duration;
				演员.blend_from_time += delta_seconds;
				if (演员.loop)
					演员.blend_from_time = std::fmod(演员.blend_from_time, 时长);
				else if (演员.blend_from_time > 时长)
					演员.blend_from_time = 时长;
			}
			//过渡中：双片段混合
			if (演员.blend_from && 演员.clip)
			{
				if (renderer.animation().actor_blend_write(演员.palette_offset, *演员.blend_from, *演员.clip,
					*演员.skeleton, 演员.blend_from_time, 演员.time_seconds, 演员.blend_weight, 演员.loop))
					更新数 += 1;
				continue;
			}
			//单片段：按片段时间采样
			if (演员.clip && 演员.clip->duration > 0.0f)
			{
				if (renderer.animation().actor_clip_write(演员.palette_offset, *演员.clip, *演员.skeleton,
					演员.time_seconds, 演员.loop))
					更新数 += 1;
				continue;
			}
			//无片段：按绑定姿态写调色板
			Animation_Pose 姿态;
			if (!姿态.build(*演员.skeleton))
				continue;
			if (renderer.animation().actor_palette_write(演员.palette_offset, *演员.skeleton, 姿态))
				更新数 += 1;
		}
		return 更新数;
	}

	//切换演员动画片段
	bool level_bridge_animation_transition(Asset_Scene& scene, const std::string& actor_name,
		const std::shared_ptr<const Animation_Clip>& target, float duration_seconds)
	{
		//目标片段与时长前置
		if (!target || !std::isfinite(duration_seconds))
			return false;
		//查找演员
		Asset_Actor* 演员 = nullptr;
		for (Asset_Actor& item : scene.actors)
			if (item.name == actor_name)
			{
				演员 = &item;
				break;
			}
		if (演员 == nullptr)
			return false;
		//立即切换（无当前片段或时长非正）
		if (duration_seconds <= 0.0f || !演员->clip)
		{
			演员->clip = target;
			演员->time_seconds = 0.0f;
			演员->blend_from.reset();
			演员->blend_from_time = 0.0f;
			演员->blend_weight = 1.0f;
			演员->blend_speed = 0.0f;
			return true;
		}
		//淡入：当前片段转为过渡源，按秒数推进权重
		演员->blend_from = 演员->clip;
		演员->blend_from_time = 演员->time_seconds;
		演员->clip = target;
		演员->time_seconds = 0.0f;
		演员->blend_weight = 0.0f;
		演员->blend_speed = 1.0f / duration_seconds;
		return true;
	}
}
