#pragma once
#include <vector>

struct Vertex
{
    float position[3];
    float normal[3];     // 法线向量（用于光照计算）
    float color[3];      // 顶点颜色（材质颜色，可选）
    float texCoord[2];   // 纹理坐标（UV坐标）
};

class Mesh
{
public:
    Mesh() = default;
    ~Mesh() = default;

    void SetVertices(const std::vector<Vertex>& verts) { vertices = verts; }
    void SetIndices(const std::vector<uint32_t>& idx) { indices = idx; }

    const std::vector<Vertex>& GetVertices() const { return vertices; }
    const std::vector<uint32_t>& GetIndices() const { return indices; }
    bool HasIndices() const { return !indices.empty(); }

private:
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
