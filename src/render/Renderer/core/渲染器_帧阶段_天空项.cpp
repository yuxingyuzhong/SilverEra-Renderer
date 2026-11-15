#include "../局部命名空间使用.h"
//实现体定义（提供 Vulkan / 模块类型）
#include "渲染器实现.h"
//内部工具与帧阶段声明
#include "渲染器内部工具.h"
//帧内共享辅助（合批 / 阴影签名）
#include "渲染器_帧内部.h"
//glm 数学库（逆视图投影矩阵）
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace engine
{
	void frame_stage_sky_item(Renderer_Impl_Holder::Impl* impl, uint32_t slot, const float view_proj[16], VkDeviceSize sky_instance_offset, bool sky_frame)
	{
		/*
		追加天空盒绘制项（第一百二十五轮）
		复用后处理全屏网格与场景帧槽描述符集；推送常量 offset 0 为逆视图投影矩阵，
		片元据此把 NDC 反推为世界方向并采样 binding 5 的立方体贴图。
		*/
		if (sky_frame)
		{
			VkPipeline sky_pipeline_object = impl->pipeline.pipeline_by_handle_get(impl->resource_domain->state().sky_pipeline);
			VkBuffer sky_mesh_buffer = VK_NULL_HANDLE;
			VkDeviceSize sky_mesh_index_offset = 0;
			uint32_t sky_mesh_index_count = 0;
			//若管线与全屏网格齐备
			if (sky_pipeline_object != VK_NULL_HANDLE
				&& impl->resource_domain->state().mesh.mesh_record_get(impl->device_domain->state().fullscreen_mesh, sky_mesh_buffer,
					sky_mesh_index_offset, sky_mesh_index_count))
			{
				//推送常量（16 浮点逆视图投影矩阵）
				impl->pending_push_constants.emplace_back();
				std::array<float, 32>& sky_push = impl->pending_push_constants.back();
				sky_push.fill(0.0f);
				glm::mat4 inverse_view_proj = glm::inverse(glm::make_mat4(view_proj));
				const float* inverse_data = glm::value_ptr(inverse_view_proj);
				for (std::size_t index = 0; index < 16; ++index)
					sky_push[index] = inverse_data[index];
				//天空项（全屏四边形 ＋ 天空管线 ＋ 单位实例；不参与阴影投影）
				Frame_Render_Item sky_item{};
				sky_item.pipeline = sky_pipeline_object;
				sky_item.layout = impl->pipeline.layout_get();
				sky_item.scene_set = impl->pipeline.scene_set_get(slot);
				sky_item.sampler_set = impl->pipeline.sampler_set_get(slot);
				sky_item.descriptor_set_count = 2;
				sky_item.vertex_buffer = sky_mesh_buffer;
				sky_item.index_buffer = sky_mesh_buffer;
				sky_item.index_offset = sky_mesh_index_offset;
				sky_item.index_count = sky_mesh_index_count;
				sky_item.instance_buffer = impl->instance_buffers[slot];
				sky_item.instance_offset = sky_instance_offset;
				sky_item.instance_count = 1;
				sky_item.push_constants = sky_push.data();
				sky_item.push_constant_size = static_cast<uint32_t>(sizeof(float) * 16);
				sky_item.cast_shadow = false;
				sky_item.sample_count = impl->pipeline.pipeline_sample_count(impl->resource_domain->state().sky_pipeline);
				impl->pending_items.push_back(sky_item);
			}
		}
	}
}
