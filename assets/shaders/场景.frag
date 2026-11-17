#version 450
//场景片元着色器
//来自顶点着色器的纹理坐标
layout(location = 0) in vec2 in_uv;
//来自顶点着色器的世界空间法线（零向量 = 未提供）
layout(location = 1) in vec3 in_normal;
//来自顶点着色器的世界空间位置（第三十一轮）
layout(location = 2) in vec3 in_world;
//逐物体材质（第六十八轮；x = 粗糙度缩放、y = 金属度、z = 覆盖标志、w 保留）
layout(location = 3) flat in vec4 in_material;
/*
逐物体卡通参数（第四百零八轮）
x = 色阶数（0 = 不覆盖）、y = 边缘光强度、z = 卡通高光阈值、w = 覆盖标志（> 0.5 时三项整体覆盖）；
覆盖标志为 0（默认）时回落场景 UBO 的全局卡通参数，画面与引入前逐字节一致。
*/
layout(location = 4) flat in vec4 in_toon;
/*
逐物体卡通暗部色调（第四百零九轮）
xyz = 暗部色调、w 保留；仅在覆盖标志（in_toon.w）> 0.5 时参与着色，默认 (1,1,1) 即不色偏。
*/
layout(location = 5) flat in vec4 in_toon_tint;

/*
逐物体反照率色调（第六十九轮）
推送常量前 64 字节为占位矩阵（模型矩阵已由实例属性承载），64..79 为色调 vec4；
门面在非文字路径**恒推送**该块（默认白色），故未显式设置色调时画面逐字节不变。
*/
layout(push_constant) uniform Push_Tint
{
	layout(offset = 64) vec4 tint;
	//材质贴图参数（第一百二十四轮；x = 法线强度（0 = 不启用）、y = 粗糙度贴图启用、zw 保留）
	layout(offset = 80) vec4 material_extras;
	//材质贴图 UV 变换（第一百七十一轮；xy = 缩放、zw = 偏移；默认 (1,1)/(0,0)）
	layout(offset = 96) vec4 material_uv;
	/*
	清漆层参数（第二百三十三轮；x = 清漆强度 0..1、y = 清漆粗糙度 0.03..1、zw 保留）
	x 为 0 时整段跳过，默认 0 使画面与引入前逐字节不变。
	*/
	layout(offset = 112) vec4 clearcoat;
} push_tint;

//颜色附件输出
layout(location = 0) out vec4 out_color;

//反照率贴图（描述符集 1）：线性过滤 ＋ 重复寻址
layout(set = 1, binding = 0) uniform sampler2D albedo;
//阴影图（第五十二轮；set 1 binding 1，RGBA8 打包 32 位深度，以 texelFetch 读取规避插值）
layout(set = 1, binding = 1) uniform sampler2D shadow_map;
//法线贴图（第一百二十四轮；set 1 binding 3，切线空间法线；默认绑定白纹理）
layout(set = 1, binding = 3) uniform sampler2D normal_map;
//粗糙度贴图（第一百二十四轮；set 1 binding 4，取红通道作粗糙度乘子；默认绑定白纹理 → ×1）
layout(set = 1, binding = 4) uniform sampler2D roughness_map;
//天空盒立方体贴图（第一百二十五轮；set 1 binding 5；默认绑定内置纯白立方体）
layout(set = 1, binding = 5) uniform samplerCube sky_map;
//自发光颜色贴图（第一百六十八轮；set 1 binding 6；默认绑定白纹理 → 乘子恒 1）
layout(set = 1, binding = 6) uniform sampler2D emissive_map;
//环境光遮蔽贴图（第一百六十九轮；set 1 binding 7；取红通道；默认白 → 乘子 1）
layout(set = 1, binding = 7) uniform sampler2D ao_map;
//金属度贴图（第一百六十九轮；set 1 binding 8；取红通道乘到金属度；默认白 → ×1）
layout(set = 1, binding = 8) uniform sampler2D metallic_map;
//聚光灯阴影图（第四百八十四轮；set 1 binding 9；与方向光阴影图独立；第五百一十三轮扩为 8 瓦片）
layout(set = 1, binding = 9) uniform sampler2D spot_shadow_map[8];

//场景缓冲（描述符集 0）：视图投影矩阵 ＋ 光照参数 ＋ 相机位置 ＋ 点光源数组，逐帧更新
layout(set = 0, binding = 0) uniform Scene_Buffer
{
	mat4 view_proj;
	//x = 环境光、y = 高光强度、z = 光泽度、w = 有效点光源数量（第三十二轮）
	vec4 light_common;
	//xyz = 方向光传播方向，w = 方向光强度（0 = 关闭）
	vec4 light_dir;
	//rgb = 方向光颜色，w = 距离衰减模式（0 = 线性、1 = 平方反比；第三十六轮）
	vec4 light_color;
	//xyz = 相机世界位置，w = 方向光逐光高光缩放（第五十轮）
	vec4 camera_pos;
	//点光源位置（xyz）＋ 衰减半径（w），最多 8 个（第六十二轮由 4 扩为 8）
	vec4 point_pos_radius[8];
	//点光源颜色（rgb）＋ 强度（w），最多 8 个
	vec4 point_color_intensity[8];
	//聚光灯方向（xyz = 光传播方向）＋ 有效标志（w > 0.5 表示该槽为聚光灯；第三十四轮）
	vec4 point_spot_dir[8];
	//聚光灯内外锥角余弦（x = 内锥、y = 外锥；z = 逐光源衰减模式、w = 逐盏高光缩放；第三十四 / 四十六 / 四十九轮）
	vec4 point_spot_cone[8];
	//雾色（rgb）＋ 密度（w；0 = 关闭；第三十八轮）
	vec4 fog_color_density;
	//雾距离区间（x = 起始、y = 结束；第三十八轮）
	vec4 fog_range;
	//环境光颜色（rgb；第四十轮）
	vec4 ambient_color;
	//半球环境光（rgb = 地面颜色、w = 混合强度 0..1；第四十七轮）
	vec4 ambient_ground;
	//补光方向（xyz = 光传播方向）＋ 强度（w；0 = 关闭；第四十七轮）
	vec4 fill_dir_intensity;
	//补光颜色（rgb；第四十七轮）
	vec4 fill_color;
	//逐盏高光光泽度（std140：每盏占 16 字节；0 = 跟随全局；第五十一轮；第六十二轮扩为 8 盏）
	float point_shininess[8];
	//光源空间视图投影矩阵（第五十二轮；配合 shadow_params 做方向光阴影）
	mat4 light_view_proj;
	//阴影参数（x = 启用 >0.5、y = 深度偏置、z = 阴影图边长（像素）、w = 软度 0..1；第五十二／五十三轮）
	vec4 shadow_params;
	//阴影强度（0 = 阴影无效、1 = 全遮蔽；第五十三轮）
	float shadow_strength;
	//曝光补偿（第六百六十轮；EV 值，0 = 关闭；std140 下占 237，其后 pbr_params 仍对齐 240）
	float exposure;
	//PBR-lite 参数（第六十四轮；x = 启用 > 0.5、y = 粗糙度缩放（默认 1）、z = 金属度（第六十五轮，0..1）、w 保留）
	vec4 pbr_params;
	//远区级联光源空间矩阵（第一百二十一轮；级联数 ≥ 2 时有效，单级时为零矩阵）
	mat4 light_view_proj_far;
	//级联参数（x = 级联数 1/2/3、y = 首个切换距离、z = 第 1 片深度跨度、w = 第 2 片深度跨度；第一百二十一轮）
	vec4 cascade_params;
	//相机近远平面（x = 近平面、y = 远平面；第一百二十三轮，供后处理把深度换回世界距离）
	vec4 camera_range;
	//天空盒参数（第一百二十五轮；x = 启用 > 0.5、y = 漫反射（环境）强度、z = 镜面强度、w 保留）
	vec4 sky_params;
	//第三级级联光源空间矩阵（第一百四十六轮；级联数为 3 时有效，其余为零矩阵）
	mat4 light_view_proj_far2;
	//第三级参数（x = 第 2 个切换距离、y = 第 3 片深度跨度、zw 保留；第一百四十六轮）
	vec4 cascade_params2;
	//卡通高光参数（第一百七十九轮；x = 阈值 0..1、0 = 关闭；yzw 保留）
	vec4 toon_specular_params;
	/*
	簇参数（第二百八十五轮写入、第二百八十九轮读取）
	x = Cluster 启用（> 0.5 生效）、yzw = 网格 x / y / z；
	x 为 0（默认）时下方点光循环走原前向路径，画面逐字节不变。
	*/
	vec4 cluster_params;
	vec4 spot_shadow_params;
	/*
	聚光灯阴影矩阵 / 跨度 / 槽位映射 / 有效偏置（第四百八十三 / 484 轮；第五百零七轮扩为 4 瓦片；第五百一十三轮扩为 8 瓦片；第五百二十一轮追加逐瓦片偏置）
	spot_view_proj_tiles = 8 盏聚光灯各自的透视视图投影（未使用瓦片为零矩阵）；
	spot_shadow_spans = 8 份深度跨度（法线偏移换算）；
	spot_shadow_slots = 8 个前向槽位的瓦片号（-1 = 该灯不投影），每 4 个一组；
	spot_shadow_biases = 8 份有效偏置（默认等于全局偏置；开启自校准后按锥远平面归一化）。
	*/
	mat4 spot_view_proj_tiles[8];
	vec4 spot_shadow_spans[2];
	vec4 spot_shadow_slots[2];
	vec4 spot_shadow_biases[2];
	/*
	光照扩展参数（第六百七十二轮；D1 第一步）
	x = PBR 掠射高光能量修正（> 0.5 时镜面乘入射余弦）、yzw 保留；默认 0 时乘子恒 1。
	*/
	vec4 lighting_extra;
	/*
	前向高 8 槽（第六百九十九轮；追加在块尾，旧 8 槽偏移不变）
	布局与低 8 槽一致：位置 ＋ 半径、颜色 ＋ 强度、聚光方向 ＋ 标志、锥参数、逐盏光泽度。
	*/
	vec4 point_pos_radius_hi[8];
	vec4 point_color_intensity_hi[8];
	vec4 point_spot_dir_hi[8];
	vec4 point_spot_cone_hi[8];
	float point_shininess_hi[8];
	/*
	前向 16..31 槽（第七百零一轮；继续追加在块尾）
	布局同上，两组各 160 浮点。
	*/
	vec4 point_pos_radius_hi2[8];
	vec4 point_color_intensity_hi2[8];
	vec4 point_spot_dir_hi2[8];
	vec4 point_spot_cone_hi2[8];
	float point_shininess_hi2[8];
	vec4 point_pos_radius_hi3[8];
	vec4 point_color_intensity_hi3[8];
	vec4 point_spot_dir_hi3[8];
	vec4 point_spot_cone_hi3[8];
	float point_shininess_hi3[8];
	/*
	逐瓦片聚光阴影质量缩放（第七百零八轮；追加在块尾）
	每组 4 分量依次对应瓦片 0..3 / 4..7；默认 1（与全局参数相同，画面逐字节不变）。
	*/
	vec4 spot_shadow_softness_scale[2];
	vec4 spot_shadow_strength_scale[2];
	//槽位 8..31 的瓦片映射（第七百零九轮；-1 = 不投影）
	vec4 spot_shadow_slots_hi[6];
	//环境光照（方向九第 714 轮；rgb = 颜色、w = 强度；0 = 关闭，画面逐字节不变）
	vec4 environment;
	//PBR 扩展（方向九第 718 轮；x = Fresnel-roughness 与 F90 口径统一、yzw 保留；默认 0）
	vec4 pbr_extra;
} scene;

