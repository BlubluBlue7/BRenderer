#pragma once
#include <vector>
#include <string>

struct Vertex
{
    float position[3];
    float normal[3];     // 法线向量（用于光照计算）
    float color[3];      // 顶点颜色（材质颜色，可选）
    float texCoord[2];   // 纹理坐标（UV坐标）
};

// 子网格结构：表示模型中的一个子网格，使用特定的材质
struct Submesh
{
    std::string materialName;  // 材质名称（用于查找对应的纹理）
    uint32_t indexStart;       // 索引缓冲区中的起始位置
    uint32_t indexCount;       // 该子网格的索引数量
    uint32_t materialIndex;    // 材质索引（用于在纹理数组中查找）
};

class Mesh
{
public:
    Mesh() = default;
    ~Mesh() = default;

    void SetVertices(const std::vector<Vertex>& verts) { vertices = verts; }
    void SetIndices(const std::vector<uint32_t>& idx) { indices = idx; }
    void SetSubmeshes(const std::vector<Submesh>& submeshes) { this->submeshes = submeshes; }
    void AddSubmesh(const Submesh& submesh) { submeshes.push_back(submesh); }

    const std::vector<Vertex>& GetVertices() const { return vertices; }
    const std::vector<uint32_t>& GetIndices() const { return indices; }
    const std::vector<Submesh>& GetSubmeshes() const { return submeshes; }
    bool HasIndices() const { return !indices.empty(); }
    bool HasSubmeshes() const { return !submeshes.empty(); }

private:
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Submesh> submeshes;  // 子网格列表
};
