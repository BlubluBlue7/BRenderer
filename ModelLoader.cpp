#include "ModelLoader.h"
#include <DirectXMath.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

using namespace DirectX;

// ============================================================================
// 从 OBJ 文件加载模型
// 支持基本的 OBJ 格式：顶点位置、纹理坐标、法线、面
// ============================================================================
bool ModelLoader::LoadFromFile(const std::string& filename, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        return false;
    }

    // 临时存储原始数据
    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT3> normals;
    std::vector<XMFLOAT2> texCoords;
    
    // 用于处理索引（OBJ 使用 1-based 索引）
    struct FaceVertex
    {
        int posIdx = -1;
        int texIdx = -1;
        int normIdx = -1;
    };
    std::vector<std::vector<FaceVertex>> faces;

    std::string line;
    while (std::getline(file, line))
    {
        // 跳过空行和注释
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v")
        {
            // 顶点位置: v x y z
            XMFLOAT3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);
        }
        else if (prefix == "vn")
        {
            // 法线: vn x y z
            XMFLOAT3 norm;
            iss >> norm.x >> norm.y >> norm.z;
            normals.push_back(norm);
        }
        else if (prefix == "vt")
        {
            // 纹理坐标: vt u v
            XMFLOAT2 tex;
            iss >> tex.x >> tex.y;
            texCoords.push_back(tex);
        }
        else if (prefix == "f")
        {
            // 面: f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3
            // 或: f v1 v2 v3
            std::vector<FaceVertex> face;
            std::string vertexStr;
            
            while (iss >> vertexStr)
            {
                FaceVertex fv;
                std::replace(vertexStr.begin(), vertexStr.end(), '/', ' ');
                std::istringstream vss(vertexStr);
                
                // 尝试读取位置索引（必须）
                if (vss >> fv.posIdx)
                {
                    fv.posIdx--; // OBJ 使用 1-based，转换为 0-based
                    
                    // 尝试读取纹理坐标索引（可选）
                    if (vss >> fv.texIdx)
                    {
                        fv.texIdx--;
                        // 尝试读取法线索引（可选）
                        if (vss >> fv.normIdx)
                        {
                            fv.normIdx--;
                        }
                    }
                }
                
                face.push_back(fv);
            }
            
            // 只处理三角形面
            if (face.size() >= 3)
            {
                faces.push_back(face);
            }
        }
    }

    file.close();

    // ========================================================================
    // 将 OBJ 数据转换为我们的顶点格式
    // ========================================================================
    vertices.clear();
    indices.clear();

    // 使用映射来避免重复顶点
    std::vector<Vertex> uniqueVertices;
    std::vector<uint32_t> indexMap;

    for (const auto& face : faces)
    {
        // 将面转换为三角形（如果是多边形，进行三角化）
        for (size_t i = 1; i < face.size() - 1; ++i)
        {
            // 三角形的三个顶点索引
            int idx0 = 0;
            int idx1 = i;
            int idx2 = i + 1;

            // 处理三个顶点
            for (int idx : {idx0, idx1, idx2})
            {
                const FaceVertex& fv = face[idx];
                
                if (fv.posIdx < 0 || fv.posIdx >= (int)positions.size())
                    continue;

                Vertex vertex;
                vertex.position[0] = positions[fv.posIdx].x;
                vertex.position[1] = positions[fv.posIdx].y;
                vertex.position[2] = positions[fv.posIdx].z;

                // 如果有法线，使用它；否则稍后计算
                if (fv.normIdx >= 0 && fv.normIdx < (int)normals.size())
                {
                    vertex.normal[0] = normals[fv.normIdx].x;
                    vertex.normal[1] = normals[fv.normIdx].y;
                    vertex.normal[2] = normals[fv.normIdx].z;
                }
                else
                {
                    // 临时设为 0，稍后计算
                    vertex.normal[0] = 0.0f;
                    vertex.normal[1] = 0.0f;
                    vertex.normal[2] = 0.0f;
                }

                // 默认颜色（白色）
                vertex.color[0] = 1.0f;
                vertex.color[1] = 1.0f;
                vertex.color[2] = 1.0f;

                // 查找是否已存在相同顶点
                bool found = false;
                uint32_t existingIndex = 0;
                
                for (size_t j = 0; j < uniqueVertices.size(); ++j)
                {
                    const Vertex& v = uniqueVertices[j];
                    if (v.position[0] == vertex.position[0] &&
                        v.position[1] == vertex.position[1] &&
                        v.position[2] == vertex.position[2] &&
                        v.normal[0] == vertex.normal[0] &&
                        v.normal[1] == vertex.normal[1] &&
                        v.normal[2] == vertex.normal[2])
                    {
                        found = true;
                        existingIndex = (uint32_t)j;
                        break;
                    }
                }

                if (found)
                {
                    indices.push_back(existingIndex);
                }
                else
                {
                    uint32_t newIndex = (uint32_t)uniqueVertices.size();
                    uniqueVertices.push_back(vertex);
                    indices.push_back(newIndex);
                }
            }
        }
    }

    vertices = uniqueVertices;

    // 如果没有法线，计算它们
    bool hasNormals = false;
    for (const auto& v : vertices)
    {
        if (v.normal[0] != 0.0f || v.normal[1] != 0.0f || v.normal[2] != 0.0f)
        {
            hasNormals = true;
            break;
        }
    }

    if (!hasNormals)
    {
        CalculateNormals(vertices, indices);
    }

    return !vertices.empty();
}

