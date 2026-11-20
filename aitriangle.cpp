#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl.h>
#include <cmath>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

const char* hlsl = R"(
cbuffer CB : register(b0) { float a; };
struct VOut { float4 pos : SV_POSITION; float3 col : COLOR; };
VOut VS(uint id : SV_VertexID) {
    float2 p[3] = { {0, 0.6}, {0.5, -0.5}, {-0.5, -0.5} };
    float3 c[3] = { {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };
    float s = sin(a), co = cos(a);
    float2 r = float2(p[id].x * co - p[id].y * s, p[id].x * s + p[id].y * co);
    VOut o = { float4(r, 0, 1), c[id] };
    return o;
}
float4 PS(VOut i) : SV_TARGET { return float4(i.col, 1); }
)";

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    WNDCLASS wc = { 0, WndProc, 0, 0, hInst, 0, 0, 0, 0, L"DX12" };
    RegisterClass(&wc);
    HWND hwnd = CreateWindow(L"DX12", L"DX12 Triangle", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 800, 800, 0, 0, hInst, 0);

    ComPtr<IDXGIFactory4> factory; CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    ComPtr<ID3D12Device> dev; D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev));

    D3D12_COMMAND_QUEUE_DESC qd = {};
    ComPtr<ID3D12CommandQueue> queue; dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue));

    DXGI_SWAP_CHAIN_DESC1 scd = { 800, 800, DXGI_FORMAT_R8G8B8A8_UNORM, 0, {1, 0}, DXGI_USAGE_RENDER_TARGET_OUTPUT, 2, DXGI_SCALING_STRETCH, DXGI_SWAP_EFFECT_FLIP_DISCARD };
    ComPtr<IDXGISwapChain1> sc1; factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &scd, nullptr, nullptr, &sc1);
    ComPtr<IDXGISwapChain3> sc; sc1.As(&sc);

    D3D12_DESCRIPTOR_HEAP_DESC rtvHd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2 };
    ComPtr<ID3D12DescriptorHeap> rtvHeap; dev->CreateDescriptorHeap(&rtvHd, IID_PPV_ARGS(&rtvHeap));
    UINT rtvSize = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    ComPtr<ID3D12Resource> rtvs[2];
    D3D12_CPU_DESCRIPTOR_HANDLE rtvH = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < 2; i++) {
        sc->GetBuffer(i, IID_PPV_ARGS(&rtvs[i]));
        dev->CreateRenderTargetView(rtvs[i].Get(), nullptr, rtvH);
        rtvH.ptr += rtvSize;
    }

    D3D12_ROOT_PARAMETER rp = {}; rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp.Constants = { 0, 0, 1 };
    D3D12_ROOT_SIGNATURE_DESC rsd = { 1, &rp };
    ComPtr<ID3DBlob> sig, err, vs, ps;
    D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    ComPtr<ID3D12RootSignature> rootSig; dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&rootSig));

    D3DCompile(hlsl, strlen(hlsl), 0, 0, 0, "VS", "vs_5_0", 0, 0, &vs, 0);
    D3DCompile(hlsl, strlen(hlsl), 0, 0, 0, "PS", "ps_5_0", 0, 0, &ps, 0);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psod = {};
    psod.pRootSignature = rootSig.Get();
    psod.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psod.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psod.RasterizerState = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE };
    psod.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psod.SampleMask = UINT_MAX;
    psod.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psod.NumRenderTargets = 1;
    psod.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psod.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pso; dev->CreateGraphicsPipelineState(&psod, IID_PPV_ARGS(&pso));

    ComPtr<ID3D12CommandAllocator> alloc; dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
    ComPtr<ID3D12GraphicsCommandList> cmd; dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), pso.Get(), IID_PPV_ARGS(&cmd));
    cmd->Close();

    ComPtr<ID3D12Fence> fence; dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE fenceEvt = CreateEvent(0, 0, 0, 0);
    UINT64 fenceVal = 0;

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); continue; }

        UINT idx = sc->GetCurrentBackBufferIndex();
        alloc->Reset();
        cmd->Reset(alloc.Get(), pso.Get());

        D3D12_RESOURCE_BARRIER b1 = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, {{rtvs[idx].Get(), 0, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET}} };
        cmd->ResourceBarrier(1, &b1);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += idx * rtvSize;
        float clear[4] = { 0.08f, 0.08f, 0.12f, 1.0f };
        cmd->ClearRenderTargetView(rtv, clear, 0, nullptr);
        cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

        D3D12_VIEWPORT vp = { 0, 0, 800, 800, 0, 1 };
        D3D12_RECT rect = { 0, 0, 800, 800 };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &rect);

        float angle = GetTickCount64() * 0.002f;
        cmd->SetGraphicsRootSignature(rootSig.Get());
        cmd->SetGraphicsRoot32BitConstants(0, 1, &angle, 0);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);

        D3D12_RESOURCE_BARRIER b2 = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, {{rtvs[idx].Get(), 0, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT}} };
        cmd->ResourceBarrier(1, &b2);
        cmd->Close();

        ID3D12CommandList* lists[] = { cmd.Get() };
        queue->ExecuteCommandLists(1, lists);
        sc->Present(1, 0);

        queue->Signal(fence.Get(), ++fenceVal);
        if (fence->GetCompletedValue() < fenceVal) { fence->SetEventOnCompletion(fenceVal, fenceEvt); WaitForSingleObject(fenceEvt, INFINITE); }
    }
    return 0;
}
