#pragma once
//公共语义类型与前置声明
#include "渲染器类型.h"

//游戏引擎命名空间
namespace engine
{
	class Renderer;
	struct Particle_Domain_State;

	/*
	渲染器域：粒子
	绘制数据源绑定 / 开关与容量 / 发射器与模拟参数 / GPU 推进与常驻 / billboard 与间接绘制 / 统计观测。
	*/
	class Render_Particle
	{
	public:
		explicit Render_Particle(Renderer* owner);
		~Render_Particle(void);
		//不可拷贝、不可移动
		Render_Particle(const Render_Particle&) = delete;
		Render_Particle& operator=(const Render_Particle&) = delete;

		//=== 原粒子域接口方法（签名原样照抄）===
		/*
		绑定粒子绘制数据源
		states / alive / capacity 由应用层持有（长度须一致）；states 与 alive 均可写
		（GPU 推进回读会覆盖它们，活跃标记置 0 表示该索引已在 GPU 侧死亡，索引回收由调用方负责）。
		传入 nullptr 或 capacity 为 0 表示解绑（此后绘制入口不可用）。
		*/
		void particle_source_bind(Particle_State* states, uint8_t* alive, uint32_t capacity);
		//当前绑定容量（未绑定返回 0）
		uint32_t particle_source_capacity(void) const;
		//粒子路径是否可用（已初始化且计算路径可用即为真；第三百一十一轮）
		bool particle_supported(void) const;
		//开关（仅初始化后允许；关闭不释放容量，重新开启沿用绑定数据）
		bool particle_enable_set(bool enabled);
		//开关状态
		bool particle_enable_get(void) const;
		//GPU 缓冲容量（钳制 [1, 1 048 576]；容量变化重建 GPU 缓冲，不触碰绑定数据）
		bool particle_capacity_set(uint32_t capacity);
		uint32_t particle_capacity_get(void) const;
		//发射器（GPU 推进输入；参数非法整次拒绝，保持既有配置；须先开启）
		bool particle_emitter_set(const Particle_Emitter& emitter);
		//当前发射器快照（观测 / 应用层模拟用；未初始化返回默认构造值）
		Particle_Emitter particle_emitter_get(void) const;
		/*
		模拟统计回报（应用层模拟计数；只累加帧统计，不参与绘制）
		出生 / 死亡计数由模拟方（应用层 CPU 路径或 GPU 推进）上报，供帧统计观测。
		*/
		void particle_stats_add(uint32_t born, uint32_t died);
		//模拟参数（GPU 推进与外观输入；参数非法整次拒绝；须先开启）
		bool particle_params_set(const Particle_Params& params);
		/*
		当前模拟参数快照（第三百七十轮；观测用）
		返回最近一次被接受的参数（含软粒子档位 / 颜色 / 软边等外观字段）；
		未初始化返回默认构造值（各字段为 0）。
		*/
		Particle_Params particle_params_get(void) const;
		//GPU 推进是否就绪（需计算路径可用且粒子已开启；首次调用惰性创建缓冲与管线）
		bool particle_gpu_available(void);
		/*
		GPU 单步推进（第三百一十二轮；第三百一十三轮补生命周期）
		上传当前 CPU 镜像 → 推进着色器（重力 / 阻尼 / 积分 / 年龄）→ 回读覆盖 CPU 镜像；
		回读后按 `lifetime ≤ 0` 判定 GPU 侧死亡并同步回收镜像中的活跃标记。
		返回假表示路径不可用或步长非法。
		*/
		bool particle_gpu_step(float dt);
		//最近一次 GPU 单步判定的死亡数（第三百一十三轮；未开启 / 未推进为 0）
		uint32_t particle_gpu_died_last(void) const;
		//常驻 GPU 推进的活跃粒子数（只读观测；未开启常驻返回 0）
		uint32_t particle_gpu_active_count(void) const;
		/*
		粒子 billboard 绘制（第三百一十四轮）
		把当前活跃粒子按相机朝向四边形一次性实例化登记为**一个绘制项**（实际绘制在下一帧 frame_render）；
		size 为四边形世界边长；未开启 / 无活跃粒子 / 资源创建失败 / size 非法时返回假且不登记任何绘制项。
		*/
		bool particle_draw(float size);
		/*
		粒子混合档（第三百一十六轮；默认 Alpha）
		仅接受 Alpha / Additive；切换即释放旧粒子管线，下次 particle_draw 惰性重建（不改变既有画面行为）。
		*/
		bool particle_blend_set(Blend_Mode mode);
		Blend_Mode particle_blend_get(void) const;
		//粒子深度写入（第三百一十六轮；默认关闭；切换同样重建粒子管线）
		bool particle_depth_write_set(bool enabled);
		bool particle_depth_write_get(void) const;
		/*
		粒子深度分桶近似排序（第三百一十七轮；默认 0 = 关闭）
		0 = 按池索引顺序单次实例化；1..16 = 按视线距离分桶、桶间由远及近登记（桶内保持池索引顺序）。
		本层**不做逐粒子排序**：同桶粒子的叠加上限为整桶一次绘制，结果稳定（同一输入逐帧一致）。
		*/
		bool particle_sort_buckets_set(uint32_t buckets);
		uint32_t particle_sort_buckets_get(void) const;
		/*
		当前粒子 GPU 资源占用字节（第三百二十轮；只读观测；第三百五十二轮补间接命令缓冲）
		双缓冲（容量 × 32 × 2）＋ 力场缓冲（8 × 40）＋ 控制 / 紧凑索引与计数 ＋ 间接命令缓冲（按帧槽 20 B）；资源未创建时为 0。
		*/
		uint64_t particle_gpu_bytes(void) const;
		/*
		常驻推进开关（第三百二十七轮；默认关闭）
		为真时发射与活跃数由 GPU 侧「活跃位图 ＋ 环形写指针」维护，CPU 镜像不参与；
		计算路径不可用 / 未开启粒子时返回假。切换不重建状态缓冲（容量变化仍按既有规则重建）。
		*/
		bool particle_resident_set(bool enabled);
		bool particle_resident_get(void) const;
		/*
		常驻紧凑索引（第三百三十三轮；阶段 D；绘制机制的一部分，保留在渲染层）
		把活跃位图压缩为连续索引数组（供 GPU 侧绘制按 gl_InstanceIndex 取粒子），
		紧凑计数由原子自增产出；返回假表示未开启常驻 / 计算不可用。
		*/
		bool particle_gpu_compact(void);
		//最近一次紧凑结果的索引数（未紧凑 / 未开启常驻为 0）
		uint32_t particle_compact_count_get(void) const;
		/*
		回读紧凑索引（第三百三十三轮；诊断 / 测试用）
		写出前 max_count 项（按紧凑顺序 = 池索引升序），返回实际写出的项数。
		*/
		uint32_t particle_compact_indices_get(uint32_t* out, uint32_t max_count);
		/*
		粒子 GPU 绘制（第三百四十一轮；常驻模式专用）
		先紧凑索引，再以「紧凑索引 → 状态 → 着色器内 billboard」登记**一个不消费实例属性**的绘制项：
		顶点来自单位四边形，实例数 = 紧凑计数；相机位置与尺寸经推送常量下发。
		返回假表示未开启常驻 / 资源或管线不可用 / 无活跃粒子 / size 非法。
		*/
		bool particle_gpu_draw(float size);
		/*
		粒子 GPU 间接绘制（第三百四十二轮；常驻模式专用）
		紧凑索引 → 计算着色器把「紧凑计数」写成一条 VkDrawIndexedIndirectCommand →
		登记一个不消费实例属性的绘制项，录制阶段以**单条** vkCmdDrawIndexedIndirect 下达（实例数取自命令）。
		命令缓冲按帧槽划分，绘制前等待该槽上一轮帧完成，避免覆盖仍在读取的命令。
		返回假表示未开启常驻 / 资源或管线不可用 / 无活跃粒子 / size 非法。
		*/
		bool particle_indirect_draw(float size);
		/*
		粒子间接绘制命令回读（第三百四十二轮；诊断 / 测试）
		写出最近一次 particle_indirect_draw 写入的 5 个 uint32：
		index_count / instance_count / first_index / vertex_offset / first_instance。
		返回假表示未开启常驻 / 尚未写入 / 回读失败。
		*/
		bool particle_indirect_command_get(uint32_t command_out[5]);
		//GPU 推进自检（上传当前 CPU 状态 → 零步推进 → 回读比对；仅验证调度通路与拷贝语义）
		bool particle_gpu_probe(void);

		//=== 迁移期内部入口（阶段 8 收口时降为私有）===
		//初始化（原 renderer_init_services 中的粒子装配；资源全部惰性创建）
		void init(const Renderer_Config& effective);
		//关闭（原 shutdown 中粒子相关部分；幂等）
		void shutdown(void);
		//迁移期：状态直取（供帧模块过渡；阶段 8 改为语义方法）
		Particle_Domain_State& state(void);
		const Particle_Domain_State& state(void) const;

	private:
		//门面自身（迁移期用于调用尚未迁出的门面方法与 impl）
		Renderer* owner = nullptr;
		//粒子域状态
		std::unique_ptr<Particle_Domain_State> state_ptr;
	};
}
