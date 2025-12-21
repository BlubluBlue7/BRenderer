# FBX 文件加载指南

## ✅ 当前状态

**`ModelLoader` 现在已支持 FBX 文件！** 通过集成 Assimp 库，现在可以加载多种格式：
- ✅ FBX（Autodesk FBX）
- ✅ OBJ（Wavefront OBJ）
- ✅ DAE（Collada）
- ✅ 3DS（3D Studio）
- ✅ 以及其他 Assimp 支持的格式

## 使用方法

### 1. 放置模型文件

将 FBX 文件放在以下任一位置，程序会自动查找：

- `E:\Study\Project\BRenderer\Res\SKM_Manny_Simple.FBX`（推荐）
- `x64/Debug/Res/SKM_Manny_Simple.FBX`
- 其他可能的路径（程序会尝试多个位置）

### 2. 支持的文件扩展名

程序会自动识别以下扩展名（不区分大小写）：
- `.fbx` / `.FBX` - Autodesk FBX
- `.obj` / `.OBJ` - Wavefront OBJ
- `.dae` - Collada
- `.3ds` - 3D Studio
- `.blend` - Blender
- `.x` - DirectX X
- `.md5mesh` - Doom 3

### 3. 加载逻辑

1. **优先使用 Assimp 加载**：对于 FBX、OBJ 等格式，优先使用 Assimp 库加载
2. **备用 OBJ 解析器**：如果 Assimp 加载 OBJ 失败，会尝试使用简单的 OBJ 解析器
3. **自动处理**：
   - 自动三角化多边形
   - 自动生成法线（如果缺失）
   - 合并相同顶点
   - 支持多个网格合并

## 技术细节

### 使用的库

- **Assimp** (Open Asset Import Library)
  - 通过 vcpkg 安装：`vcpkg install assimp:x64-windows`
  - 项目已配置为自动链接

### 加载的数据

当前实现会加载：
- ✅ 顶点位置
- ✅ 法线（如果没有，会自动生成）
- ✅ 顶点颜色（如果有；否则使用默认浅灰色 0.8, 0.8, 0.8）
- ✅ 面索引

### 不支持的功能（当前）

- ⚠️ 纹理坐标（虽然会读取，但当前渲染器不使用）
- ⚠️ 材质信息
- ⚠️ 骨骼动画
- ⚠️ 纹理贴图

## 当前程序支持的资源

### 必需的：
- ✅ 模型文件（FBX、OBJ 等多种格式）
- ✅ Shader 文件（`Shaders/VertexShader.hlsl`, `Shaders/PixelShader.hlsl`）

### 可选的：
- ⚠️ 纹理文件（当前不支持，渲染器使用顶点颜色）
- ⚠️ 材质文件（当前不支持）

## 故障排除

### 如果模型加载失败：

1. **检查文件路径**：确保 FBX 文件在 `Res` 目录下
2. **检查文件格式**：确保 FBX 文件没有损坏
3. **查看错误信息**：程序会在 Output 窗口输出详细错误信息
4. **尝试 OBJ 格式**：如果 FBX 有问题，可以尝试从 UE 导出为 OBJ

### 如果编译错误（找不到 Assimp 头文件）：

1. 确保 vcpkg 已安装 Assimp：
   ```bash
   vcpkg install assimp:x64-windows
   ```
2. 确保 vcpkg 已集成到 Visual Studio：
   ```bash
   vcpkg integrate install
   ```
3. 重新生成解决方案

## 示例

程序会自动尝试加载以下文件（按优先级）：
1. `Res/SKM_Manny_Simple.FBX`
2. `Res/SKM_Manny_Simple.fbx`
3. `Res/SKM_Manny_Simple.obj`
4. 以及其他可能的路径