/*
前向 32 槽访问器（第六百九十九轮 16 槽；第七百零一轮 32 槽）
index 0..7 读旧数组、8..15 读 hi、16..23 读 hi2、24..31 读 hi3。
*/
vec4 point_pos_radius_at(int index)
{
	if (index < 8)
		return scene.point_pos_radius[index];
	if (index < 16)
		return scene.point_pos_radius_hi[index - 8];
	if (index < 24)
		return scene.point_pos_radius_hi2[index - 16];
	return scene.point_pos_radius_hi3[index - 24];
}
vec4 point_color_intensity_at(int index)
{
	if (index < 8)
		return scene.point_color_intensity[index];
	if (index < 16)
		return scene.point_color_intensity_hi[index - 8];
	if (index < 24)
		return scene.point_color_intensity_hi2[index - 16];
	return scene.point_color_intensity_hi3[index - 24];
}
vec4 point_spot_dir_at(int index)
{
	if (index < 8)
		return scene.point_spot_dir[index];
	if (index < 16)
		return scene.point_spot_dir_hi[index - 8];
	if (index < 24)
		return scene.point_spot_dir_hi2[index - 16];
	return scene.point_spot_dir_hi3[index - 24];
}
vec4 point_spot_cone_at(int index)
{
	if (index < 8)
		return scene.point_spot_cone[index];
	if (index < 16)
		return scene.point_spot_cone_hi[index - 8];
	if (index < 24)
		return scene.point_spot_cone_hi2[index - 16];
	return scene.point_spot_cone_hi3[index - 24];
}
float point_shininess_at(int index)
{
	if (index < 8)
		return scene.point_shininess[index];
	if (index < 16)
		return scene.point_shininess_hi[index - 8];
	if (index < 24)
		return scene.point_shininess_hi2[index - 16];
	return scene.point_shininess_hi3[index - 24];
}

//点光源数量上限（旧前向路径；与门面 Impl::POINT_LIGHT_UBO_MAX 一致；第七百零一轮 16 → 32）
const int POINT_LIGHT_MAX = 32;

/*
光源记录（与 Host 侧 Light_Record 逐字段一致，std430 共 80 字节）
pos_radius = 位置 ＋ 半径；color_intensity = 颜色 ＋ 强度；direction = 光传播方向（点光为 0）；
cone = 内锥余弦 / 外锥余弦 / 衰减模式 / 逐盏高光缩放；尾部为光泽度、类型、有效标志与对齐保留。
*/
struct Light_Record
{
	vec4 pos_radius;
	vec4 color_intensity;
	vec4 direction;
	vec4 cone;
	float shininess;
	uint type;
	uint flags;
	float reserved;
};

/*
簇数据存储缓冲（描述符集 0 binding 1..4；第二百八十八轮接入、第二百八十九轮读取）
binding 1 = 光源记录数组（1024 条）、2 = 逐簇计数、3 = 逐簇排他偏移、4 = 簇光源列表；
Cluster 关闭时四条绑定指向占位缓冲，且下方分支不生效，故不会读取其中内容。
*/
layout(set = 0, binding = 1) readonly buffer Light_Records { Light_Record light_records[]; };
layout(set = 0, binding = 2) readonly buffer Cluster_Counts { uint cluster_counts[]; };
layout(set = 0, binding = 3) readonly buffer Cluster_Offsets { uint cluster_offsets[]; };
layout(set = 0, binding = 4) readonly buffer Cluster_List { uint cluster_list[]; };

/*
逐物体 PBR 粗糙度缩放（第六十八轮；第一百七十一轮改为按变换后 UV 采样）
覆盖标志有效时取实例属性（钳到合法域），否则沿用场景 UBO 的全局 pbr_params.y。
*/
float material_roughness_scale(vec2 uv)
{
	float base = in_material.z > 0.5 ? clamp(in_material.x, 0.05, 4.0) : max(scene.pbr_params.y, 0.05);
	/*
	粗糙度贴图（第一百二十四轮）
	取红通道作乘子；默认绑定为内置白纹理（采样值 1），故无贴图路径与引入前逐字节一致。
	*/
	return base * texture(roughness_map, uv).r;
}

/*
逐物体金属度（第六十八轮；第一百六十九轮乘金属度贴图；第一百七十一轮按变换后 UV 采样）
覆盖标志有效时取实例属性（钳到 0..1），否则沿用场景 UBO 的全局 pbr_params.z；
再乘金属度贴图红通道（默认绑定白纹理 → ×1），结果钳回 0..1。
*/
float material_metallic(vec2 uv)
{
	float base = in_material.z > 0.5 ? clamp(in_material.y, 0.0, 1.0) : clamp(scene.pbr_params.z, 0.0, 1.0);
	return clamp(base * texture(metallic_map, uv).r, 0.0, 1.0);
}

/*
二次元风格化：光照色阶量化（第一百七十四轮）
steps <= 0.5（关闭）时原样返回；否则把照度量化到 steps 档（四舍五入到最近档），
形成硬边的明暗分界（卡通 / 二次元常用的二值、三值阴影）。
*/
float toon_quantize(float ndotl, float steps)
{
	if (steps <= 0.5)
		return ndotl;
	return floor(clamp(ndotl, 0.0, 1.0) * steps + 0.5) / steps;
}

/*
PBR-lite 粗糙度换算（第六十四轮；第六十八轮增加逐物体缩放入参）
由 Blinn-Phong 光泽度换算 GGX 粗糙度：roughness = sqrt(2 / (shininess + 2))，
再乘缩放入参并钳到 [0.03, 1]；第 721 轮起可选修正映射（见函数内）。
*/
float pbr_roughness(float shininess, float scale)
{
	/*
	绝对口径（第七百轮；D1 收尾）
	lighting_extra.y > 0.5 时材质 / 全局粗糙度直接作为 GGX 粗糙度，不再由光泽度换算；
	默认 0 时沿用旧换算，画面逐字节不变。
	*/
	if (scene.lighting_extra.y > 0.5)
		return clamp(scale, 0.03, 1.0);
	float roughness = sqrt(2.0 / (max(shininess, 1.0) + 2.0));
	//光泽度修正映射（第 721 轮；pbr_extra.w > 0.5）：alpha = sqrt(2/(n+2)) → roughness = alpha^0.5
	if (scene.pbr_extra.w > 0.5)
		roughness = pow(2.0 / (max(shininess, 1.0) + 2.0), 0.25);
	return clamp(roughness * max(scale, 0.05), 0.03, 1.0);
}

