#
# =============================================================================
# Application/Renderer 源文件列表（生成物，勿手改）
# -----------------------------------------------------------------------------
# 生成方式
#     由 Engine/EngineCore/cmake/生成源文件列表.cmake 在每轮配置时自动维护：把
#     file(GLOB_RECURSE) 的扫描结果与本文件比对（按路径集合），仅在集合有差异时
#     整体写回。
#
# 为什么要这个文件
#     CMakeLists 用 GLOB 收集源文件时，构建脚本里没有任何「具体文件名」。
#     Visual Studio 的文件视图改名 / 迁移操作，靠 File API 的 backtrace 找
#     「可改写的引用」；backtrace 只落在「把源文件直接加入 target 的那条命令」
#     上。本文件用 target_sources 直接列出文件名的字面量，于是 VS 的改写就会落到
#     本文件上；而本文件被 CMakeLists include，属配置依赖，被改写即触发重新配置。
#
# 维护
#     - 不要手工编辑。新增 / 删除 / 改名文件后，下一轮配置自动刷新。
#     - 路径一律相对本层根目录（Application/Renderer），与 VS 改写后的写法一致。
#     - 这里是这些文件的「唯一」加入点：CMakeLists 的 add_library 不再传这些源文件，
#       否则 backtrace 会归到 add_library 上，VS 就又无法改写了。
# =============================================================================
#
target_sources(RendererCore PRIVATE
    "src/render/Asset/core/mtl文件加载.cpp"
    "src/render/Asset/core/obj文件加载.cpp"
    "src/render/Cluster/core/簇划分.cpp"
    "src/render/Compute/core/Compute.cpp"
    "src/render/Device/core/实例创建.cpp"
    "src/render/Device/core/校验层配置.cpp"
    "src/render/Device/core/物理设备选择.cpp"
    "src/render/Device/core/设备上下文.cpp"
    "src/render/Device/core/逻辑设备创建.cpp"
    "src/render/Frame/core/像素回读.cpp"
    "src/render/Frame/core/命令录制.cpp"
    "src/render/Frame/core/帧槽创建.cpp"
    "src/render/Frame/core/并行录制.cpp"
    "src/render/Frame/core/延迟销毁.cpp"
    "src/render/Frame/core/提交呈现.cpp"
    "src/render/GPU驱动/core/剔除.cpp"
    "src/render/GPU驱动/core/包围体.cpp"
    "src/render/HiZ/core/HiZ.cpp"
    "src/render/HiZ/core/遮挡测试.cpp"
    "src/render/Memory/core/VMA实现.cpp"
    "src/render/Memory/core/上传批次.cpp"
    "src/render/Memory/core/分配器创建.cpp"
    "src/render/Memory/core/图像创建.cpp"
    "src/render/Memory/core/缓冲创建.cpp"
    "src/render/Mesh/core/网格加载器.cpp"
    "src/render/Mesh/core/网格缓冲.cpp"
    "src/render/Pipeline/core/图形管线.cpp"
    "src/render/Pipeline/core/描述符布局.cpp"
    "src/render/Pipeline/core/管线.cpp"
    "src/render/Pipeline/core/管线布局.cpp"
    "src/render/Platform/core/事件泵.cpp"
    "src/render/Platform/core/平台上下文.cpp"
    "src/render/Platform/core/窗口创建.cpp"
    "src/render/Platform/core/表面创建.cpp"
    "src/render/RenderTarget/core/目标创建.cpp"
    "src/render/Renderer/core/UI通道.cpp"
    "src/render/Renderer/core/渲染器.cpp"
    "src/render/Scene/core/相机.cpp"
    "src/render/Shader/core/着色器.cpp"
    "src/render/Shader/core/着色器模块.cpp"
    "src/render/Swapchain/core/交换链.cpp"
    "src/render/Swapchain/core/交换链查询.cpp"
    "src/render/Swapchain/core/交换链重建.cpp"
    "src/render/Text/core/字体加载.cpp"
    "src/render/Text/core/文字几何.cpp"
    "src/render/Text/core/文字渲染.cpp"
    "src/render/Texture/core/纹理创建.cpp"
    "src/render/Texture/core/纹理解码.cpp"
    "src/render/Texture/core/采样器创建.cpp"
    "src/render/Thread/core/作业分派.cpp"
)

# 头文件不参与编译，只为让 IDE 建立文件索引；同样经 target_sources 列出，
# 使头文件改名 / 迁移时 VS 也能改写引用
target_sources(RendererCore PRIVATE
    "common/前置头文件包含.h"
    "src/render/Asset/core/文本解析工具.h"
    "src/render/Asset/局部命名空间使用.h"
    "src/render/Asset/资产加载.h"
    "src/render/Cluster/簇划分.h"
    "src/render/Compute/Compute.h"
    "src/render/Compute/局部命名空间使用.h"
    "src/render/Compute/计算数据.h"
    "src/render/Device/core/校验层配置.h"
    "src/render/Device/局部命名空间使用.h"
    "src/render/Device/设备上下文.h"
    "src/render/Frame/局部命名空间使用.h"
    "src/render/Frame/帧上下文.h"
    "src/render/GPU驱动/剔除数据.h"
    "src/render/GPU驱动/包围体.h"
    "src/render/GPU驱动/局部命名空间使用.h"
    "src/render/HiZ/HiZ.h"
    "src/render/HiZ/局部命名空间使用.h"
    "src/render/HiZ/遮挡测试.h"
    "src/render/Memory/内存上下文.h"
    "src/render/Memory/局部命名空间使用.h"
    "src/render/Mesh/局部命名空间使用.h"
    "src/render/Mesh/网格.h"
    "src/render/Mesh/网格数据.h"
    "src/render/Pipeline/局部命名空间使用.h"
    "src/render/Pipeline/材质数据.h"
    "src/render/Pipeline/管线.h"
    "src/render/Pipeline/管线数据.h"
    "src/render/Platform/局部命名空间使用.h"
    "src/render/Platform/平台上下文.h"
    "src/render/RenderTarget/局部命名空间使用.h"
    "src/render/RenderTarget/渲染目标.h"
    "src/render/Renderer/core/渲染器实现.h"
    "src/render/Renderer/core/结果检查.h"
    "src/render/Renderer/局部命名空间使用.h"
    "src/render/Renderer/渲染器.h"
    "src/render/Renderer/渲染器_UI通道.h"
    "src/render/Scene/光源数据.h"
    "src/render/Scene/场景数据.h"
    "src/render/Scene/局部命名空间使用.h"
    "src/render/Scene/相机.h"
    "src/render/Shader/局部命名空间使用.h"
    "src/render/Shader/着色器.h"
    "src/render/Swapchain/交换链.h"
    "src/render/Swapchain/局部命名空间使用.h"
    "src/render/Text/字体.h"
    "src/render/Text/局部命名空间使用.h"
    "src/render/Text/文字布局数据.h"
    "src/render/Text/文字渲染.h"
    "src/render/Texture/局部命名空间使用.h"
    "src/render/Texture/纹理.h"
    "src/render/Texture/纹理数据.h"
    "src/render/Thread/局部命名空间使用.h"
    "src/render/Thread/线程池.h"
)
