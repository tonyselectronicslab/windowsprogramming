#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl.h>
#include <cstdio>
#include <cmath>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

const char* hlsl = R"(
cbuffer CB : register(b0) { float p1y, p2y, bx, by; };
struct VOut { float4 pos : SV_POSITION; float3 col : COLOR; };
VOut VS(uint id : SV_VertexID) {
    float2 q[6] = { {-1, 1}, {1, 1}, {-1, -1}, {-1, -1}, {1, 1}, {1, -1} };
    uint inst = id / 6, v = id % 6;
    float2 pos[3] = { {-0.9f, p1y}, {0.9f, p2y}, {bx, by} };
    float2 sz[3]  = { {0.025f, 0.18f}, {0.025f, 0.18f}, {0.025f, 0.025f} };
    float3 col[3] = { {0.2f, 0.8f, 1.0f}, {1.0f, 0.4f, 0.4f}, {1.0f, 1.0f, 0.3f} };
    VOut o = { float4(pos[inst] + q[v] * sz[inst], 0, 1), col[inst] };
    return o;
}
float4 PS(VOut i) : SV_TARGET { return float4(i.col, 1); }
)";

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    WNDCLASS wc = { 0, WndProc, 0, 0, hInst, 0, 0, 0, 0, L"DX12Pong" };
    RegisterClass(&wc);
    HWND hwnd = CreateWindow(L"DX12Pong", L"DX12 Pong (1P) | You 0 - 0 AI", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 800, 800, 0, 0, hInst, 0);

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

    D3D12_ROOT_PARAMETER rp = {}; rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp.Constants = { 0, 0, 4 };
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
    UINT64 fenceVal = 0, lastT = GetTickCount64();

    float state[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // p1y, p2y, bx, by
    float vx = 1.1f, vy = 0.75f;
    int s1 = 0, s2 = 0;

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); continue; }

        UINT64 now = GetTickCount64();
        float dt = (now - lastT) * 0.001f;
        lastT = now;

        // Player (Left Paddle): W/S or Up/Down
        if ((GetAsyncKeyState('W') | GetAsyncKeyState(VK_UP)) & 0x8000) state[0] += 1.6f * dt;
        if ((GetAsyncKeyState('S') | GetAsyncKeyState(VK_DOWN)) & 0x8000) state[0] -= 1.6f * dt;

        // AI (Right Paddle): tracks ball when approaching, returns toward center otherwise
        float targetY = (vx > 0) ? state[3] : 0.0f;
        float diff = targetY - state[1];
        if (fabsf(diff) > 0.02f) state[1] += (diff > 0 ? 1.15f : -1.15f) * dt;

        for (int i = 0; i < 2; i++) if (state[i] > 0.82f) state[i] = 0.82f; else if (state[i] < -0.82f) state[i] = -0.82f;

        // Ball movement & collisions
        state[2] += vx * dt; state[3] += vy * dt;
        if (state[3] > 0.975f) { state[3] = 0.975f; vy = -vy; }
        if (state[3] < -0.975f) { state[3] = -0.975f; vy = -vy; }

        if (vx < 0 && state[2] < -0.85f && state[2] > -0.95f && fabsf(state[3] - state[0]) < 0.205f) { vx = -vx * 1.05f; vy += (state[3] - state[0]) * 2.0f; }
        if (vx > 0 && state[2] > 0.85f && state[2] < 0.95f && fabsf(state[3] - state[1]) < 0.205f) { vx = -vx * 1.05f; vy += (state[3] - state[1]) * 2.0f; }

        if (state[2] < -1.0f || state[2] > 1.0f) {
            (state[2] < 0 ? s2 : s1)++;
            state[2] = state[3] = 0.0f;
            vx = (vx > 0 ? -1.1f : 1.1f); vy = 0.75f;
            wchar_t title[64]; swprintf_s(title, L"DX12 Pong (1P) | You %d - %d AI", s1, s2);
            SetWindowText(hwnd, title);
        }

        UINT idx = sc->GetCurrentBackBufferIndex();
        alloc->Reset();
        cmd->Reset(alloc.Get(), pso.Get());

        D3D12_RESOURCE_BARRIER b1 = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, {{rtvs[idx].Get(), 0, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET}} };
        cmd->ResourceBarrier(1, &b1);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += idx * rtvSize;
        float clear[4] = { 0.06f, 0.06f, 0.09f, 1.0f };
        cmd->ClearRenderTargetView(rtv, clear, 0, nullptr);
        cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

        D3D12_VIEWPORT vp = { 0, 0, 800, 800, 0, 1 };
        D3D12_RECT rect = { 0, 0, 800, 800 };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &rect);

        cmd->SetGraphicsRootSignature(rootSig.Get());
        cmd->SetGraphicsRoot32BitConstants(0, 4, state, 0);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(18, 1, 0, 0);

        D3D12_RESOURCE_BARRIER b2 = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, {{rtvs[idx].Get(), 0, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT}} };
        cmd->ResourceBarrier(1, &b2);
        cmd->Close();

        ID3D12CommandList* lists[] = { cmd.Get() };
        queue->ExecuteCommandLists(1, lists);
        sc->Present(1, 0);

        queue->Signal(fence.Get(), ++fenceVal);
        if (fence->GetCompletedValue() < fenceVal) { fence->SetEventOnCompletion(fenceVal, fenceEvt); WaitForSingleObject(fenceEvt, INFINITE); }
    }

    CloseHandle(fenceEvt);
    return 0;
}