/*
PBR-lite 菲涅尔基色（第六十五轮；第六十八轮金属度改为入参）
金属度（0..1）把 F0 从介电 0.04 插值到反照率色；金属表面镜面带基色。
*/
vec3 pbr_f0(vec3 albedo, float metallic)
{
	return mix(vec3(0.04), albedo, clamp(metallic, 0.0, 1.0));
}

/*
PBR 菲涅尔项（方向九第 718 轮：Fresnel-roughness 与 F90 口径统一）
scene.pbr_extra.x > 0.5 时 F90 取 max(1 − roughness, F0)：粗糙表面掠射反射上限随粗糙度下降，
介电与金属共用同一口径；关闭时 F90 恒 1，与引入前逐字节一致。
*/
vec3 pbr_fresnel(vec3 f0, float cos_theta, float roughness)
{
	vec3 f90 = vec3(1.0);
	if (scene.pbr_extra.x > 0.5)
		f90 = max(vec3(1.0 - clamp(roughness, 0.0, 1.0)), f0);
	return f0 + (f90 - f0) * pow(1.0 - clamp(cos_theta, 0.0, 1.0), 5.0);
}

/*
环境镜面 split-sum 解析近似（方向九第 719 轮；Karis 移动端拟合）
由粗糙度与 N·V 直接给出 DFG 积分的 A / B 系数，替代单个方向的菲涅尔采样；
仅在 scene.pbr_extra.y > 0.5 时使用，默认关闭时走原菲涅尔路径（逐字节不变）。
*/
vec2 env_brdf_approx(float roughness, float ndotv)
{
	const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
	const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
	vec4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28 * ndotv)) * r.x + r.y;
	return vec2(-1.04, 1.04) * a004 + r.zw;
}

