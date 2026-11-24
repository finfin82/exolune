// Thin upscaler front: FSR 2.2 today, with a type enum that can later grow a DLSS path.
#pragma once
#include <d3d12.h>
#include <cstdint>
#include <memory>

class Upscaler {
public:
    enum class Type { Off, Fsr };
    enum class Quality { Quality, Balanced, Performance };

    static Type ParseType(const wchar_t* value);
    static Quality ParseQuality(const wchar_t* value);
    static const wchar_t* TypeName(Type type);
    static const wchar_t* QualityName(Quality quality);
    static float Ratio(Quality quality);
    static void RenderSize(UINT displayW, UINT displayH, float scaleOrZero, Type type, Quality quality,
        UINT* renderW, UINT* renderH);

    Upscaler();
    ~Upscaler();
    Upscaler(const Upscaler&) = delete;
    Upscaler& operator=(const Upscaler&) = delete;

    bool Init(ID3D12Device* device, UINT displayW, UINT displayH, UINT renderW, UINT renderH);
    void Destroy();
    bool Ready() const { return mReady; }

    int JitterPhaseCount() const;
    void JitterOffset(int frameIndex, float* x, float* y) const;

    // color/depth/motion at render resolution, output at display resolution.
    // motion vectors are previous-minus-current in render-target pixels.
    // Resources must already be in a state compatible with compute SRV/UAV as documented on the call site.
    bool Dispatch(ID3D12GraphicsCommandList* list,
        ID3D12Resource* color, ID3D12Resource* depth, ID3D12Resource* motion,
        ID3D12Resource* composition, ID3D12Resource* output,
        float jitterX, float jitterY, float frameTimeMs, float fovYRadians,
        bool reset, bool sharpen, float sharpness);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
    bool mReady = false;
    UINT mRenderW = 0, mRenderH = 0, mDisplayW = 0, mDisplayH = 0;
};
