#include "MeshMgr.h"

// ============================================================================
// 创建网格
// 同时创建 CPU 端的网格对象和 GPU 端的网格资源
// ============================================================================
std::shared_ptr<Mesh> MeshMgr::CreateMesh(const std::string& name,
    const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& indices)
{
    // ========================================================================
    // 步骤 1: 创建 CPU 端的网格对象
    // 存储顶点和索引数据（在系统内存中）
    // ========================================================================
    auto mesh = std::make_shared<Mesh>();
    mesh->SetVertices(verts);      // 设置顶点数据
    mesh->SetIndices(indices);     // 设置索引数据（可选）
    
    // 将网格存储到映射表中，使用名称作为键
    meshes[name] = mesh;

    // ========================================================================
    // 步骤 2: 创建 GPU 端的网格资源
    // 将数据上传到 GPU 显存，创建顶点缓冲区和索引缓冲区
    // ========================================================================
    auto gpuMesh = std::make_shared<MeshGPU>();
    // 上传网格数据到 GPU
    if (!gpuMesh->UploadToGPU(device, context, *mesh))
    {
        // 如果上传失败，从映射表中移除
        meshes.erase(name);
        return nullptr;
    }
    
    // 将 GPU 网格资源存储到映射表中
    gpuMeshes[name] = gpuMesh;

    // 返回 CPU 端的网格对象（用于后续可能的修改）
    return mesh;
}

// ============================================================================
// 创建网格（带子网格信息）
// ============================================================================
std::shared_ptr<Mesh> MeshMgr::CreateMesh(const std::string& name,
    const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& indices,
    const std::vector<Submesh>& submeshes)
{
    // 创建 CPU 端的网格对象
    auto mesh = std::make_shared<Mesh>();
    mesh->SetVertices(verts);
    mesh->SetIndices(indices);
    mesh->SetSubmeshes(submeshes);  // 设置子网格信息
    
    // 将网格存储到映射表中
    meshes[name] = mesh;
    
    // 创建 GPU 端的网格资源
    auto gpuMesh = std::make_shared<MeshGPU>();
    if (!gpuMesh->UploadToGPU(device, context, *mesh))
    {
        meshes.erase(name);
        return nullptr;
    }
    
    // 将 GPU 网格资源存储到映射表中
    gpuMeshes[name] = gpuMesh;
    
    return mesh;
}

// ============================================================================
// 获取 GPU 网格资源
// 根据名称查找并返回对应的 GPU 网格资源，用于渲染
// ============================================================================
std::shared_ptr<MeshGPU> MeshMgr::GetMeshGPU(const std::string& name)
{
    // 在映射表中查找
    auto it = gpuMeshes.find(name);
    if (it != gpuMeshes.end())
        return it->second;  // 找到，返回 GPU 网格资源
    
    // 未找到，返回空指针
    return nullptr;
}