// ============================================================================
// 计算顶点的法线
// 通过计算共享该顶点的所有面的法线平均值
// ============================================================================
void ModelLoader::CalculateNormals(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    // 初始化所有法线为 0
    for (auto& v : vertices)
    {
        v.normal[0] = 0.0f;
        v.normal[1] = 0.0f;
        v.normal[2] = 0.0f;
    }

    // 遍历所有三角形，计算每个面的法线并累加到顶点
    for (size_t i = 0; i < indices.size(); i += 3)
    {
        if (i + 2 >= indices.size())
            break;

        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        Vertex& v0 = vertices[i0];
        Vertex& v1 = vertices[i1];
        Vertex& v2 = vertices[i2];

        // 计算三角形的两条边
        float edge1[3] = {
            v1.position[0] - v0.position[0],
            v1.position[1] - v0.position[1],
            v1.position[2] - v0.position[2]
        };

        float edge2[3] = {
            v2.position[0] - v0.position[0],
            v2.position[1] - v0.position[1],
            v2.position[2] - v0.position[2]
        };

        // 计算叉积（法线）
        float normal[3] = {
            edge1[1] * edge2[2] - edge1[2] * edge2[1],
            edge1[2] * edge2[0] - edge1[0] * edge2[2],
            edge1[0] * edge2[1] - edge1[1] * edge2[0]
        };

        // 归一化
        float length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        if (length > 0.0001f)
        {
            normal[0] /= length;
            normal[1] /= length;
            normal[2] /= length;
        }

        // 累加到三个顶点
        v0.normal[0] += normal[0];
        v0.normal[1] += normal[1];
        v0.normal[2] += normal[2];

        v1.normal[0] += normal[0];
        v1.normal[1] += normal[1];
        v1.normal[2] += normal[2];

        v2.normal[0] += normal[0];
        v2.normal[1] += normal[1];
        v2.normal[2] += normal[2];
    }

    // 归一化所有法线
    for (auto& v : vertices)
    {
        float length = sqrtf(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2]);
        if (length > 0.0001f)
        {
            v.normal[0] /= length;
            v.normal[1] /= length;
            v.normal[2] /= length;
        }
        else
        {
            // 如果法线无效，设为默认值（向上）
            v.normal[0] = 0.0f;
            v.normal[1] = 1.0f;
            v.normal[2] = 0.0f;
        }
    }
}