/*
环境漫反射辐照度方向平均（方向九第 720 轮）
天空盒开启且 scene.pbr_extra.z > 0.5 时，取法线及其切平面上四个方向的立方图均值，
近似半球辐照度；关闭时退回按法线单点采样（第 715 轮口径，逐字节不变）。
天空盒未开启时调用方不使用本函数结果。
*/
vec3 env_diffuse_sample(vec3 normal)
{
	if (scene.pbr_extra.z <= 0.5)
		return texture(sky_map, normal).rgb;
	//切平面基（避开与法线接近平行的参考轴）
	vec3 reference = abs(normal.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
	vec3 tangent = normalize(cross(reference, normal));
	vec3 bitangent = cross(normal, tangent);
	vec3 total = texture(sky_map, normal).rgb;
	total += texture(sky_map, normalize(normal + tangent)).rgb;
	total += texture(sky_map, normalize(normal - tangent)).rgb;
	total += texture(sky_map, normalize(normal + bitangent)).rgb;
	total += texture(sky_map, normalize(normal - bitangent)).rgb;
	return total / 5.0;
}

/*
PBR 镜面能量修正（第六百七十二轮；D1 第一步）
开启（lighting_extra.x > 0.5）时乘入射余弦 NoL，使镜面等价于 D·G·F/(4·NoV)，修正掠射角过亮；
关闭时乘子为 1，既有 PBR 画面逐字节不变。
*/
vec3 pbr_specular_energy(vec3 specular, float ndotl)
{
	return scene.lighting_extra.x > 0.5 ? specular * ndotl : specular;
}

/*
GGX 镜面反射（第六十四轮；第六十五轮改为 RGB 菲涅尔）
分布项 GGX（α = roughness²）、几何项 Smith-Schlick、菲涅尔项 Schlick（F0 由金属度给出）；
返回已含 1 / (4·N·L·N·V) 的镜面值（可大于 1，由上层强度钳制）。
*/
vec3 ggx_specular(vec3 normal, vec3 to_view, vec3 to_light, float roughness, vec3 f0)
{
	/*
	镜面抗锯齿（方向九第 717 轮；lighting_extra.w > 0.5 时启用）
	按屏幕空间法线导数加宽粗糙度，抑制远处 / 高频法线的高光闪烁；
	默认关闭时不做任何修改，画面逐字节不变。
	*/
	if (scene.lighting_extra.w > 0.5)
	{
		vec3 normal_dx = dFdx(normal);
		vec3 normal_dy = dFdy(normal);
		float variance = dot(normal_dx, normal_dx) + dot(normal_dy, normal_dy);
		roughness = clamp(sqrt(roughness * roughness + min(2.0 * variance, 0.18)), 0.03, 1.0);
	}
	float ndotl = max(dot(normal, to_light), 0.0);
	float ndotv = max(dot(normal, to_view), 0.0);
	//任一侧背光则无镜面
	if (ndotl <= 0.0 || ndotv <= 0.0)
		return vec3(0.0);
	vec3 halfway = normalize(to_light + to_view);
	float ndoth = max(dot(normal, halfway), 0.0);
	float vdoth = max(dot(to_view, halfway), 0.0);
	float alpha = roughness * roughness;
	float alpha2 = alpha * alpha;
	float denominator = ndoth * ndoth * (alpha2 - 1.0) + 1.0;
	//法线分布（GGX）
	float distribution = alpha2 / (3.14159265 * denominator * denominator);
	//几何遮蔽（Smith-Schlick 近似）
	float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
	float geometry = (ndotl / (ndotl * (1.0 - k) + k)) * (ndotv / (ndotv * (1.0 - k) + k));
	//菲涅尔（Schlick；F0 由金属度插值；第 718 轮起支持 Fresnel-roughness 口径）
	vec3 fresnel = pbr_fresnel(f0, vdoth, roughness);
	vec3 result = fresnel * (distribution * geometry / (4.0 * ndotl * ndotv));
	/*
	多重散射能量补偿（方向九第 716 轮；lighting_extra.z > 0.5 时启用）
	单次散射 GGX 在高粗糙金属上偏暗；按 F0 亮度与粗糙度补回多次弹射能量。
	默认关闭时乘子恒 1，画面与引入前逐字节一致。
	*/
	if (scene.lighting_extra.z > 0.5)
	{
		float f0_luma = dot(f0, vec3(0.2126, 0.7152, 0.0722));
		result *= 1.0 + f0_luma * roughness * 0.5;
	}
	return result;
}

/*
清漆层高光（第二百三十三轮引入；第二百三十四轮抽为函数并扩展到补光 / 点光 / 聚光）
用独立粗糙度的 GGX（F0 固定介电 0.04）叠一层锐利反射；weight 为该光源的衰减 / 锥形 / 逐光系数。
强度为 0（默认）时恒返回零向量，调用方以 max 叠加，故无清漆路径画面逐字节不变。
*/
vec3 clearcoat_specular(vec3 normal, vec3 to_view, vec3 to_light, float weight)
{
	float strength = clamp(push_tint.clearcoat.x, 0.0, 1.0);
	//未启用清漆
	if (strength <= 0.0)
		return vec3(0.0);
	float roughness = clamp(push_tint.clearcoat.y, 0.03, 1.0);
	return ggx_specular(normal, to_view, to_light, roughness, vec3(0.04)) * strength * weight;
}

/*
单个点光 / 聚光贡献（第二百八十九轮抽取；旧前向路径与 Cluster 列表路径共用）
pos_radius / color_intensity / light_direction / cone 与 Host 侧光源记录同口径；
除「聚光判定」由 is_spot 传入外，衰减 / 锥形 / 色阶量化 / 高光 / 清漆的计算顺序与原 8 盏循环逐行一致，
故小规模光源下两条路径结果逐字节相同。
*/
void accumulate_point_light(vec3 normal, vec3 to_view, vec3 albedo, float roughness_cached, float metallic_cached,
	float toon_levels, vec4 pos_radius, vec4 color_intensity, vec4 light_direction, vec4 cone, float shininess_record,
	bool is_spot, inout vec3 radiance, inout vec3 specular)
{
	vec3 offset = pos_radius.xyz - in_world;
	float distance = length(offset);
	//避免零距离除零
	vec3 to_light = distance > 0.000001 ? offset / distance : vec3(0.0, 0.0, 1.0);
	float radius = pos_radius.w;
	float attenuation = 1.0;
	if (radius > 0.000001)
	{
		float normalized = distance / radius;
		//逐光源模式（0 = 跟随全局；全局 0 = 线性、1 = 平方反比，映射到 1 / 2）
		int mode = int(cone.z + 0.5);
		if (mode == 0)
			mode = scene.light_color.w > 0.5 ? 2 : 1;
		attenuation = mode == 2
			? 1.0 / (1.0 + normalized * normalized)
			: clamp(1.0 - normalized, 0.0, 1.0);
	}
	float cone_factor = 1.0;
	/*
	衰减为零时提前退出（第三百零四轮）
	线性衰减（模式 1）在片元超出半径时恰为 0：辐射度加 0、高光 / 清漆 max(…, 0) 均不改变结果；
	平方反比（模式 2）恒大于 0，不会触发本分支，故与未提前退出逐字节等价。
	*/
	if (attenuation <= 0.0)
		return;
	//聚光灯锥形衰减（普通点光的有效标志为 0 → 恒 1）
	if (is_spot)
	{
		float cos_angle = dot(-to_light, light_direction.xyz);
		cone_factor = smoothstep(cone.y, cone.x, cos_angle);
	}
	/*
	锥外提前退出（第二百九十四轮）
	圆锥外 cone_factor 恒为 0：辐射度加 0、高光 / 清漆 max(…, 0) 均不改变结果，
	故直接返回，跳过后续点积与 pow；与不提前退出逐字节等价。
	*/
	if (is_spot && cone_factor <= 0.0)
		return;
	float ndotl = max(dot(normal, to_light), 0.0);
	//色阶量化（第一百七十四轮；关闭时原样返回）
	ndotl = toon_quantize(ndotl, toon_levels);
	radiance += color_intensity.rgb * color_intensity.w * ndotl * attenuation * cone_factor;
	//逐盏光泽度（0 = 跟随全局光泽度）
	float shininess = shininess_record > 0.0 ? shininess_record : max(scene.light_common.z, 1.0);
	//高光：PBR 开启走 GGX，否则沿用 Blinn-Phong（第六十四轮）
	if (scene.pbr_params.x > 0.5)
		specular = max(specular, pbr_specular_energy(ggx_specular(normal, to_view, to_light,
			pbr_roughness(shininess, roughness_cached), pbr_f0(albedo, metallic_cached))
			* attenuation * cone_factor * cone.w * color_intensity.w, ndotl));
	else
	{
		vec3 halfway = normalize(to_light + to_view);
		float spec_base = max(dot(normal, halfway), 0.0);
		specular = max(specular, vec3(pow(spec_base, shininess) * attenuation * cone_factor * cone.w));
	}
	//清漆层（第二百三十四轮：点光 / 聚光同样叠加一层清漆反射）
	specular = max(specular, clearcoat_specular(normal, to_view, to_light,
		attenuation * cone_factor * cone.w * color_intensity.w));
}

/*
阴影深度读取（第六百五十七轮；供 PCSS 阻塞物搜索与比较复用）
在给定纹素取一次打包深度并解包；钳边限制在 texel_origin 起的 size 范围内
（图集开启时瓦片原点非零，避免采到相邻瓦片）。
*/
float shadow_depth_read(sampler2D shadow_tex, ivec2 texel, int size, ivec2 texel_origin)
{
	//越界钳到本瓦片纹素范围
	ivec2 clamped = clamp(texel, texel_origin, texel_origin + ivec2(size - 1));
	vec4 packed = texelFetch(shadow_tex, clamped, 0);
	//解包 32 位深度
	return dot(packed, vec4(1.0, 1.0 / 255.0, 1.0 / 65025.0, 1.0 / 16581375.0));
}

/*
阴影深度比较（第五十三轮；第四百八十四轮改为显式收阴影图）
在阴影图纹素坐标处取一次打包深度并解包，返回 current <= stored ? 1 : 0。
*/
float shadow_compare(sampler2D shadow_tex, ivec2 texel, float current, int size, ivec2 texel_origin)
{
	//片元比阴影图记录更近则受光，否则遮蔽
	float stored = shadow_depth_read(shadow_tex, texel, size, texel_origin);
	return current <= stored ? 1.0 : 0.0;
}

/*
PCSS 采样盘（第六百五十七轮；16 点 Vogel 螺旋，单位圆内均布）
阻塞物搜索与可变半径 PCF 共用同一盘，仅半径不同，避免两套分布带来的噪声差异。
*/
const vec2 PCSS_DISK[16] = vec2[](
	vec2(0.177, 0.000), vec2(-0.226, 0.207), vec2(0.035, -0.394), vec2(0.285, 0.371),
	vec2(-0.522, -0.092), vec2(0.495, -0.315), vec2(-0.165, 0.616), vec2(-0.316, -0.608),
	vec2(0.685, 0.250), vec2(-0.712, 0.294), vec2(0.343, -0.734), vec2(0.254, 0.809),
	vec2(-0.765, -0.443), vec2(0.897, -0.197), vec2(-0.548, 0.779), vec2(-0.126, -0.976)
);

/*
方向光阴影系数（第五十二轮；第五十三轮增软度与强度；第五十九轮加法线偏移）
shadow_params：x = 启用（> 0.5 生效）、y = 深度偏置、z = 阴影图边长（像素）、w = 软度（0 = 单点硬边、≥0.5 = 3×3 平均）。
返回 0 = 完全遮蔽、1 = 完全受光；阴影图范围外或 w ≤ 0 时视为受光；
最终按 scene.shadow_strength 在 1 与遮蔽系数之间插值（0 = 阴影无效、1 = 全遮蔽）。
*/
//按级序取本片光源空间矩阵（第一百四十六轮）
mat4 cascade_matrix_of(int index)
{
	if (index == 1)
		return scene.light_view_proj_far;
	if (index >= 2)
		return scene.light_view_proj_far2;
	return scene.light_view_proj;
}

//按级序取本片深度跨度（第一百四十六轮）
float cascade_span_of(int index)
{
	if (index == 1)
		return scene.cascade_params.w;
	if (index >= 2)
		return scene.cascade_params2.y;
	return scene.cascade_params.z;
}

/*
单级阴影因子（未乘阴影强度；count 为级数、index 为级序）
矩阵、深度跨度与阴影图显式传入（第四百八十四轮）：方向光取级联矩阵 / 跨度 / binding 1，
聚光取 spot_view_proj_tiles[i] / spot_shadow_spans[i] / binding 9 的数组元素 i。
texel_origin 为图集内的瓦片像素原点（第六百七十轮；独立阴影图恒为 (0,0)）。
*/
float shadow_sample_core(mat4 light_matrix, float depth_span, sampler2D shadow_tex, int count, int index,
	vec3 world_pos, vec3 normal, float bias, float softness, ivec2 texel_origin)
{
	/*
	法线偏移（第五十九轮）
	沿片元法线推出一小段后再取阴影深度，抵消深度量化与掠射角造成的自阴影（shadow acne）；
	偏移量 = 深度偏置 × 光源深度跨度（单级恒为 59；多级取本片跨度；聚光取锥远平面），
	使世界量级与 NDC 偏置量一致；法线为零向量（未提供法线）时不偏移。
	*/
	vec3 sample_pos = world_pos;
	if (length(normal) > 0.000001)
		sample_pos += normalize(normal) * (bias * depth_span);
	//光源空间裁剪坐标
	vec4 light_clip = light_matrix * vec4(sample_pos, 1.0);
	//位于光源背后
	if (light_clip.w <= 0.000001)
		return 1.0;
	vec3 ndc = light_clip.xyz / light_clip.w;
	vec2 uv = ndc.xy * 0.5 + 0.5;
	//阴影图外一律受光（避免边界拉伸）
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
		return 1.0;
	//多级时把级联局部坐标映射到对应小片（X 按片序偏移，Y 恒收缩到顶格）
	if (count == 2)
	{
		uv.x = uv.x * 0.5 + (index == 1 ? 0.5 : 0.0);
		uv.y = uv.y * 0.5;
	}
	else if (count >= 3)
	{
		float slice = 1.0 / 3.0;
		uv.x = uv.x * slice + float(index) * slice;
		uv.y = uv.y * slice;
	}
	int size = int(scene.shadow_params.z + 0.5);
	//纹素坐标（最近点，避免插值得到无意义的打包字节；叠加图集瓦片原点）
	ivec2 texel = texel_origin + ivec2(clamp(uv * float(size), vec2(0.0), vec2(float(size - 1))));
	//当前片元在光源空间的深度（Vulkan 深度范围 [0,1]）减去偏置
	float current = ndc.z - bias;
	/*
	PCSS 软阴影（第六百五十七轮）
	开关与参数复用 toon_specular_params 的空闲分量：y = 启用（> 0.5）、z = 阻塞物搜索尺度（纹素）、
	w = PCF 半径上限（纹素）；半影宽度 = clamp((片元深度 − 平均遮挡深度) × 搜索尺度, 1, 上限)。
	关闭（y ≤ 0.5）时完全走下方旧口径（硬边 / 3×3 软度），画面逐字节不变。
	*/
	if (scene.toon_specular_params.y > 0.5)
	{
		float search = max(scene.toon_specular_params.z, 1.0);
		float max_radius = max(scene.toon_specular_params.w, 1.0);
		//阻塞物搜索：16 点盘，统计比片元更近的平均深度
		float blocker_sum = 0.0;
		float blockers = 0.0;
		for (int tap = 0; tap < 16; ++tap)
		{
			float stored = shadow_depth_read(shadow_tex, texel + ivec2(PCSS_DISK[tap] * search), size, texel_origin);
			//若该样本比片元更近则计入遮挡
			if (stored < current)
			{
				blocker_sum += stored;
				blockers += 1.0;
			}
		}
		//无遮挡物 → 完全受光
		if (blockers <= 0.0)
			return 1.0;
		float blocker_depth = blocker_sum / blockers;
		//半影宽度（纹素）：深度差越大越软，钳到 [1, 上限]
		float penumbra = clamp((current - blocker_depth) * search, 1.0, max_radius);
		//可变半径 PCF：同一盘按半影半径采样
		float lit = 0.0;
		for (int tap = 0; tap < 16; ++tap)
			lit += shadow_compare(shadow_tex, texel + ivec2(PCSS_DISK[tap] * penumbra), current, size, texel_origin);
		return lit / 16.0;
	}
	//遮蔽系数（软度 0 = 单点；>0 按软度映射邻域半径 1..3 → 3×3 / 5×5 / 7×7）
	if (softness <= 0.0)
		return shadow_compare(shadow_tex, texel, current, size, texel_origin);
	//邻域半径（0.25 以下 → 1、0.75 以下 → 2、其余 → 3）
	float soft = clamp(softness, 0.0, 1.0);
	int radius = soft <= 0.25 ? 1 : (soft <= 0.75 ? 2 : 3);
	float sum = 0.0;
	int samples = 0;
	for (int offset_y = -radius; offset_y <= radius; ++offset_y)
		for (int offset_x = -radius; offset_x <= radius; ++offset_x)
		{
			sum += shadow_compare(shadow_tex, texel + ivec2(offset_x, offset_y), current, size, texel_origin);
			++samples;
		}
	return sum / float(samples);
}

//单级阴影因子（方向光口径：偏置 / 软度取 shadow_params）
float shadow_sample(int index, int count, vec3 world_pos, vec3 normal)
{
	return shadow_sample_core(cascade_matrix_of(index), cascade_span_of(index), shadow_map, count, index,
		world_pos, normal, scene.shadow_params.y, scene.shadow_params.w, ivec2(0));
}

/*
聚光灯阴影槽位映射读取（第七百零九轮；槽位 0..31）
0..7 读 spot_shadow_slots（每组 4 槽），8..31 读追加在块尾的 spot_shadow_slots_hi；-1 = 不投影。
*/
float spot_slot_tile(int slot)
{
	if (slot < 8)
	{
		vec4 group = scene.spot_shadow_slots[slot / 4];
		if (slot % 4 == 0)
			return group.x;
		if (slot % 4 == 1)
			return group.y;
		if (slot % 4 == 2)
			return group.z;
		return group.w;
	}
	vec4 group = scene.spot_shadow_slots_hi[(slot - 8) / 4];
	int offset = (slot - 8) % 4;
	if (offset == 0)
		return group.x;
	if (offset == 1)
		return group.y;
	if (offset == 2)
		return group.z;
	return group.w;
}

/*
聚光灯阴影系数（第四百七十四 / 476 / 479 / 484 轮；第五百零七轮扩为多盏；第七百零九轮扩到 32 槽）
与方向光阴影完全独立：开关由 spot_shadow_params.w 表达（> 0.5 = 本帧存在有效聚光阴影）；
槽位映射把「前向槽位」映射到「瓦片号」（-1 = 该灯不投影），
矩阵 / 跨度 / 阴影图分别取对应瓦片的 spot_view_proj_tiles / spot_shadow_spans / binding 9 数组元素。
*/
float spot_shadow_factor(vec3 world_pos, vec3 normal, int light_index)
{
	//阴影总开关关闭，或聚光阴影未开启（w = 0）
	if (scene.shadow_params.x < 0.5 || scene.spot_shadow_params.w < 0.5)
		return 1.0;
	//槽位越界（前向最多 32 槽）
	if (light_index < 0 || light_index >= 32)
		return 1.0;
	//该槽位的瓦片号（-1 = 不投影）
	float tile_value = spot_slot_tile(light_index);
	//未映射到任何瓦片：该灯不受聚光阴影影响
	if (tile_value < -0.5)
		return 1.0;
	//瓦片号与质量参数（偏置改为逐瓦片有效值，第五百二十一轮）
	int tile = int(tile_value + 0.5);
	/*
	逐瓦片质量缩放（第七百零八轮）
	按瓦片从 2 组 vec4 中取分量；默认 1，故有效软度 / 强度与全局参数相同（逐字节不变）。
	*/
	vec4 soft_group = scene.spot_shadow_softness_scale[tile / 4];
	vec4 strength_group = scene.spot_shadow_strength_scale[tile / 4];
	float soft_scale = soft_group.x;
	float strength_scale = strength_group.x;
	if (tile % 4 == 1)
	{
		soft_scale = soft_group.y;
		strength_scale = strength_group.y;
	}
	else if (tile % 4 == 2)
	{
		soft_scale = soft_group.z;
		strength_scale = strength_group.z;
	}
	else if (tile % 4 == 3)
	{
		soft_scale = soft_group.w;
		strength_scale = strength_group.w;
	}
	float softness = scene.spot_shadow_params.y * soft_scale;
	float strength = clamp(scene.spot_shadow_params.z * strength_scale, 0.0, 1.0);
	/*
	图集纹素原点（第六百七十轮；C1）
	图集开启（spot_shadow_params.x > 0.5）时瓦片按 4 列 2 行排布，原点 = (列·S, 行·S)；
	关闭时为 (0, 0)，与引入前逐字节一致。
	*/
	int tile_size = int(scene.shadow_params.z + 0.5);
	ivec2 tile_origin = scene.spot_shadow_params.x > 0.5
		? ivec2((tile % 4) * tile_size, (tile / 4) * tile_size) : ivec2(0);
	/*
	按瓦片取矩阵 / 跨度 / 阴影图
	显式八分支：采样器数组不做动态索引，避免驱动对「动态一致索引」的限制。
	*/
	if (tile == 0)
		return mix(1.0, shadow_sample_core(scene.spot_view_proj_tiles[0], scene.spot_shadow_spans[0].x,
			spot_shadow_map[0], 1, 0, world_pos, normal, scene.spot_shadow_biases[0].x, softness, tile_origin), strength);
	if (tile == 1)
		return mix(1.0, shadow_sample_core(scene.spot_view_proj_tiles[1], scene.spot_shadow_spans[0].y,
			spot_shadow_map[1], 1, 0, world_pos, normal, scene.spot_shadow_biases[0].y, softness, tile_origin), strength);
	if (tile == 2)
		return mix(1.0, shadow_sample_core(scene.spot_view_proj_tiles[2], scene.spot_shadow_spans[0].z,
			spot_shadow_map[2], 1, 0, world_pos, normal, scene.spot_shadow_biases[0].z, softness, tile_origin), strength);
	if (tile == 3)
		return mix(1.0, shadow_sample_core(scene.spot_view_proj_tiles[3], scene.spot_shadow_spans[0].w,
			spot_shadow_map[3], 1, 0, world_pos, normal, scene.spot_shadow_biases[0].w, softness, tile_origin), strength);
	if (tile == 4)
		return mix(1.0, shadow_sample_core(scene.spot_view_proj_tiles[4], scene.spot_shadow_spans[1].x,
			spot_shadow_map[4], 1, 0, world_pos, normal, scene.spot_shadow_biases[1].x, softness, tile_origin), strength);
	if (tile == 5)
		return mix(1.0, shadow_sample_core(scene.spot_view_proj_tiles[5], scene.spot_shadow_spans[1].y,
			spot_shadow_map[5], 1, 0, world_pos, normal, scene.spot_shadow_biases[1].y, softness, tile_origin), strength);
	if (tile == 6)
		return mix(1.0, shadow_sample_core(scene.spot_view_proj_tiles[6], scene.spot_shadow_spans[1].z,
			spot_shadow_map[6], 1, 0, world_pos, normal, scene.spot_shadow_biases[1].z, softness, tile_origin), strength);
	if (tile == 7)
		return mix(1.0, shadow_sample_core(scene.spot_view_proj_tiles[7], scene.spot_shadow_spans[1].w,
			spot_shadow_map[7], 1, 0, world_pos, normal, scene.spot_shadow_biases[1].w, softness, tile_origin), strength);
	//越界瓦片按无阴影处理
	return 1.0;
}

float shadow_factor(vec3 world_pos, vec3 normal)
{
	//未启用阴影
	if (scene.shadow_params.x < 0.5)
		return 1.0;
	//方向光阴影未启用：方向光不受阴影影响（第四百八十四轮：与聚光阴影独立）
	if (scene.cascade_params.x < 0.5)
		return 1.0;
	/*
	级联选择（第一百二十一轮；第一百四十六轮扩为三级）
	按片元与相机的世界距离选择级联：阈值依次为 cascade_params.y 与 cascade_params2.x；
	各级使用各自的光源空间矩阵，阴影图则沿 X 切成同级数的方形小片。
	*/
	int cascade_count = int(scene.cascade_params.x + 0.5);
	float view_distance = distance(world_pos, scene.camera_pos.xyz);
	int cascade = 0;
	if (cascade_count >= 2 && view_distance > scene.cascade_params.y)
		cascade = 1;
	if (cascade_count >= 3 && view_distance > scene.cascade_params2.x)
		cascade = 2;
	float factor = shadow_sample(cascade, cascade_count, world_pos, normal);
	/*
	级联接缝淡化（第一百四十八轮；cascade_params2.z = 淡化带宽比例，0 = 硬切换）
	在切换阈值两侧各取「阈值 × 比例」的带宽，对相邻两片因子做平滑混合，
	消除分辨率突变造成的可见接缝；带宽为 0 时完全跳过，画面逐字节不变。
	*/
	float blend = clamp(scene.cascade_params2.z, 0.0, 0.5);
	if (blend > 0.0 && cascade_count >= 2)
	{
		//第 1 个阈值（近区 / 第 2 片）
		float split_near = scene.cascade_params.y;
		float band_near = max(split_near * blend, 0.0001);
		if (abs(view_distance - split_near) < band_near)
		{
			float weight = smoothstep(-band_near, band_near, view_distance - split_near);
			factor = mix(shadow_sample(0, cascade_count, world_pos, normal),
				shadow_sample(1, cascade_count, world_pos, normal), weight);
		}
		//第 2 个阈值（第 2 片 / 第 3 片；仅三级）
		if (cascade_count >= 3)
		{
			float split_far = scene.cascade_params2.x;
			float band_far = max(split_far * blend, 0.0001);
			if (abs(view_distance - split_far) < band_far)
			{
				float weight = smoothstep(-band_far, band_far, view_distance - split_far);
				factor = mix(shadow_sample(1, cascade_count, world_pos, normal),
					shadow_sample(2, cascade_count, world_pos, normal), weight);
			}
		}
	}
	//按阴影强度在「无阴影」与「全遮蔽」之间插值
	return mix(1.0, factor, clamp(scene.shadow_strength, 0.0, 1.0));
}

void main()
{
	/*
	材质贴图 UV 变换（第一百七十一轮；第一百八十三轮加旋转）
	全部材质贴图共用同一变换：先缩放、再绕 (0,0) 旋转、最后偏移；
	默认 scale (1,1)、rotation 0、offset (0,0) 时 uv' 与原 UV 完全一致，画面逐字节不变。
	*/
	vec2 uv_scaled = in_uv * push_tint.material_uv.xy;
	//UV 旋转（角度 0 时 cos=1 / sin=0，结果与未旋转逐字节一致）
	if (push_tint.material_extras.w != 0.0)
	{
		float rotation_cos = cos(push_tint.material_extras.w);
		float rotation_sin = sin(push_tint.material_extras.w);
		uv_scaled = vec2(uv_scaled.x * rotation_cos - uv_scaled.y * rotation_sin,
			uv_scaled.x * rotation_sin + uv_scaled.y * rotation_cos);
	}
	vec2 uv_material = uv_scaled + push_tint.material_uv.zw;
	//采样反照率并乘逐物体色调（默认白色 → 不变）
	vec4 albedo_color = texture(albedo, uv_material) * push_tint.tint;
	/*
	Alpha 裁剪（第一百八十三轮）
	阈值 > 0 且反照率 alpha 低于阈值时丢弃该片元（镂空 / 树叶类贴图）；默认 0 不做任何裁剪。
	*/
	if (push_tint.material_extras.z > 0.0 && albedo_color.a < push_tint.material_extras.z)
		discard;
	/*
	环境光遮蔽（第一百六十九轮）
	取红通道作环境光乘子；默认绑定白纹理时恒为 1，故未配置贴图路径逐字节不变。
	仅作用于环境光基底与天空 IBL 漫反射，不衰减方向光 / 点光源的直接照度。
	*/
	float occlusion = texture(ao_map, uv_material).r;
	/*
	PBR 材质参数预采样（第二百三十六轮）
	粗糙度 / 金属度两个采样结果在同一片元内与光源无关，原先在方向光、补光、每盏点光 / 聚光分支各采样一次
	（8 盏时最多 18 次纹理采样）；此处按 PBR 开启与否各算一次，后续分支复用，画面逐字节一致。
	PBR 关闭时不做采样（保持原有非 PBR 路径的采样量），缓存值仅为占位。
	*/
	float material_roughness_cached = 1.0;
	float material_metallic_cached = 0.0;
	if (scene.pbr_params.x > 0.5)
	{
		material_roughness_cached = material_roughness_scale(uv_material);
		material_metallic_cached = material_metallic(uv_material);
	}
	//二次元风格化参数（第一百七十四轮；均复用 UBO 保留分量）
	float toon_levels = scene.pbr_params.w;
	float toon_rim = scene.sky_params.w;
	/*
	逐物体卡通参数覆盖（第四百零八轮）
	覆盖标志（in_toon.w > 0.5）为真时，色阶数 / 边缘光 / 卡通高光阈值改用逐物体值；
	默认覆盖标志为 0 时三项与全局值逐字节一致，故未设置材质的绘制画面不变。
	*/
	float toon_specular = scene.toon_specular_params.x;
	if (in_toon.w > 0.5)
	{
		toon_levels = in_toon.x;
		toon_rim = in_toon.y;
		toon_specular = in_toon.z;
	}
	/*
	二次元暗部色调（第一百七十八轮；三个保留分量拼 RGB）
	266 / 267（camera_range.zw）= 红 / 绿，291（cascade_params2.w）= 蓝；
	默认 (1,1,1) 时下方乘子恒为 1，画面与引入前逐字节一致。
	*/
	vec3 toon_shadow_tint = vec3(scene.camera_range.z, scene.camera_range.w, scene.cascade_params2.w);
	//逐物体暗部色调覆盖（第四百零九轮；覆盖标志为真时改用实例色调，默认 (1,1,1) 即不色偏）
	if (in_toon.w > 0.5)
		toon_shadow_tint = in_toon_tint.xyz;
	//主方向光量化后的受光系数（未提供法线 / 方向光关闭时保持 1，即不参与暗部判定）
	float toon_primary_shade = 1.0;
	/*
	多光源累加（第二十九轮方向光 / 第三十一轮点光源与高光 / 第三十二轮多光源）
	未提供法线（长度近零）时按「朝向相机」处理，等价于满照（辐射度 = 1），
	保证既有只写位置 / 纹理坐标的网格画面不变。
	*/
	vec3 radiance = vec3(occlusion);
	vec3 specular = vec3(0.0);
	/*
	PBR 漫反射能量分摊（第六百七十四轮；D1 第二步）
	默认 1（不做分摊）；能量模式且有法线时按 (1 − 菲涅尔) 衰减，
	使掠射角处漫反射让位给镜面，最终权重在合成处乘到漫反射项。
	*/
	float pbr_diffuse_energy = 1.0;
	//天空盒镜面 IBL 累加（第一百二十五轮；不受全局高光强度缩放）
	vec3 sky_specular = vec3(0.0);
	//若提供了法线
	if (length(in_normal) > 0.000001)
	{
		vec3 normal = normalize(in_normal);
		/*
		法线贴图（第一百二十四轮）
		强度为 0（默认）时整段跳过，几何法线原样使用，画面逐字节不变；
		启用时按屏幕空间导数构造切线基（网格无切线属性），把切线空间法线变换到世界空间后按强度混合。
		*/
		if (push_tint.material_extras.x > 0.0)
		{
			//切线空间法线（[0,1] 编码 → [-1,1]）
			vec3 tangent_normal = texture(normal_map, uv_material).xyz * 2.0 - 1.0;
			//屏幕空间导数构造切线 / 副切线
			vec3 dp1 = dFdx(in_world);
			vec3 dp2 = dFdy(in_world);
			vec2 duv1 = dFdx(in_uv);
			vec2 duv2 = dFdy(in_uv);
			vec3 dp2perp = cross(dp2, normal);
			vec3 dp1perp = cross(normal, dp1);
			vec3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
			vec3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
			//基向量归一（退化时保持几何法线）
			float maximum = max(dot(tangent, tangent), dot(bitangent, bitangent));
			if (maximum > 0.0)
			{
				float inverse_length = inversesqrt(maximum);
				tangent *= inverse_length;
				bitangent *= inverse_length;
				vec3 perturbed = normalize(tangent * tangent_normal.x + bitangent * tangent_normal.y
					+ normal * tangent_normal.z);
				normal = normalize(mix(normal, perturbed, clamp(push_tint.material_extras.x, 0.0, 1.0)));
			}
		}
		//视线方向（相机 → 片元 的反向，即片元指向相机）
		vec3 to_view = normalize(scene.camera_pos.xyz - in_world);
		/*
		菲涅尔能量分摊（第六百七十四轮；仅能量模式 ＋ PBR 开启）
		视角菲涅尔取 RGB 最大分量；关闭时 pbr_diffuse_energy 恒 1（逐字节不变）。
		*/
		if (scene.lighting_extra.x > 0.5 && scene.pbr_params.x > 0.5)
		{
			vec3 f0 = pbr_f0(albedo_color.rgb, material_metallic_cached);
			vec3 fresnel = f0 + (vec3(1.0) - f0)
				* pow(1.0 - clamp(dot(normal, to_view), 0.0, 1.0), 5.0);
			pbr_diffuse_energy = clamp(1.0 - max(max(fresnel.r, fresnel.g), fresnel.b), 0.0, 1.0);
		}
		/*
		环境光作为基底辐射度（颜色 × 强度；第四十轮引入颜色；第四十七轮支持半球混合）
		半球混合：法线朝上取 ambient_color、朝下取 ambient_ground.rgb，
		按「法线 y 分量映射到 0..1」线性插值后再按 ambient_ground.w 与单一颜色混合；
		w = 0 时退化为单一 ambient_color（既有画面逐字节不变）。
		*/
		vec3 ambient_rgb = scene.ambient_color.rgb;
		//若启用半球环境光
		if (scene.ambient_ground.w > 0.0)
		{
			//法线 y ∈ [-1, 1] 映射到 [0, 1]
			float up_mix = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
			//地面 → 天顶插值，再与单一颜色按强度混合
			vec3 hemisphere_rgb = mix(scene.ambient_ground.rgb, scene.ambient_color.rgb, up_mix);
			ambient_rgb = mix(scene.ambient_color.rgb, hemisphere_rgb, scene.ambient_ground.w);
		}
		radiance = ambient_rgb * scene.light_common.x * occlusion;
		/*
		天空盒简易 IBL（第一百二十五轮）
		启用时以立方体贴图作环境补光：法线方向采样近似漫反射（加入辐射度），
		反射方向采样近似镜面（独立累加，不受全局高光强度缩放）；
		关闭时整段跳过，画面与引入前逐字节一致。
		*/
		if (scene.sky_params.x > 0.5)
		{
			radiance += texture(sky_map, normal).rgb * scene.sky_params.y * occlusion;
			vec3 reflected = reflect(-to_view, normal);
			sky_specular = texture(sky_map, reflected).rgb * scene.sky_params.z;
		}
		/*
		常量环境光照（方向九第 714 轮；IBL 的常量近似）
		environment.rgb = 环境颜色、.w = 强度；强度 0 时整段跳过，画面与引入前逐字节一致。
		漫反射按反照率 × (1 - 金属度) × 漫反射能量分摊累加；
		镜面按 Schlick 菲涅尔反射率乘环境颜色累加（均匀环境下与粗糙度无关）。
		*/
		if (scene.environment.w > 0.0)
		{
			/*
			环境源（第七百一十五轮）
			天空盒开启时以其立方体贴图作为环境源：法线方向近似漫反射、反射方向近似镜面；
			关闭时退回常量颜色（第 714 轮口径，画面逐字节不变）。
			*/
			vec3 env_diffuse = scene.environment.rgb;
			vec3 env_specular = scene.environment.rgb;
			if (scene.sky_params.x > 0.5)
			{
				env_diffuse *= env_diffuse_sample(normal);
				env_specular *= texture(sky_map, reflect(-to_view, normal)).rgb;
			}
			vec3 env_rgb = env_diffuse * scene.environment.w;
			vec3 env_spec_rgb = env_specular * scene.environment.w;
			radiance += env_rgb * albedo_color.rgb * (1.0 - material_metallic_cached)
				* pbr_diffuse_energy * occlusion;
			vec3 env_f0 = pbr_f0(albedo_color.rgb, material_metallic_cached);
			//环境镜面 split-sum 近似（第 719 轮；pbr_extra.y > 0.5 时启用）
			float env_roughness = pbr_roughness(max(scene.light_common.z, 1.0), material_roughness_cached);
			float env_ndotv = clamp(dot(normal, to_view), 0.0, 1.0);
			vec2 env_brdf = env_brdf_approx(env_roughness, env_ndotv);
			vec3 env_spec = scene.pbr_extra.y > 0.5
				? env_f0 * env_brdf.x + env_brdf.y
				: pbr_fresnel(env_f0, env_ndotv, env_roughness);
			sky_specular += env_spec_rgb * env_spec;
		}
		/*
		方向光（强度为 0 视为关闭）
		高光按 Blinn-Phong：H = normalize(L + V)，取各光源贡献的最大值（不做叠加，避免过曝）
		*/
		if (scene.light_dir.w > 0.0)
		{
			vec3 to_light = -scene.light_dir.xyz;
			float ndotl = max(dot(normal, to_light), 0.0);
			//色阶量化（第一百七十四轮；关闭时原样返回）
			ndotl = toon_quantize(ndotl, toon_levels);
			//记录主方向光受光系数（第一百七十八轮；供暗部色调化使用）
			toon_primary_shade = ndotl;
			//方向光阴影系数（第五十二轮；未启用时恒 1；第五十九轮传入法线做偏移）
			float dir_shadow = shadow_factor(in_world, normal);
			radiance += scene.light_color.rgb * scene.light_dir.w * ndotl * dir_shadow;
			//高光：PBR 开启走 GGX，否则沿用 Blinn-Phong（第六十四轮）
			if (scene.pbr_params.x > 0.5)
				specular = max(specular, pbr_specular_energy(ggx_specular(normal, to_view, to_light,
					pbr_roughness(max(scene.light_common.z, 1.0), material_roughness_cached),
					pbr_f0(albedo_color.rgb, material_metallic_cached))
					* scene.camera_pos.w * dir_shadow * scene.light_dir.w, ndotl));
			else
			{
				vec3 halfway = normalize(to_light + to_view);
				float spec_base = max(dot(normal, halfway), 0.0);
				specular = max(specular, vec3(pow(spec_base, max(scene.light_common.z, 1.0)) * scene.camera_pos.w * dir_shadow));
			}
			/*
			清漆层（第二百三十三轮；第二百三十四轮改走公共函数）
			在主光高光之上叠一层独立粗糙度的 GGX 反射（F0 固定 0.04），模拟漆面清漆的锐利高光；
			clearcoat.x 为 0 时整段跳过，故默认画面逐字节不变。
			*/
			specular = max(specular, clearcoat_specular(normal, to_view, to_light,
				scene.camera_pos.w * dir_shadow * scene.light_dir.w));
		}
		/*
		补光（第四十七轮；第二盏方向光，强度为 0 视为关闭）
		只与方向光做漫反射累加；高光同样取最大值（不与主光叠加，避免过曝）。
		*/
		if (scene.fill_dir_intensity.w > 0.0)
		{
			vec3 fill_to_light = -scene.fill_dir_intensity.xyz;
			float fill_ndotl = max(dot(normal, fill_to_light), 0.0);
			//色阶量化（第一百七十四轮）
			fill_ndotl = toon_quantize(fill_ndotl, toon_levels);
			radiance += scene.fill_color.rgb * scene.fill_dir_intensity.w * fill_ndotl;
			//高光：PBR 开启走 GGX，否则沿用 Blinn-Phong（第六十四轮）
			if (scene.pbr_params.x > 0.5)
				specular = max(specular, pbr_specular_energy(ggx_specular(normal, to_view, fill_to_light,
					pbr_roughness(max(scene.light_common.z, 1.0), material_roughness_cached),
					pbr_f0(albedo_color.rgb, material_metallic_cached))
					* scene.fill_color.w * scene.fill_dir_intensity.w, fill_ndotl));
			else
			{
				vec3 fill_halfway = normalize(fill_to_light + to_view);
				float fill_spec_base = max(dot(normal, fill_halfway), 0.0);
				specular = max(specular, vec3(pow(fill_spec_base, max(scene.light_common.z, 1.0)) * scene.fill_color.w));
			}
			//清漆层（第二百三十四轮：补光同样叠加一层清漆反射）
			specular = max(specular, clearcoat_specular(normal, to_view, fill_to_light,
				scene.fill_color.w * scene.fill_dir_intensity.w));
		}
		/*
		点光源 / 聚光（第二百八十九轮：Cluster 开启时改读逐簇光源列表）
		Cluster 开启（场景 UBO 296 > 0.5）时按片元位置换算所属簇，从清单取光源记录逐盏累加；
		关闭时沿用原 8 盏 UBO 前向循环；两条路径共用同一累加函数，故小规模光源下画面逐字节一致。
		*/
		if (scene.cluster_params.x > 0.5)
		{
			//网格轴数（至少 1，避免空网格）
			int grid_x = max(int(scene.cluster_params.y + 0.5), 1);
			int grid_y = max(int(scene.cluster_params.z + 0.5), 1);
			int grid_z = max(int(scene.cluster_params.w + 0.5), 1);
			//片元裁剪空间坐标（与 CPU 参考的屏幕归一化口径一致）
			vec4 cluster_clip = scene.view_proj * vec4(in_world, 1.0);
			//默认落在第 0 簇（相机背后等退化情形保守取首个簇）
			int cluster_x = 0;
			int cluster_y = 0;
			//相机前方才换算屏幕坐标
			if (cluster_clip.w > 0.000001)
			{
				vec2 cluster_ndc = vec2(cluster_clip.x / cluster_clip.w * 0.5 + 0.5,
					0.5 - cluster_clip.y / cluster_clip.w * 0.5);
				cluster_x = clamp(int(floor(cluster_ndc.x * float(grid_x))), 0, grid_x - 1);
				cluster_y = clamp(int(floor(cluster_ndc.y * float(grid_y))), 0, grid_y - 1);
			}
			//深度轴按「相机到片元距离」线性映射（与 CPU 覆盖口径一致）
			float cluster_span = max(scene.camera_range.y - scene.camera_range.x, 0.000001);
			float cluster_distance = distance(scene.camera_pos.xyz, in_world);
			int cluster_z = clamp(int(floor((cluster_distance - scene.camera_range.x) / cluster_span
				* float(grid_z))), 0, grid_z - 1);
			//线性下标（x 变化最快）
			uint cluster = uint((cluster_z * grid_y + cluster_y) * grid_x + cluster_x);
			uint cluster_begin = cluster_offsets[cluster];
			uint cluster_count = cluster_counts[cluster];
			//逐盏累加（清单按光源记录顺序排列，与旧前向循环同序）
			for (uint item = 0u; item < cluster_count; ++item)
			{
				uint light_index = cluster_list[cluster_begin + item];
				Light_Record light = light_records[light_index];
				//方向光 / 补光由上方既有分支处理
				if (light.type == 0u)
					continue;
				accumulate_point_light(normal, to_view, albedo_color.rgb, material_roughness_cached,
					material_metallic_cached, toon_levels, light.pos_radius, light.color_intensity,
					light.direction, light.cone, light.shininess, light.type == 2u, radiance, specular);
				//聚光灯阴影（第四百七十四 / 479 / 488 轮；仅遮蔽所选序号的聚光灯）
				if (light.type == 2u)
				{
					//Cluster 记录的 reserved 携带前向路径槽位（与 spot_shadow_index 同口径）
					float spot_shadow = spot_shadow_factor(in_world, normal, int(light.reserved));
					radiance *= spot_shadow;
					specular *= spot_shadow;
				}
			}
		}
		else
		{
			/*
			点光源（数量由 light_common.w 给出，线性衰减；超过上限的登记在门面侧即被拒绝）
			*/
			int point_count = int(scene.light_common.w);
			for (int index = 0; index < POINT_LIGHT_MAX; ++index)
			{
				//超出有效数量的槽位不参与
				if (index >= point_count)
					break;
				accumulate_point_light(normal, to_view, albedo_color.rgb, material_roughness_cached,
					material_metallic_cached, toon_levels, point_pos_radius_at(index),
					point_color_intensity_at(index), point_spot_dir_at(index),
					point_spot_cone_at(index), point_shininess_at(index),
					point_spot_dir_at(index).w > 0.5, radiance, specular);
				//聚光灯阴影（第四百七十四 / 479 轮；仅遮蔽所选序号的聚光灯）
				if (point_spot_dir_at(index).w > 0.5)
				{
					float spot_shadow = spot_shadow_factor(in_world, normal, index);
					radiance *= spot_shadow;
					specular *= spot_shadow;
				}
			}
		}
	}
	/*
	最终颜色：反照率 × 累加辐射度 × 漫反射权重 ＋ 镜面（强度由 light_common.y 统一缩放），钳制到 [0,1]
	漫反射权重（第六十五轮）：PBR 开启时按金属度做能量守恒（1 − metallic），关闭时恒 1；
	金属度来源由 material_metallic(uv_material) 决定（第六十八轮：逐物体覆盖或全局）；
	第二百三十六轮改为复用片元开头的预采样结果（`material_metallic_cached`）。
	*/
	float diffuse_weight = scene.pbr_params.x > 0.5
		? clamp(1.0 - material_metallic_cached, 0.0, 1.0) * pbr_diffuse_energy : 1.0;
	vec3 lit = albedo_color.rgb * radiance * diffuse_weight + specular * scene.light_common.y + sky_specular;
	/*
	二次元风格化 · 卡通高光（第一百七十九轮）
	阈值 > 0 时按「三通道最大高光亮度」二值化：低于阈值整块归零、不低于阈值整块保留，
	形成硬边高光块（配合色阶量化的硬边阴影）；默认 0 时整段跳过，画面逐字节不变。
	*/
	if (toon_specular > 0.0)
	{
		float specular_level = max(max(specular.r, specular.g), specular.b);
		//高光阈值化（低于阈值的高光整块抹除）
		if (specular_level < toon_specular)
			specular = vec3(0.0);
		//重新合成以应用阈值化后的高光
		lit = albedo_color.rgb * radiance * diffuse_weight + specular * scene.light_common.y + sky_specular;
	}
	/*
	二次元风格化 · 暗部色调化（第一百七十八轮）
	按主方向光量化后的受光系数把暗部乘上色调：权重 = 1 − smoothstep(0, 0.5, shade)，
	照度越低权重越高（系数 ≥ 0.5 的受光面完全不受影响）；
	色调为 (1,1,1)（默认）时乘子恒为 1，整段等价于不执行，画面逐字节不变。
	*/
	if (any(notEqual(clamp(toon_shadow_tint, 0.0, 1.0), vec3(1.0))))
	{
		float toon_shadow_weight = 1.0 - smoothstep(0.0, 0.5, toon_primary_shade);
		lit *= mix(vec3(1.0), clamp(toon_shadow_tint, 0.0, 1.0), toon_shadow_weight);
	}
	/*
	二次元风格化 · 菲涅尔边缘光（第一百七十四轮）
	强度 > 0 且提供了法线时，按「视线与法线」夹角在轮廓处叠加反照率亮边（三次方收窄）；
	默认 0 时整段跳过，画面与引入前逐字节一致。
	*/
	if (toon_rim > 0.0 && length(in_normal) > 0.000001)
	{
		vec3 rim_view = normalize(scene.camera_pos.xyz - in_world);
		float rim = 1.0 - clamp(dot(normalize(in_normal), rim_view), 0.0, 1.0);
		lit += albedo_color.rgb * (rim * rim * rim) * clamp(toon_rim, 0.0, 1.0) * 2.0;
	}
	/*
	距离雾（第三十八轮；密度 0 = 关闭；第一百四十三轮补指数平方模式）
	线性（fog_range.z ≤ 0.5）：按「相机 → 片元」距离在 [起始, 结束] 间归一化，取 t² × 密度混合雾色；
	指数平方（fog_range.z > 0.5）：因子为 1 − exp(−(密度 × (距离 − 起始))²)，近处趋零、远处平滑饱和；
	雾前的照度与雾后的混合都在线性空间完成，最后统一钳制。
	*/
	/*
	逐物体自发光（第七十轮；第一百六十八轮乘自发光贴图）
	实例材质第 4 分量（w）为自发光强度（默认 0 = 无自发光），叠加在受光结果之上；
	再乘自发光贴图颜色（默认绑定白纹理 → 乘子 1）；
	强度为 0 时表达式恒为 0，默认画面逐字节不变。
	*/
	vec3 emissive_rgb = albedo_color.rgb * clamp(in_material.w, 0.0, 8.0) * texture(emissive_map, uv_material).rgb;
	vec3 final_rgb = lit + emissive_rgb;
	if (scene.fog_color_density.w > 0.0)
	{
		float view_distance = length(scene.camera_pos.xyz - in_world);
		float fog_factor = 0.0;
		//指数平方模式：起雾距离外按密度平方增长
		if (scene.fog_range.z > 0.5)
		{
			float fog_distance = max(view_distance - scene.fog_range.x, 0.0);
			float fog_scaled = scene.fog_color_density.w * fog_distance;
			fog_factor = 1.0 - exp(-(fog_scaled * fog_scaled));
		}
		else
		{
			//雾区间跨度（避免除零）
			float fog_span = max(scene.fog_range.y - scene.fog_range.x, 0.0001);
			float fog_t = clamp((view_distance - scene.fog_range.x) / fog_span, 0.0, 1.0);
			fog_factor = fog_t * fog_t * scene.fog_color_density.w;
		}
		fog_factor = clamp(fog_factor, 0.0, 1.0);
		/*
		雾高度衰减（第一百四十三轮；系数 0 = 关闭）
		因子上再乘 exp(−falloff × max(世界 y, 0))：越高越稀薄，地面与水下不受影响；
		系数为 0 时 exp(0) = 1，结果与不开高度雾逐字节一致。
		*/
		fog_factor *= exp(-scene.fog_range.w * max(in_world.y, 0.0));
		final_rgb = mix(lit, scene.fog_color_density.rgb, fog_factor);
	}
	/*
	曝光补偿（第六百六十轮）
	按 EV 乘以 2 的幂；EV = 0 时 exp2(0) = 1，乘性恒等，画面逐字节不变。
	*/
	final_rgb *= exp2(scene.exposure);
	out_color = vec4(clamp(final_rgb, 0.0, 1.0), albedo_color.a);
}
