# stb_image（渲染层外部库）

- **来源**：`nothings/stb` 的 `stb_image.h`（公共领域 / MIT 双授权，`http://nothings.org/stb`）
- **版本**：**v2.30**（文件首行自述 `stb_image - v2.30 - public domain image loader`）
- **引入方式**：按《agent开发指南·外部库引入约束》**拷贝入库内**（与 `src/` 平级的 `external/` 下），不使用投影文件夹、不使用绝对路径；本副本由工作区内既有副本 `Engine/EngineSystem/external/stb/stb_image.h` 逐字节拷贝而来（同一工作区内的既存版本，避免联网取用不同版本）
- **完整性**：`SHA256 = 594C2FE35D49488B4382DBFAEC8F98366DEFCA819D916AC95BECF3E75F4200B3`（283010 字节）
- **用法**：**只在一个编译单元**中先 `#define STB_IMAGE_IMPLEMENTATION` 再包含本头 —— 本层为 `Application/Renderer/src/render/Texture/core/纹理解码.cpp`；其余文件若要使用接口，只包含本头**不带**该宏
- **已启用的功能面**：仅解码（PNG / JPG / BMP 等 stb 支持的格式），未启用写图（未引入 `stb_image_write.h`）、未启用 `STBI_NO_*` 裁剪宏

# stb_truetype（渲染层外部库）

- **来源**：`nothings/stb` 的 `stb_truetype.h`（公共领域 / MIT 双授权，`http://nothings.org/stb`）
- **版本**：**v1.26**（文件首行自述 `stb_truetype.h - v1.26 - public domain`）
- **引入方式**：按《agent开发指南·外部库引入约束》**拷贝入库内**（与 `src/` 平级的 `external/` 下），不使用投影文件夹、不使用绝对路径；本副本由本机既存副本 `D:\代码存储\代码仓库\星屑弹幕\external\raylib-master\src\external\stb_truetype.h` 逐字节拷贝而来（同族单头库，避免联网取用不同版本）
- **完整性**：`SHA256 = 899E3C67CCF08C15AD333A47F1F0BC5972A1F0486F2A8FC88D9ECCBFDD9D6477`（199176 字节）
- **用法**：**只在一个编译单元**中先 `#define STB_TRUETYPE_IMPLEMENTATION` 再包含本头 —— 本层为 `Application/Renderer/src/render/Text/core/字体加载.cpp`；其余文件若要使用接口，只包含本头**不带**该宏
- **已启用的功能面**：仅字形光栅化（`stbtt_InitFont` / `stbtt_ScaleForPixelHeight` / `stbtt_GetCodepointBitmap` / `stbtt_GetCodepointHMetrics` / `stbtt_GetFontVMetrics`），未启用 `STBTT_STATIC`、未启用 `STB_TRUETYPE_IMPLEMENTATION` 之外的任何可选宏
- **注意**：本库自身不做字体文件范围检查（首行明示「NO SECURITY GUARANTEE」），故只用于**可信字体文件**（本层由调用方给出路径，测试用系统字体）
