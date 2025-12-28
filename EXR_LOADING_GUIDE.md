# EXR/HDR 环境贴图加载指南

## ✅ 当前状态

**现在支持 `.exr` 和 `.hdr` 格式的环境贴图！**

## 需要的文件

### 1. stb_image.h

需要下载 `stb_image.h` 单头文件库来支持 `.exr` 和 `.hdr` 格式的加载。

**下载地址：**
- GitHub: https://github.com/nothings/stb/blob/master/stb_image.h
- 直接下载链接：https://raw.githubusercontent.com/nothings/stb/master/stb_image.h

**安装步骤：**
1. 下载 `stb_image.h` 文件
2. 将文件放在项目根目录（`E:\Study\Project\BRenderer\stb_image.h`）
3. 重新编译项目

## 使用方法

### 1. 放置环境贴图文件

将 `.exr` 或 `.hdr` 文件放在以下任一位置，程序会自动查找：

**推荐位置：**
- `E:\Study\Project\BRenderer\Res\environment.exr`（推荐）
- `E:\Study\Project\BRenderer\Res\environment.hdr`

**其他尝试位置：**
- `x64/Debug/Res/environment.exr`
- `x64/Debug/Res/environment.hdr`
- 项目根目录下的 `environment.exr` 或 `environment.hdr`

### 2. 支持的文件格式

- ✅ `.exr` - OpenEXR 格式（高动态范围）
- ✅ `.hdr` - Radiance HDR 格式

### 3. 文件格式要求

环境贴图应该是**等距柱状投影（Equirectangular）**格式，这是最常见的 HDR 环境贴图格式。

**等距柱状投影特点：**
- 宽高比通常是 2:1（例如 2048x1024）
- 图像会被自动转换为立方体贴图（CubeMap）用于渲染

## 获取环境贴图资源

### 免费资源网站：

1. **HDRI Haven** - https://hdrihaven.com/
   - 大量免费 HDR 环境贴图
   - 支持 `.hdr` 和 `.exr` 格式下载

2. **Poly Haven** - https://polyhaven.com/hdris
   - 高质量 HDR 环境贴图
   - 支持多种格式

3. **OpenGameArt** - https://opengameart.org/
   - 游戏资源社区
   - 包含 HDR 环境贴图

## 技术细节

### 加载流程：

1. **文件检测**：程序启动时自动查找环境贴图文件
2. **格式识别**：根据文件扩展名（`.exr` 或 `.hdr`）识别格式
3. **图像加载**：使用 `stb_image` 库加载 HDR 数据
4. **格式转换**：将等距柱状投影转换为立方体贴图（6个面，每面 512x512）
5. **GPU 上传**：创建 DirectX 11 立方体贴图资源

### 立方体贴图生成：

- 输入：等距柱状投影 HDR 图像（例如 2048x1024）
- 输出：立方体贴图（6个面，每面 512x512）
- 转换方法：使用球面坐标到立方体贴图坐标的数学转换

## 故障排除

### 问题：编译错误 "stb_image.h: No such file"

**解决方案：**
1. 确保已下载 `stb_image.h` 文件
2. 将文件放在项目根目录
3. 重新编译项目

### 问题：环境贴图加载失败

**可能原因：**
1. 文件路径不正确
2. 文件格式不支持（确保是 `.exr` 或 `.hdr`）
3. 文件损坏

**解决方案：**
1. 检查文件是否在 `Res/` 目录下
2. 检查文件扩展名是否正确
3. 尝试使用其他 HDR 文件

### 问题：画面显示默认蓝色渐变

**原因：** 环境贴图加载失败，使用了默认环境贴图

**解决方案：**
1. 检查控制台输出，查看加载错误信息
2. 确保文件路径正确
3. 确保文件格式正确

## 注意事项

- 环境贴图文件可能很大（几MB到几十MB），加载可能需要一些时间
- 首次加载时会进行格式转换，可能需要几秒钟
- 如果加载失败，程序会自动使用默认的蓝色渐变环境贴图

