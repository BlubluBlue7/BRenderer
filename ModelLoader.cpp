#include "ModelLoader.h"
#include <DirectXMath.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace DirectX;

// ============================================================================
// 从文件加载模型（支持 OBJ 和 FBX 等多种格式）
// ============================================================================
bool ModelLoader::LoadFromFile(const std::string& filename, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
{
    // 检查文件扩展名
    size_t dotPos = filename.find_last_of('.');
    if (dotPos == std::string::npos)
        return false;
    
    std::string ext = filename.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    // 优先使用 Assimp 加载（支持 FBX、OBJ 等多种格式）
    if (ext == "fbx" || ext == "obj" || ext == "dae" || ext == "3ds" || ext == "blend" || ext == "x" || ext == "md5mesh")
    {
        if (LoadWithAssimp(filename, vertices, indices))
            return true;
        // 如果 Assimp 加载失败，对于 OBJ 文件可以尝试简单解析器
        if (ext == "obj")
            return LoadOBJ(filename, vertices, indices);
        return false;
    }
    
    // 其他格式尝试用 Assimp
    return LoadWithAssimp(filename, vertices, indices);
}

// ============================================================================
// 使用 Assimp 加载模型（支持 FBX、OBJ 等多种格式）
// ============================================================================
bool ModelLoader::LoadWithAssimp(const std::string& filename, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
{
    Assimp::Importer importer;
    
    // 加载场景，应用一些后处理选项
    // aiProcess_Triangulate: 将所有多边形转换为三角形
    // aiProcess_GenNormals: 如果没有法线，生成法线
    // aiProcess_CalcTangentSpace: 计算切线和副切线
    // aiProcess_JoinIdenticalVertices: 合并相同的顶点
    const aiScene* scene = importer.ReadFile(
        filename,
        aiProcess_Triangulate | 
        aiProcess_GenNormals | 
        aiProcess_JoinIdenticalVertices |
        aiProcess_CalcTangentSpace
    );
    
    // 检查加载是否成功
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        return false;
    }
    
    vertices.clear();
    indices.clear();
    
    // 处理场景中的所有网格
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; meshIndex++)
    {
        aiMesh* mesh = scene->mMeshes[meshIndex];
        
        // 获取起始索引（用于多个网格合并）
        uint32_t indexOffset = static_cast<uint32_t>(vertices.size());
        
        // 处理顶点
        for (unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            Vertex vertex;
            
            // 位置
            if (mesh->mVertices)
            {
                vertex.position[0] = mesh->mVertices[i].x;
                vertex.position[1] = mesh->mVertices[i].y;
                vertex.position[2] = mesh->mVertices[i].z;
            }
            else
            {
                vertex.position[0] = 0.0f;
                vertex.position[1] = 0.0f;
                vertex.position[2] = 0.0f;
            }
            
            // 法线
            if (mesh->mNormals)
            {
                vertex.normal[0] = mesh->mNormals[i].x;
                vertex.normal[1] = mesh->mNormals[i].y;
                vertex.normal[2] = mesh->mNormals[i].z;
            }
            else
            {
                vertex.normal[0] = 0.0f;
                vertex.normal[1] = 1.0f;
                vertex.normal[2] = 0.0f;
            }
            
            // 纹理坐标（如果存在，使用第一个纹理坐标通道）
            if (mesh->mTextureCoords[0])
            {
                vertex.texCoord[0] = mesh->mTextureCoords[0][i].x;
                vertex.texCoord[1] = mesh->mTextureCoords[0][i].y;
            }
            else
            {
                // 如果没有纹理坐标，使用默认值(0,0)
                vertex.texCoord[0] = 0.0f;
                vertex.texCoord[1] = 0.0f;
            }
            
            // 顶点颜色（如果存在，使用第一个颜色通道；否则使用默认白色）
            if (mesh->mColors[0])
            {
                vertex.color[0] = mesh->mColors[0][i].r;
                vertex.color[1] = mesh->mColors[0][i].g;
                vertex.color[2] = mesh->mColors[0][i].b;
            }
            else
            {
                // 如果没有顶点颜色，使用默认的浅灰色
                vertex.color[0] = 0.8f;
                vertex.color[1] = 0.8f;
                vertex.color[2] = 0.8f;
            }
            
            vertices.push_back(vertex);
        }
        
        // 处理面（索引）
        for (unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            // 由于已经应用了 aiProcess_Triangulate，每个面应该都是三角形
            for (unsigned int j = 0; j < face.mNumIndices; j++)
            {
                indices.push_back(indexOffset + face.mIndices[j]);
            }
        }
    }
    
    // 如果没有法线，计算法线（检查是否有任何法线为零）
    bool needsNormals = false;
    if (vertices.size() > 0)
    {
        for (const auto& v : vertices)
        {
            if (v.normal[0] == 0.0f && v.normal[1] == 0.0f && v.normal[2] == 0.0f)
            {
                needsNormals = true;
                break;
            }
        }
        if (needsNormals)
        {
            CalculateNormals(vertices, indices);
        }
    }
    
    return !vertices.empty();
}

// ============================================================================
// 使用简单解析器加载 OBJ 文件（备用方法）
// ============================================================================
bool ModelLoader::LoadOBJ(const std::string& filename, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
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

                // 纹理坐标（如果有）
                if (fv.texIdx >= 0 && fv.texIdx < (int)texCoords.size())
                {
                    vertex.texCoord[0] = texCoords[fv.texIdx].x;
                    vertex.texCoord[1] = texCoords[fv.texIdx].y;
                }
                else
                {
                    vertex.texCoord[0] = 0.0f;
                    vertex.texCoord[1] = 0.0f;
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
                        v.normal[2] == vertex.normal[2] &&
                        v.texCoord[0] == vertex.texCoord[0] &&
                        v.texCoord[1] == vertex.texCoord[1] &&
                        v.color[0] == vertex.color[0] &&
                        v.color[1] == vertex.color[1] &&
                        v.color[2] == vertex.color[2])
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

