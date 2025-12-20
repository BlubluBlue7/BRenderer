#pragma once
#include <unordered_map>
#include <string>
#include <memory>
#include "Mesh.h"
#include "MeshGPU.h"
#include <d3d11.h>

class MeshMgr
{
public:
    MeshMgr(ID3D11Device* device, ID3D11DeviceContext* context)
        : device(device), context(context) {}

    // 添加 Mesh
    std::shared_ptr<Mesh> CreateMesh(const std::string& name,
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& indices = {});

    // 获取 GPU 对象
    std::shared_ptr<MeshGPU> GetMeshGPU(const std::string& name);

private:
    ID3D11Device* device;
    ID3D11DeviceContext* context;

    std::unordered_map<std::string, std::shared_ptr<Mesh>> meshes;
    std::unordered_map<std::string, std::shared_ptr<MeshGPU>> gpuMeshes;
};
