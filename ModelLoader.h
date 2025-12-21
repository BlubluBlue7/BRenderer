#pragma once
#include "Mesh.h"
#include <DirectXMath.h>
#include <string>
#include <vector>

using namespace DirectX;

// ============================================================================
// 模型加载器
// 支持加载 OBJ 和 FBX 文件格式（通过 Assimp）
// ============================================================================
class ModelLoader
{
public:
    // 从文件加载模型（支持 OBJ 和 FBX 格式）
    // 返回是否加载成功
    static bool LoadFromFile(const std::string& filename, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
    
    // 计算顶点的法线（如果文件没有提供法线）
    static void CalculateNormals(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    
private:
    // 使用 Assimp 加载模型（支持 FBX、OBJ 等多种格式）
    static bool LoadWithAssimp(const std::string& filename, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
    
    // 使用简单解析器加载 OBJ 文件（备用方法）
    static bool LoadOBJ(const std::string& filename, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
};

