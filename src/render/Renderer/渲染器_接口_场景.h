#pragma once
//公共语义类型与共享实现体访问
#include "渲染器实现访问.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器能力接口：场景
	*/
	class Renderer_Scene : public virtual Renderer_Impl_Holder
	{
	public:
		//相机：设置位置
		void camera_position_set(float x, float y, float z);
		//相机：设置注视点
		void camera_target_set(float x, float y, float z);
		//相机：设置视场角（弧度）
		void camera_fov_set(float radians);
		//相机：设置近远平面
		void camera_near_far_set(float near_plane, float far_plane);
		/*
		切换正交 / 透视投影（第一百六十四轮新增）
		enabled 为真时改用正交投影（垂直可见高度 ortho_height 世界单位，宽按宽高比推出）；
		ortho_height 非正时忽略本次调用；未初始化时静默返回。
		*/
		void camera_ortho_set(bool enabled, float ortho_height);
		/*
		设置相机上方向（第一百八十六轮）
		(x, y, z) 为世界坐标上方向（默认 (0,1,0)）；全零或含非有限分量时整次忽略。
		数据侧对应 `Camera_Data::up` / `Scene_Data::up`。未初始化时静默返回。
		*/
		void camera_up_set(float x, float y, float z);
		/*
		设置宽高比覆盖（第一百八十六轮；0 = 跟随窗口）
		aspect > 0 时投影固定按该比例（离屏 / 非窗口比例输出）；≤ 0 或非有限值时回到跟随窗口。
		数据侧对应 `Camera_Data::aspect` / `Scene_Data::aspect`。
		*/
		void camera_aspect_set(float aspect);
		//查询当前生效宽高比（覆盖优先；未初始化返回 0；第一百八十六轮）
		float camera_aspect_get(void) const;
		//查询视场角（弧度；未初始化返回假且出参不变）
		bool camera_fov_get(float* radians_out) const;
		/*
		查询指定视深处可见的垂直世界高度（第一百六十五轮新增）
		透视：``2 · tan(fov/2) · 视深``；正交：恒为正交高度；视深非正按 0 处理。
		未初始化返回假且不改写出参；出参可为空（仅查询状态）。
		*/
		bool camera_world_height_at(float view_depth, float* height_out) const;
		//查询投影模式与正交高度（未初始化返回假且出参不变）
		bool camera_ortho_get(bool* enabled_out, float* height_out) const;
		/*
		世界点 → 屏幕坐标（第一百四十九轮新增）
		u / v 以左上角为原点（0..1 为视口内，允许越界），depth 为裁剪空间深度（0..1）。
		点位于相机背后（裁剪 w ≤ 0）或未初始化时返回假，出参保持原值。
		*/
		bool camera_project(const float world[3], float* u_out, float* v_out, float* depth_out) const;
		/*
		屏幕坐标 → 世界点（第一百四十九轮新增）
		与 camera_project 互逆；未初始化 / 矩阵不可逆 / 出参为空时返回假。
		*/
		bool camera_unproject(float u, float v, float depth, float world_out[3]) const;
		/*
		世界空间 AABB 是否与视锥相交（第一百六十轮新增）
		保守判定：任一面 8 角点全在外侧才判不可见（不会误剔）；未初始化 / 入参为空返回假。
		*/
		bool camera_aabb_visible(const float min[3], const float max[3]) const;
		/*
		导出相机「视图 × 投影」矩阵（第一百六十一轮新增）
		out 为列主序 16 浮点（同 UBO 布局，可直接用于外部几何 / 拾取计算）；
		未初始化或 out 为空返回假且不写出。
		*/
		bool camera_view_proj(float out[16]) const;

		/*
		应用场景描述（相机位置 / 注视点 / 视场角 / 近远平面 ＋ 清屏色）
		等价于逐项调用上方四个相机 setter 并设置清屏色；未初始化时静默返回。
		**参数校验**：视场角须落在 (0, π)、近平面须为正、远平面须大于近平面，任一非法即整次拒绝
		（记录告警并保持既有相机与清屏色不变）；清屏色分量一律钳制到 [0,1]（负 / 非数归 0）。
		*/
		void scene_apply(const Scene_Data& scene);
		/*
		应用场景描述（三组入参；第四十七轮新增）
		等价于把 camera / light / fog 三组分别提交：相机五项与清屏色、方向光与环境光、
		点 / 聚光灯组与高光 / 衰减模式 / 环境光颜色、距离雾。
		校验规则与 `scene_apply(const Scene_Data&)` 完全一致：相机参数非法即整次拒绝；
		单条光源非法只跳过该条；清屏色与雾参数按各 setter 的钳制 / 忽略规则处理。
		*/
		void scene_apply(const Camera_Data& camera, const Light_Data& light, const Fog_Data& fog);
		/*
		设置清屏色（RGBA）
		各分量钳制到 [0,1] 后生效（负 / 非数归 0、大于 1 归 1）；自下一次 frame_render 起生效；
		未初始化或指针为空时静默返回。
		*/
		void clear_color_set(const float color[4]);
		/*
		设置方向光与环境光（第二十九轮新增）
		direction 为**光传播方向**（指向场景内部；长度须非零，内部归一化），
		color 为光颜色（分量 0..1），intensity 为强度（≥ 0，与颜色相乘后使用），
		ambient 为环境光（0..1，直接叠加到照度）；照度最终钳制到 [0,1]。
		任一参数非法（空指针 / 零向量 / 非数 / 负强度 / 越界颜色）即**整次拒绝**并告警，
		保持既有光照不变；自下一次 frame_render 起生效。
		*/
		void light_set(const float direction[3], const float color[3], float intensity, float ambient);
		/*
		设置点光源（第三十一轮新增；与方向光二选一，后设者生效）
		position 为光源世界位置，radius 为线性衰减半径（> 0；距离达到半径时照度为 0），
		color / intensity / ambient 语义同 `light_set`。
		任一参数非法（空指针 / 半径非正或非数 / 颜色越界 / 强度或环境光越界）即整次拒绝并告警。
		*/
		void light_point_set(const float position[3], const float color[3], float radius, float intensity, float ambient);
		/*
		追加一个点光源（第三十二轮新增；最多 4 个，按登记顺序累加）
		不清空既有光源；超出上限、参数非法（空指针 / 半径非正 / 颜色越界 / 强度或环境光越界）时
		返回假并告警，既有光源保持不变。ambient 为全局环境光（多次调用取最后一次的合法值）。
		attenuation 为**逐光源距离衰减模式**（第四十六轮新增）：0 = 跟随全局（默认）、1 = 线性、2 = 平方反比；
		取值大于 2 时按 2 处理。
		specular 为**逐盏高光缩放**（第四十八轮新增）：1 = 跟随全局（默认）、0 = 该灯不产生高光；须落在 0..100。
		shininess 为**逐盏高光光泽度**（第五十一轮新增）：0 = 跟随全局（默认），> 0 为该灯自身光泽度（1..1000）。
		*/
		bool light_add_point(const float position[3], const float color[3], float radius, float intensity,
			float ambient, uint32_t attenuation = 0, float specular = 1.0f, float shininess = 0.0f);
		/*
		追加一个聚光灯（第三十四轮新增；与点光源同槽、共用 8 盏上限（第六十二轮扩为 8））
		direction 为**光传播方向**（从光源指向被照区域，长度须非零，内部归一化）；
		inner_deg / outer_deg 为内 / 外锥角（度）：须满足 0 < inner_deg < outer_deg ≤ 90，
		锥内照度不衰减、锥外为 0、两者之间按 smoothstep 平滑过渡。
		position / radius / color / intensity / ambient 语义同 `light_add_point`；
		attenuation 语义同 `light_add_point`（0 = 跟随全局、1 = 线性、2 = 平方反比）。
		specular 语义同 `light_add_point`（逐盏高光缩放，0..100）；shininess 语义同 `light_add_point`（逐盏光泽度，0..1000）。
		任一参数非法（空指针 / 方向退化 / 半径非正 / 颜色越界 / 锥角非法）或已满 8 盏时
		返回假并告警，既有光源保持不变。
		*/
		bool light_add_spot(const float position[3], const float direction[3], const float color[3],
			float radius, float intensity, float inner_deg, float outer_deg, float ambient,
			uint32_t attenuation = 0, float specular = 1.0f, float shininess = 0.0f);
		/*
		清空全部点光源并恢复方向光默认强度（第三十二轮新增）
		方向 / 颜色 / 环境光保持既有值；清空后画面回到「仅方向光」。
		*/
		void lights_clear(void);
		/*
		已登记点光源数量（第九十二轮新增；含聚光灯；未初始化返回 0）
		*/
		uint32_t point_light_count(void) const;
		/*
		读取第 index 盏点光源的状态快照（第九十二轮新增；只读诊断）
		成功写出状态并返回真；未初始化 / index 越界返回假（出参保持不变）。
		*/
		bool point_light_state_get(uint32_t index, Point_Light_State& state_out) const;
		/*
		光源记录快照（第二百六十五轮；阶段 D 数据底座）
		把当前光源写成 Light_Record 数组：索引 0 = 主方向光、1 = 补光，其后为逐盏点光 / 聚光；
		返回**写入条数**（≤ capacity；capacity 不足时按上述顺序截断），未初始化返回 0。
		out 为空且 capacity > 0 时返回 0（不写）。
		*/
		uint32_t light_records_get(Light_Record* out, uint32_t capacity) const;
		//当前光源记录总数（主光 ＋ 补光 ＋ 逐盏；不受 capacity 影响）
		uint32_t light_record_count(void) const;
		/*
		设置光源选择模式与预算（第六百四十五轮；A1）
		mode = 0 声明顺序（默认，与引入前逐字节一致）、1 = 重要性选择；其它取值按 0。
		budget = 0 跟随路径上限（旧前向 8 盏 / 簇路径全量），>0 为参与光照的光源预算（钳到容量上限）。
		选择在 scene_apply 时对「候选集合」生效，命中后仍按声明顺序登记（聚光槽位映射不变）。
		*/
		void light_select_set(uint32_t mode, uint32_t budget);
		/*
		设置镜面高光参数（第三十一轮新增；对方向光与点光源均生效）
		intensity 为高光强度（≥ 0，0 = 关闭），shininess 为光泽度（≥ 1，越大高光越集中）。
		任一参数非法（NaN / 强度为负 / 光泽度小于 1）即整次拒绝并告警。
		*/
		void specular_set(float intensity, float shininess);
		/*
		设置距离衰减模式（第三十六轮新增；对点光源与聚光灯共同生效）
		mode = 0 线性衰减（`clamp(1 − d / radius, 0, 1)`，默认）；mode = 1 平方反比（`1 / (1 + (d / radius)²)`）。
		仅接受 0 与 1；其它取值返回假并告警（保持原模式）。自下一次 frame_render 起生效。
		*/
		bool light_attenuation_set(uint32_t mode);
		//读取当前距离衰减模式（第三十六轮新增；未初始化返回 0）
		uint32_t light_attenuation_get(void) const;
		/*
		粒子绘制数据源（第三百零七轮迁入引擎核心层后的职责分离）
		渲染层**不持有也不推进**粒子模拟状态：模拟（发射 / 力场 / 碰撞 / 生命周期）在系统层与
		应用层完成，本层只负责把状态数组上传 GPU 并登记 billboard 绘制。
		绑定后才可绘制；未绑定 / 空指针 / 容量为 0 时各绘制入口返回假且不登记绘制项。
		*/

		//当前聚光灯数量（第一百一十五轮；is_spot 为真的光源数）
		uint32_t spot_light_count(void) const;
	};
}
