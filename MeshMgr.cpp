#include "MeshMgr.h"

std::shared_ptr<Mesh> MeshMgr::CreateMesh(const std::string& name,
    const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& indices)
{
    auto mesh = std::make_shared<Mesh>();
    mesh->SetVertices(verts);
    mesh->SetIndices(indices);
    meshes[name] = mesh;

    auto gpuMesh = std::make_shared<MeshGPU>();
    gpuMesh->UploadToGPU(device, context, *mesh);
    gpuMeshes[name] = gpuMesh;

    return mesh;
}

std::shared_ptr<MeshGPU> MeshMgr::GetMeshGPU(const std::string& name)
{
    auto it = gpuMeshes.find(name);
    if (it != gpuMeshes.end()) return it->second;
    return nullptr;
}
