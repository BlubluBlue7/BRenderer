#pragma once

// 确保在包含 Windows.h 相关头文件之前定义 NOMINMAX，避免 min/max 宏冲突
#ifndef NOMINMAX
#define NOMINMAX
#endif

// 先包含标准库头文件
#include <unordered_map>
#include <string>
#include <memory>

// 然后包含Windows和DirectX头文件
#include <windows.h>
#include <d3d11.h>
#include "Mesh.h"
#include "MeshGPU.h"

class MeshMgr
{
public:
    MeshMgr(ID3D11Device* device, ID3D11DeviceContext* context)
        : device(device), context(context) {}

    // 创建 Mesh
    std::shared_ptr<Mesh> CreateMesh(const std::string& name,
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& indices = {});
    
    // 创建 Mesh（带子网格信息）
    std::shared_ptr<Mesh> CreateMesh(const std::string& name,
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& indices,
        const std::vector<Submesh>& submeshes);

    // 获取 GPU 资源
    std::shared_ptr<MeshGPU> GetMeshGPU(const std::string& name);

private:
    ID3D11Device* device;
    ID3D11DeviceContext* context;

    std::unordered_map<std::string, std::shared_ptr<Mesh>> meshes;
    std::unordered_map<std::string, std::shared_ptr<MeshGPU>> gpuMeshes;
};
