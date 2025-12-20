#pragma once
#include <vector>

struct Vertex
{
    float position[3];
    float color[3];  // 可扩展为法线/UV/切线等
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
