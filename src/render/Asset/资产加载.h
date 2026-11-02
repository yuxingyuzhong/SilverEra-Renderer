#pragma once
//预编译头
#include "Application/Renderer/common/前置头文件包含.h"
//网格数据类型（Vertex / Mesh_Data / Mesh_Section）
#include "Application/Renderer/src/render/Mesh/网格数据.h"

//游戏引擎命名空间
namespace engine
{
	/*
	网格文件数据（第四百二十二轮；资产加载方向第一阶段的返回结构）
	data      —— 顶点与索引（可直接交给门面 mesh_load / mesh_load_sections）
	sections  —— 按 usemtl 分组的子网格段（material 为组序号 0,1,2…）
	names     —— 段序号 → 材质名（OBJ 里的 usemtl 名称；空串表示该文件未分材质）
	*/
	struct Mesh_File_Data
	{
		//顶点与索引
		Mesh_Data data;
		//子网格分段（至少一段；未分材质时只有一段 material = 0）
		std::vector<Mesh_Section> sections;
		//段序号对应的材质名
		std::vector<std::string> names;
	};

	/*
	从 OBJ 文件加载网格（第四百二十二轮）
	支持：v / vt / vn / f（含多边形扇形三角化、负索引）、usemtl 分组；
	忽略：mtllib、o、g、s、注释与其余行。
	缺 vt 时纹理坐标填 (0,0)；缺 vn 时按相邻三角形面积加权自动生成顶点法线。
	失败（路径不可读 / 超限 / 语法非法 / 无有效三角面）时返回假，且 out 被清空。
	*/
	bool obj_mesh_load(const std::string& path, Mesh_File_Data& out);

	/*
	MTL 材质项（第四百二十三轮；资产加载方向第二阶段）
	只承载 OBJ 材质库里的原始字段，不直接构造 Material_Data——
	由门面 `material_load_mtl` 负责「字段换算 ＋ 贴图解码 ＋ 材质创建」。
	*/
	struct Mtl_Material
	{
		//材质名（与 OBJ 的 usemtl 对应）
		std::string name;
		//漫反射颜色（Kd；缺省白）
		float diffuse[3] = { 1.0f, 1.0f, 1.0f };
		//镜面颜色（Ks；缺省黑）
		float specular[3] = { 0.0f, 0.0f, 0.0f };
		//自发光颜色（Ke；缺省黑）
		float emissive[3] = { 0.0f, 0.0f, 0.0f };
		//镜面指数（Ns；0..1000，缺省 0）
		float shininess = 0.0f;
		//不透明度（d；缺省 1；Tr 按 1 − Tr 换算）
		float opacity = 1.0f;
		//漫反射贴图（map_Kd；相对 MTL 所在目录，空串表示无）
		std::string diffuse_map;
		//法线 / 凹凸贴图（map_Bump / bump / norm；同上）
		std::string normal_map;
	};

	/*
	从 MTL 文件读取材质库（第四百二十三轮）
	支持 newmtl / Kd / Ks / Ke / Ns / d / Tr / map_Kd / map_Bump（bump / norm 视同）；
	其余行（Ka / Ni / illum / 注释等）忽略。失败（路径不可读 / 超限 / 无任何 newmtl）返回假并清空输出。
	贴图路径原样保留（不解析相对目录）；解析目录由调用方处理。
	*/
	bool mtl_materials_load(const std::string& path, std::vector<Mtl_Material>& out);

	/*
	场景清单（第四百二十四轮；资产加载方向第三阶段）
	纯文本行格式（UTF-8，一行一条，`#` 起首为注释）：
	  mesh  <网格ID> <OBJ 路径>
	  mtl   <材质ID> <MTL 路径>
	  place <网格ID> [材质ID] <x> <y> <z> <sx> <sy> <sz> [<rx> <ry> <rz>]
	路径相对清单文件所在目录；`place` 的材质列可省略（表示无材质）；
	旋转三轴可省略（度，缺省 0 = 不旋转；第四百三十一轮）。
	本结构只做文本解析，不建 GPU 资源；资源创建由门面 `scene_load` 完成。
	*/
	struct Scene_Mesh_Ref
	{
		//网格 ID（清单内唯一）
		std::string id;
		//OBJ 路径（相对清单目录）
		std::string path;
	};

	//清单里的材质库引用
	struct Scene_Mtl_Ref
	{
		//材质 ID（清单内唯一）
		std::string id;
		//MTL 路径（相对清单目录）
		std::string path;
	};

	//清单里的一个摆放记录
	struct Scene_Place
	{
		//网格 ID
		std::string mesh_id;
		//材质 ID（空串表示无材质）
		std::string material_id;
		//位置（世界坐标）
		float position[3] = { 0.0f, 0.0f, 0.0f };
		//缩放（三轴）
		float scale[3] = { 1.0f, 1.0f, 1.0f };
		/*
		欧拉角旋转（度；第四百三十一轮）
		顺序 X → Y → Z（变换矩阵 R = Rz·Ry·Rx）；缺省全零 = 不旋转。
		*/
		float rotation_deg[3] = { 0.0f, 0.0f, 0.0f };
	};

	//整份清单
	struct Scene_Manifest
	{
		//网格引用
		std::vector<Scene_Mesh_Ref> meshes;
		//材质库引用
		std::vector<Scene_Mtl_Ref> materials;
		//摆放记录
		std::vector<Scene_Place> places;
	};

	/*
	解析场景清单（第四百二十四轮；第四百六十七轮增可引用实例名）
	失败（路径不可读 / 超限 / 未知指令 / 字段缺失 / ID 重复）返回假并清空输出；
	仅做语法与「ID 是否被引用」的静态检查，不校验 OBJ / MTL 文件是否存在。
	`实例名表` 为关卡材质实例声明（方向七阶段 B）；`place` 的材质列命中实例名即视为合法，
	未命中再查材质库；清单文件版一般传空表。
	*/
	bool scene_manifest_parse(const std::string& path, Scene_Manifest& out,
		const std::vector<std::string>& 实例名表 = {});

	/*
	按内存行解析场景清单（第四百三十轮；第四百六十七轮增可引用实例名）
	行内容与 `scene_manifest_parse` 完全同语法（`mesh` / `mtl` / `place`，`#` 注释、空行忽略）；
	供关卡装载复用（关卡文件里的资产段落不落成独立清单文件）。
	失败口径同文件版（含「至少一条 place」），失败时输出清空。
	*/
	bool scene_manifest_parse_lines(const std::vector<std::string>& lines, Scene_Manifest& out,
		const std::vector<std::string>& 实例名表 = {});
}
