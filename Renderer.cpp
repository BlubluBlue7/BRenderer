#include "Renderer.h"
#include "MeshMgr.h"

#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <cstring>
#pragma comment(lib, "d3dcompiler.lib")

bool Renderer::Initialize(HWND hwnd, int width, int height)
{
    m_width = width;
    m_height = height;

    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferCount = 1;
    scDesc.BufferDesc.Width = width;
    scDesc.BufferDesc.Height = height;
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.OutputWindow = hwnd;
    scDesc.SampleDesc.Count = 1;
    scDesc.SampleDesc.Quality = 0;
    scDesc.Windowed = TRUE;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &scDesc,
        m_swapChain.GetAddressOf(),
        m_device.GetAddressOf(),
        &featureLevel,
        m_context.GetAddressOf()
    );

    if (FAILED(hr))
        return false;

    // ��ȡ BackBuffer ������ RTV
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr))
        return false;

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_rtv.GetAddressOf());
    if (FAILED(hr))
        return false;

    // ��ʼ�� MeshMgr
    m_meshMgr = new MeshMgr(m_device.Get(), m_context.Get());

    // ���������� Mesh
    // 创建 Shader 和 InputLayout
    if (!CreateShaders())
        return false;
    if (!CreateInputLayout())
        return false;

    std::vector<Vertex> verts =
    {
        {{0.0f,  0.5f, 0.0f},  {1.0f, 0.0f, 0.0f}},
        {{0.5f, -0.5f, 0.0f},  {0.0f, 1.0f, 0.0f}},
        {{-0.5f,-0.5f, 0.0f},  {0.0f, 0.0f, 1.0f}}
    };

    m_meshMgr->CreateMesh("Triangle", verts);

    return true;
}

void Renderer::RenderFrame()
{
    if (!m_context || !m_rtv) return;

    // 1. �� RT
    m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);

    float clearColor[4] = { 0.2f, 0.3f, 0.6f, 1.0f }; // ��ɫ
    m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);

    D3D11_VIEWPORT vp{};
    vp.Width = (float)m_width;
    vp.Height = (float)m_height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    // 4. Shader + InputLayout��������ǰ������
    m_context->IASetInputLayout(m_inputLayout.Get());
    m_context->VSSetShader(m_vs.Get(), nullptr, 0);
    m_context->PSSetShader(m_ps.Get(), nullptr, 0);

    // ��Ⱦѭ��
    auto triangleGPU = m_meshMgr->GetMeshGPU("Triangle");
    if (triangleGPU)
    {
       triangleGPU->Bind(m_context.Get());
       triangleGPU->Draw(m_context.Get());
    }


    m_swapChain->Present(1, 0);
}

void Renderer::Cleanup()
{
    if (m_meshMgr)
    {
        delete m_meshMgr;
        m_meshMgr = nullptr;
    }

    m_inputLayout.Reset();
    m_ps.Reset();
    m_vs.Reset();
    m_rtv.Reset();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();
}

bool Renderer::CompileShader(const char* shaderCode, const char* entryPoint, const char* target, ID3DBlob** blob)
{
    if (!shaderCode || !entryPoint || !target || !blob)
        return false;

    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompile(
        shaderCode,
        strlen(shaderCode),
        nullptr,
        nullptr,
        nullptr,
        entryPoint,
        target,
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
        0,
        blob,
        errorBlob.GetAddressOf()
    );

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        return false;
    }

    return true;
}

bool Renderer::CreateShaders()
{
    // Vertex Shader
    const char* vsCode = R"(
        struct VSInput
        {
            float3 position : POSITION;
            float3 color : COLOR;
        };

        struct PSInput
        {
            float4 position : SV_POSITION;
            float3 color : COLOR;
        };

        PSInput VS(VSInput input)
        {
            PSInput output;
            output.position = float4(input.position, 1.0f);
            output.color = input.color;
            return output;
        }
    )";

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    if (!CompileShader(vsCode, "VS", "vs_5_0", vsBlob.GetAddressOf()))
        return false;

    HRESULT hr = m_device->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        m_vs.GetAddressOf()
    );
    if (FAILED(hr))
        return false;

    // Pixel Shader
    const char* psCode = R"(
        struct PSInput
        {
            float4 position : SV_POSITION;
            float3 color : COLOR;
        };

        float4 PS(PSInput input) : SV_TARGET
        {
            return float4(input.color, 1.0f);
        }
    )";

    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    if (!CompileShader(psCode, "PS", "ps_5_0", psBlob.GetAddressOf()))
        return false;

    hr = m_device->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        m_ps.GetAddressOf()
    );
    if (FAILED(hr))
        return false;

    // 保存 VS Blob 用于创建 InputLayout
    m_vsBlob = vsBlob;
    return true;
}

bool Renderer::CreateInputLayout()
{
    if (!m_vsBlob)
        return false;

    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    HRESULT hr = m_device->CreateInputLayout(
        layout,
        ARRAYSIZE(layout),
        m_vsBlob->GetBufferPointer(),
        m_vsBlob->GetBufferSize(),
        m_inputLayout.GetAddressOf()
    );

    return SUCCEEDED(hr);
}
