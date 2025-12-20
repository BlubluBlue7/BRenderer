#include "MeshGPU.h"

// ============================================================================
// 上传网格数据到 GPU
// 将 CPU 端的顶点和索引数据上传到 GPU 显存，创建顶点缓冲区和索引缓冲区
// ============================================================================
bool MeshGPU::UploadToGPU(ID3D11Device* device, ID3D11DeviceContext* context, const Mesh& mesh)
{
    // 获取顶点数据
    const auto& verts = mesh.GetVertices();
    if (verts.empty()) return false;

    // ========================================================================
    // 步骤 1: 创建顶点缓冲区
    // 顶点缓冲区存储所有顶点的数据（位置、颜色等）
    // ========================================================================
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.Usage = D3D11_USAGE_DEFAULT;                    // 使用方式：默认（GPU 读写，CPU 不可访问）
    vbDesc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * verts.size()); // 缓冲区大小（字节）
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;          // 绑定标志：用作顶点缓冲区
    vbDesc.CPUAccessFlags = 0;                            // CPU 访问标志：0 表示 CPU 不可访问
    vbDesc.MiscFlags = 0;                                 // 其他标志：无
    vbDesc.StructureByteStride = 0;                       // 结构体大小：0 表示非结构化缓冲区

    // 初始化数据：指定要上传到 GPU 的数据
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = verts.data();                      // 源数据指针（CPU 内存）
    initData.SysMemPitch = 0;                             // 行间距（用于纹理，缓冲区为0）
    initData.SysMemSlicePitch = 0;                        // 切片间距（用于3D纹理，缓冲区为0）

    // 创建顶点缓冲区
    HRESULT hr = device->CreateBuffer(&vbDesc, &initData, vertexBuffer.GetAddressOf());
    if (FAILED(hr)) return false;

    // ========================================================================
    // 步骤 2: 创建索引缓冲区（如果存在索引数据）
    // 索引缓冲区存储顶点的索引，用于重用顶点数据，减少内存占用
    // ========================================================================
    const auto& idx = mesh.GetIndices();
    if (!idx.empty())
    {
        // 配置索引缓冲区描述符
        D3D11_BUFFER_DESC ibDesc = {};
        ibDesc.Usage = D3D11_USAGE_DEFAULT;                    // 使用方式：默认
        ibDesc.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * idx.size()); // 缓冲区大小
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;           // 绑定标志：用作索引缓冲区
        ibDesc.CPUAccessFlags = 0;                            // CPU 不可访问
        ibDesc.MiscFlags = 0;
        ibDesc.StructureByteStride = 0;

        // 初始化数据
        D3D11_SUBRESOURCE_DATA ibData = {};
        ibData.pSysMem = idx.data();                          // 索引数据指针

        // 创建索引缓冲区
        hr = device->CreateBuffer(&ibDesc, &ibData, indexBuffer.GetAddressOf());
        if (FAILED(hr)) return false;

        // 保存索引数量和标志
        indexCount = static_cast<UINT>(idx.size());
        hasIndices = true;
    }
    else
    {
        // 没有索引数据，使用顺序绘制
        hasIndices = false;
    }

    // 保存顶点数量
    vertexCount = (UINT)mesh.GetVertices().size();
    return true;
}

// ============================================================================
// 绑定网格资源到渲染管线
// 将顶点缓冲区和索引缓冲区绑定到输入装配阶段（Input Assembler）
// ============================================================================
void MeshGPU::Bind(ID3D11DeviceContext* context)
{
    // ========================================================================
    // 绑定顶点缓冲区
    // ========================================================================
    UINT offset = 0;  // 从缓冲区开始处的偏移量（字节）
    // 将顶点缓冲区绑定到输入槽 0
    context->IASetVertexBuffers(
        0,                              // 起始输入槽索引
        1,                              // 缓冲区数量
        vertexBuffer.GetAddressOf(),   // 顶点缓冲区数组
        &vertexStride,                  // 每个顶点的大小（字节）
        &offset                         // 偏移量数组
    );

    // ========================================================================
    // 绑定索引缓冲区（如果存在）
    // ========================================================================
    if (hasIndices)
    {
        context->IASetIndexBuffer(
            indexBuffer.Get(),          // 索引缓冲区
            DXGI_FORMAT_R32_UINT,      // 索引格式（32位无符号整数）
            0                           // 偏移量
        );
    }

    // ========================================================================
    // 设置图元拓扑类型
    // 告诉 GPU 如何解释顶点数据（三角形列表、三角形带等）
    // ========================================================================
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // TRIANGLELIST: 每三个顶点组成一个三角形，顶点不共享
}

// ============================================================================
// 执行绘制命令
// 根据是否有索引数据，选择索引绘制或顺序绘制
// ============================================================================
void MeshGPU::Draw(ID3D11DeviceContext* context)
{
    if (hasIndices)
    {
        // 使用索引绘制：通过索引缓冲区重用顶点数据
        // 参数说明：
        // - indexCount: 要绘制的索引数量
        // - 0: 起始索引位置
        // - 0: 起始顶点位置（用于顶点缓冲区偏移）
        context->DrawIndexed(indexCount, 0, 0);
    }
    else
    {
        // 顺序绘制：按顶点缓冲区中的顺序绘制
        // 参数说明：
        // - vertexCount: 要绘制的顶点数量
        // - 0: 起始顶点位置
        context->Draw(vertexCount, 0);
    }
}
