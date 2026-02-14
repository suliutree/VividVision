# VividVision Agent Handoff

本文件用于让新开 context 的 agent 快速接管项目。

## 1. 项目目标与当前里程碑

- 项目：跨平台 C++20 渲染引擎原型（Vulkan 后端）。
- 当前目标：`FBX 导入 -> 动画 -> 材质 -> 灯光 -> Vulkan 渲染` 闭环已打通。
- 运行平台重点：macOS Apple Silicon + MoltenVK。
- 当前状态：可加载并渲染 `assets/fbx/*.fbx`，包含骨骼动画、材质、点光/聚光/方向光、多灯、方向光阴影。

## 2. 快速开始（本机命令）

```bash
cmake -S . -B build -G Ninja
cmake --build build -j
ctest --test-dir build --output-on-failure
```

运行 demo：

```bash
./build/engine/vividvision_demo /Users/suliu/Projects/VividVision/assets/fbx/Hip_Hop_Dancing.fbx
```

键位：

- `Space`：暂停/继续动画
- `N`：下一个动画剪辑
- `P`：上一个动画剪辑
- `+`：加速
- `-`：减速
- `鼠标右键拖拽`：绕模型中心旋转
- `鼠标滚轮`：缩放
- `1`：切换 normal map（调试）
- `2`：切换 specular IBL（调试）

## 3. 架构总览

- `engine/platform`：平台层抽象与实现（窗口、文件、时间、线程）
- `engine/platform/macos/MacWindowGLFW.*`：macOS 窗口 + Vulkan surface
- `engine/rhi/vulkan`：Vulkan RAII（instance/device/swapchain/renderer）
- `engine/asset/import/AssimpFbxImporter.*`：FBX -> 内部 Scene
- `engine/render/scene/SceneTypes.hpp`：核心数据模型
- `engine/render/animation/Animator.*`：CPU 动画采样 + 骨骼姿态
- `engine/render/passes/SkinPbrPass.*`：skinned mesh + PBR 渲染通路
- `shaders/skin_pbr.vert` / `shaders/skin_pbr.frag`：主渲染着色器
- `shaders/skin_shadow.vert`：方向光阴影深度通道
- `engine/app/DemoApp.cpp`：demo 主循环
- demo 会自动追加一个程序化网格地板（用于观察接触阴影与尺度）

## 4. 图形与数学约定

- 单位：米（meter）
- 手性：右手系
- Up：`+Y`
- Forward：`-Z`
- 矩阵：列主序 + 列向量
- 变换链：`world = parent * local`
- 骨骼蒙皮：`skinMatrix = globalAnimated * inverseBind`

## 5. 已实现材质能力（当前）

### 5.1 PBR Metal-Roughness

- BaseColor
- Metallic/Roughness（两种模式）
- Packed 模式：单纹理中 `G=roughness`, `B=metallic`
- Separate 模式：独立 metallic 纹理 + roughness 纹理
- Normal（支持强度 `normalScale`）
- Occlusion（支持强度 `occlusionStrength`）
- Emissive（支持强度 `emissiveStrength`）
- Alpha Mask（cutoff）

### 5.2 Legacy Spec-Gloss 兼容

- 导入 `specular` 贴图
- `shininess -> roughness` 近似转换
- 旧 FBX（例如 Mixamo）可走 spec-gloss 兼容路径
- 支持 normal 绿色通道翻转标志 `normalGreenInverted`

### 5.3 关键实现文件

- 材质字段：`engine/render/scene/SceneTypes.hpp`
- 导入映射：`engine/asset/import/AssimpFbxImporter.cpp`
- 绑定与 push constant：`engine/render/passes/SkinPbrPass.cpp`
- shader 采样与 BRDF：`shaders/skin_pbr.frag`

## 6. FBX 导入要点

- 使用 Assimp，启用：
- `aiProcess_Triangulate`
- `aiProcess_GenNormals`
- `aiProcess_CalcTangentSpace`
- `aiProcess_FlipUVs`
- `aiProcess_LimitBoneWeights`

- 纹理支持：
- 外部路径与嵌入纹理（embedded）都支持
- Windows 风格路径自动归一化
- 目录递归按文件名兜底查找

- 骨骼权重：
- 每顶点最多 4 影响
- 自动归一化

## 7. 渲染路径（当前）

- Vulkan swapchain + depth + render pass
- skinned mesh pipeline
- 每帧 UBO + 骨骼 SSBO + 灯光 SSBO
- 阴影 pass（directional depth）+ 主 pass 采样阴影图
- 材质 descriptor set（当前 8 个纹理槽）
- 点光/聚光/方向光，多灯累加
- 增加了 IBL（程序化 HDR 环境）+ ambient + exposure（接近 Mixamo 明亮观感）
- Directional Shadow Map（单级 shadow map，稳定化 + 加权 PCF 5x5）
- 纹理上传支持自动 mipmap 生成；不支持线性 blit 的格式自动回退到单 mip
- 采样器支持各向异性过滤（设备支持时启用）

## 8. 运行日志里值得关注的字段

demo 会打印：

- FBX 加载耗时
- mesh/material/texture/skeleton/bone/clip/light 统计
- 每材质工作流信息（如 `specGloss`、`separateMR`、`flipNormalY`）
- 每秒性能：`FPS` 与 `ms/frame`

## 9. 测试与回归保护

当前测试：

- `vv_unit_animator`
- `vv_unit_weights`
- `vv_unit_import_hiphop`

其中 `vv_unit_import_hiphop` 用于约束 `Hip_Hop_Dancing.fbx` 的材质导入行为（spec-gloss、normalY 等），防止回归。

关键测试文件：

- `tests/unit/test_import_hiphop.cpp`

## 10. 已知限制（真实状态）

- 骨骼调色板容量固定 `1024` 矩阵/帧，超大多角色场景需要扩容或分批上传。
- 阴影当前仅支持方向光，且为单级 shadow map（未实现 CSM / PCSS / VSM）。
- IBL 当前是程序化环境 + 近似 BRDF，尚未接入完整的 `irradiance cubemap + prefiltered cubemap + BRDF LUT` 管线。
- 个别资产在 IBL 下仍可能出现不稳定明暗斑块（已提供运行时调试开关）。
- 暂无纹理流式加载（mipmap 已支持）。

## 11. 下一步优先级（建议）

1. 阴影升级（CSM、接触阴影质量、参数可视化）
2. 完整 IBL 资产管线（HDR 输入、irradiance/prefilter、BRDF LUT）并修复现有 IBL 稳定性问题
3. 材质校准与调试视图（albedo/normal/roughness/metallic/AO/shadow）
4. 纹理流式加载与缓存策略

## 12. 常见问题排查

- `cmake: command not found`：本机未安装 CMake。
- `GLFW reports Vulkan unavailable`：检查 Vulkan SDK/MoltenVK 与 loader 初始化。
- 贴图方向错：确认 `flipNormalY` 与 `aiProcess_FlipUVs`。
- 模型偏暗：检查 `ambient/exposure` 与 light 强度。
