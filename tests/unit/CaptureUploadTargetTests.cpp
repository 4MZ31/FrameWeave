#include "veyra/pipeline/CaptureUploadFrame.h"
#include <dxgi1_4.h>
#include <cstdio>

int main() {
    using namespace veyra::pipeline;
    using Microsoft::WRL::ComPtr;
    // WARP supplies a real D3D12 fence independently of the installed adapter.
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> warp;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Fence> fence;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
        FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) ||
        FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        std::fputs("WARP device or fence unavailable\n", stderr);
        return 2;
    }

    AVFrame* frame = allocCaptureUploadFrame(device.Get(), AV_PIX_FMT_NV12, 64, 32);
    if (!frame) return 2;
    auto* upload = captureUploadFrame(frame);
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) ++failures;
    };
    check(selectCaptureUploadTarget(*frame) && upload->direct, "idle GPU accepts direct upload");
    upload->gpuRead(fence.Get(), 1);

    // av_max_alloc is process-wide. This executable has no other FFmpeg users.
    av_max_alloc(1);
    check(!selectCaptureUploadTarget(*frame), "busy GPU rejects write when the CPU allocation fails");
    check(!useCaptureUploadCpuPlanes(*frame), "generic conversion rejects an unavailable CPU target");
    av_max_alloc(SIZE_MAX);
    check(selectCaptureUploadTarget(*frame) && !upload->direct && upload->fallbacks == 1 &&
        frame->data[0] == upload->cpu && frame->data[1] == upload->cpu + upload->chromaOffset,
        "pending fence selects both CPU planes without waiting");
    check(SUCCEEDED(fence->Signal(1)) && selectCaptureUploadTarget(*frame) && upload->direct &&
        frame->data[0] == upload->mapped && frame->data[1] == upload->mapped + upload->chromaOffset,
        "completed fence restores direct upload");
    av_frame_free(&frame);
    AVFrame ordinary{};
    check(selectCaptureUploadTarget(ordinary) && useCaptureUploadCpuPlanes(ordinary),
        "ordinary frames retain their writer");
    return failures ? 1 : 0;
}
