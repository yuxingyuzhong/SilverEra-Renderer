#pragma once
//公共语义类型与共享实现体访问
#include "渲染器实现访问.h"

//游戏引擎命名空间
namespace engine
{
	/*
	渲染器能力接口：纹理
	*/
	class Renderer_Texture : public virtual Renderer_Impl_Holder
	{
	public:
		/*
		纹理加载（RGBA8 紧排像素）
		成功返回真并写出纹理句柄；失败返回假，句柄保持 0。
		*/
		bool texture_load(uint64_t& handle_out, const uint8_t* pixels, uint32_t width, uint32_t height);
		/*
		从文件加载纹理（PNG / JPG / BMP 等经 stb 解码为 RGBA8 后上传）
		成功返回真并写出纹理句柄；文件不存在 / 为空 / 解码失败时返回假，句柄保持 0。
		*/
		bool texture_load_file(uint64_t& handle_out, const std::string& path);
		//程序生成棋盘格纹理
		bool texture_load_checkerboard(uint64_t& handle_out, uint32_t width, uint32_t height, uint32_t cell_size);
		/*
		程序生成恒等 3D LUT（第一百四十四轮新增）
		edge 为每轴纹素数（2..64），生成 RGBA8 的 2D 条带：宽 = edge × edge、高 = edge；
		恒等表输出等于输入，可作为自定义查找表的基线或通路验证。非法 edge 返回假且句柄保持 0。
		*/
		bool texture_load_lut_identity(uint64_t& handle_out, uint32_t edge);
		/*
		从内存像素创建立方体贴图（第一百二十五轮）
		six_faces 为 6 面 RGBA8 紧排（顺序 +X / -X / +Y / -Y / +Z / -Z），每面 width × height；
		用于天空盒背景与简易 IBL 采样；失败返回假且句柄保持 0。
		*/
		bool texture_cube_load(uint64_t& handle_out, const uint8_t* six_faces, uint32_t width, uint32_t height);
		/*
		天空盒 / 简易 IBL 开关（第一百二十五轮）
		启用后场景片元按法线方向与反射方向采样立方体贴图作环境补光，并在主场景 pass 末尾绘制全屏背景；
		默认关闭，关闭时画面与引入前逐字节一致。未初始化时静默忽略。
		*/
		void sky_enable_set(bool enabled);
		//当前是否启用天空盒（未初始化时为假）
		bool sky_enabled(void) const;
		//设置天空盒强度（ambient = 漫反射 / 环境补光、specular = 镜面；负值按 0 处理，默认均为 0）
		void sky_params_set(float ambient, float specular);
		//读取天空盒强度（出参可为空）
		void sky_params_get(float* ambient_out, float* specular_out) const;
		/*
		绑定天空盒立方体贴图（第一百二十五轮；0 = 内置纯白立方体）
		内部先等尽在途帧，再重写全部帧槽 set1 binding 5；句柄无效时保持原绑定。
		*/
		void sky_texture_set(uint64_t texture_handle);
		//当前天空盒立方体贴图句柄（0 = 内置纯白立方体）
		uint64_t sky_texture(void) const;
		//卸载纹理（句柄无效则忽略；绑定过该纹理的帧槽需由调用方重新 texture_bind_set）
		void texture_unload(uint64_t handle);
		/*
		读回纹理第 0 级像素（第二十七轮新增）
		先等尽在途帧（frames_wait）再同步读回；pixels 为 RGBA8 紧排（逐行自上而下），
		capacity 须不小于 宽×高×4；成功时 width_out / height_out（非空）回填纹理尺寸。
		未初始化 / 句柄无效 / 容量不足返回假（尺寸出参为 0）；读回不改变纹理内容与布局。
		*/
		bool texture_readback(uint64_t handle, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		读回纹理第 0 级的指定子区域（第二十八轮新增）
		x / y 为左上角，区域须完整落在纹理内；pixels 为区域 RGBA8 紧排，容量 ≥ 区域宽×高×4。
		*/
		bool texture_readback_region(uint64_t handle, uint32_t x, uint32_t y,
			uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		更新纹理第 0 级的子区域（第一百五十一轮新增；第一百五十四轮补 mip 重建）
		pixels 为 RGBA8 紧排（每行 width 个纹素）；区域须完整落在纹理内，否则整次拒绝。
		更新前会等尽在途帧（避免采样中的图像被改写）；成功后纹理仍处于着色器可读布局，
		其余区域保持不变；含 mip 链的纹理会重建第 1..N 级以保持一致。
		*/
		bool texture_update_region(uint64_t handle, uint32_t x, uint32_t y,
			uint32_t width, uint32_t height, const uint8_t* pixels);
		/*
		更新纹理指定级的子区域（第一百五十八轮新增）
		level 须小于纹理的 mip 级数；x / y / width / height 按该级尺寸计，
		pixels 为 RGBA8 紧排（每行 width 个纹素）；区域须完整落在该级内，否则整次拒绝。
		更新前会等尽在途帧；成功后仅重建该级的下游（level+1 .. 末级），上游级不变。
		*/
		bool texture_update_region_level(uint64_t handle, uint32_t level, uint32_t x, uint32_t y,
			uint32_t width, uint32_t height, const uint8_t* pixels);
		/*
		读回纹理指定级的子区域（第一百六十六轮新增）
		x / y 按该级尺寸计，区域须完整落在该级内；pixels 为该区域 RGBA8 紧排。
		未初始化 / 级号无效 / 越界 / 容量不足返回假且尺寸出参归零。
		*/
		bool texture_readback_region_level(uint64_t handle, uint32_t level, uint32_t x, uint32_t y,
			uint32_t region_width, uint32_t region_height, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		读回纹理指定级整幅像素（第一百五十八轮新增）
		pixels 为 RGBA8 紧排（逐行自上而下），capacity 须不小于 该级宽×高×4；
		成功时写出该级宽 / 高。未初始化 / 级号无效 / 容量不足返回假（尺寸出参为 0）。
		*/
		bool texture_readback_level(uint64_t handle, uint32_t level, uint8_t* pixels, uint32_t capacity,
			uint32_t* width_out, uint32_t* height_out);
		/*
		查询纹理 mip 级数（第一百六十二轮新增；句柄无效 / 未初始化返回假且出参归零）
		*/
		bool texture_mip_levels(uint64_t handle, uint32_t* mip_levels_out) const;
		/*
		查询纹理指定级尺寸（第一百六十一轮新增）
		level 须小于纹理 mip 级数；成功写出该级宽 / 高（任一可为空）并返回真，
		否则返回假且两出参归零。
		*/
		bool texture_size_level(uint64_t handle, uint32_t level, uint32_t* width_out, uint32_t* height_out) const;
		/*
		查询纹理尺寸（第九十轮新增）
		成功时写出宽 / 高（任一非空）并返回真；未初始化 / 句柄无效返回假且出参归零。
		*/
		bool texture_size(uint64_t handle, uint32_t* width_out, uint32_t* height_out) const;
		//设置当前绘制所用纹理（传 0 或无效句柄则回落内置默认白纹理）
		void texture_bind_set(uint64_t texture_handle);
		/*
		设置纹理采样器状态（寻址模式 / 是否启用 mip 过滤 / LOD 范围与偏置 / 各向异性）
		lod_bias 钳到设备上限 `±maxSamplerLodBias`（非数归 0），正值为更模糊、负值为更锐利；
		采样器为模块共享且被烘焙进各帧槽描述符集，故本方法在切换状态后会**重新绑定当前纹理**，
		新状态自下一次绘制起生效；未初始化时静默返回。
		*/
		void texture_sampler_state_set(const Texture_Sampler_State& state);
		/*
		读取当前采样器状态（第七十七轮新增；只读诊断）
		返回门面归一后的生效值（各向异性已按设备能力裁剪、lod_bias 已钳制）；
		未初始化时返回默认状态（默认值即既有行为）。
		*/
		Texture_Sampler_State texture_sampler_state_get(void) const;

	};
}
