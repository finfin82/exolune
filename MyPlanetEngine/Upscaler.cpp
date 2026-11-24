#define NOMINMAX
#include "Upscaler.h"
#include <cmath>
#include <cstring>
#include <vector>
#include <cwchar>
#include <algorithm>

#ifdef MYPLANETENGINE_HAS_FSR2
#include <ffx_fsr2.h>
#include <dx12/ffx_fsr2_dx12.h>
#endif

#ifdef MYPLANETENGINE_HAS_FSR2
namespace {

void FsrMessage(FfxFsr2MsgType type, const wchar_t* message)
{
    OutputDebugStringW(type == FFX_FSR2_MESSAGE_TYPE_ERROR ? L"[FSR2 error] " : L"[FSR2] ");
    if (message)
        OutputDebugStringW(message);
    OutputDebugStringW(L"\n");
}

} // namespace

struct Upscaler::Impl {
    FfxFsr2Context context = {};
    FfxFsr2Interface backend = {};
    std::vector<uint8_t> scratch;
    bool created = false;
};
#else
struct Upscaler::Impl {};
#endif

Upscaler::Upscaler() : mImpl(std::make_unique<Impl>()) {}
Upscaler::~Upscaler() { Destroy(); }

Upscaler::Type Upscaler::ParseType(const wchar_t* value)
{
    if (!value || !value[0] || wcscmp(value, L"off") == 0 || wcscmp(value, L"0") == 0)
        return Type::Off;
    if (wcscmp(value, L"fsr") == 0 || wcscmp(value, L"fsr2") == 0 || wcscmp(value, L"1") == 0)
        return Type::Fsr;
    return Type::Off;
}

Upscaler::Quality Upscaler::ParseQuality(const wchar_t* value)
{
    if (!value || !value[0])
        return Quality::Quality;
    if (wcscmp(value, L"balanced") == 0 || wcscmp(value, L"1") == 0)
        return Quality::Balanced;
    if (wcscmp(value, L"performance") == 0 || wcscmp(value, L"perf") == 0 || wcscmp(value, L"2") == 0)
        return Quality::Performance;
    return Quality::Quality;
}

const wchar_t* Upscaler::TypeName(Type type)
{
    return type == Type::Fsr ? L"FSR" : L"off";
}

const wchar_t* Upscaler::QualityName(Quality quality)
{
    switch (quality) {
    case Quality::Balanced: return L"Balanced";
    case Quality::Performance: return L"Performance";
    default: return L"Quality";
    }
}

float Upscaler::Ratio(Quality quality)
{
    switch (quality) {
    case Quality::Balanced: return 1.7f;
    case Quality::Performance: return 2.0f;
    default: return 1.5f;
    }
}

void Upscaler::RenderSize(UINT displayW, UINT displayH, float scaleOrZero, Type type, Quality quality,
    UINT* renderW, UINT* renderH)
{
    float scale = 1.0f;
    if (type == Type::Fsr)
        scale = (scaleOrZero > 0.0f) ? scaleOrZero : (1.0f / Ratio(quality));
    else if (scaleOrZero > 0.0f && scaleOrZero < 1.0f)
        scale = scaleOrZero;
    scale = (std::min)((std::max)(scale, 0.25f), 1.0f);
    UINT w = (std::max)(8u, (UINT)(long)std::lround(displayW * scale) & ~1u);
    UINT h = (std::max)(8u, (UINT)(long)std::lround(displayH * scale) & ~1u);
    if (w > displayW) w = displayW;
    if (h > displayH) h = displayH;
    *renderW = w;
    *renderH = h;
}

bool Upscaler::Init(ID3D12Device* device, UINT displayW, UINT displayH, UINT renderW, UINT renderH)
{
    Destroy();
    mDisplayW = displayW;
    mDisplayH = displayH;
    mRenderW = renderW;
    mRenderH = renderH;
#ifndef MYPLANETENGINE_HAS_FSR2
    (void)device;
    return false;
#else
    if (!device || displayW == 0 || displayH == 0 || renderW == 0 || renderH == 0)
        return false;

    const size_t scratchSize = ffxFsr2GetScratchMemorySizeDX12();
    mImpl->scratch.resize(scratchSize);
    std::memset(mImpl->scratch.data(), 0, scratchSize);
    std::memset(&mImpl->backend, 0, sizeof(mImpl->backend));
    if (ffxFsr2GetInterfaceDX12(&mImpl->backend, device, mImpl->scratch.data(), scratchSize) != FFX_OK) {
        return false;
    }

    FfxFsr2ContextDescription desc = {};
    desc.flags = FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE
        | FFX_FSR2_ENABLE_DEPTH_INVERTED
        | FFX_FSR2_ENABLE_DEPTH_INFINITE
        | FFX_FSR2_ENABLE_AUTO_EXPOSURE;
    desc.maxRenderSize = { renderW, renderH };
    desc.displaySize = { displayW, displayH };
    desc.callbacks = mImpl->backend;
    desc.device = ffxGetDeviceDX12(device);
    desc.fpMessage = &FsrMessage;
    std::memset(&mImpl->context, 0, sizeof(mImpl->context));
    if (ffxFsr2ContextCreate(&mImpl->context, &desc) != FFX_OK) {
        return false;
    }
    mImpl->created = true;
    mReady = true;
    return true;
#endif
}

void Upscaler::Destroy()
{
#ifdef MYPLANETENGINE_HAS_FSR2
    if (mImpl && mImpl->created) {
        ffxFsr2ContextDestroy(&mImpl->context);
        mImpl->created = false;
    }
    if (mImpl)
        mImpl->scratch.clear();
#endif
    mReady = false;
}

int Upscaler::JitterPhaseCount() const
{
#ifdef MYPLANETENGINE_HAS_FSR2
    return ffxFsr2GetJitterPhaseCount((int)mRenderW, (int)mDisplayW);
#else
    return 1;
#endif
}

void Upscaler::JitterOffset(int frameIndex, float* x, float* y) const
{
    *x = 0.0f;
    *y = 0.0f;
#ifdef MYPLANETENGINE_HAS_FSR2
    const int phases = (std::max)(JitterPhaseCount(), 1);
    ffxFsr2GetJitterOffset(x, y, frameIndex, phases);
#else
    (void)frameIndex;
#endif
}

bool Upscaler::Dispatch(ID3D12GraphicsCommandList* list,
    ID3D12Resource* color, ID3D12Resource* depth, ID3D12Resource* motion,
    ID3D12Resource* composition, ID3D12Resource* output,
    float jitterX, float jitterY, float frameTimeMs, float fovYRadians,
    bool reset, bool sharpen, float sharpness)
{
#ifndef MYPLANETENGINE_HAS_FSR2
    (void)list; (void)color; (void)depth; (void)motion; (void)composition; (void)output;
    (void)jitterX; (void)jitterY; (void)frameTimeMs; (void)fovYRadians;
    (void)reset; (void)sharpen; (void)sharpness;
    return false;
#else
    if (!mReady || !list || !color || !depth || !motion || !output)
        return false;

    FfxFsr2DispatchDescription d = {};
    d.commandList = ffxGetCommandListDX12(list);
    d.color = ffxGetResourceDX12(&mImpl->context, color, L"fsrColor", FFX_RESOURCE_STATE_COMPUTE_READ);
    d.depth = ffxGetResourceDX12(&mImpl->context, depth, L"fsrDepth", FFX_RESOURCE_STATE_COMPUTE_READ);
    d.motionVectors = ffxGetResourceDX12(&mImpl->context, motion, L"fsrMotion", FFX_RESOURCE_STATE_COMPUTE_READ);
    if (composition)
        d.transparencyAndComposition = ffxGetResourceDX12(&mImpl->context, composition, L"fsrComposition",
            FFX_RESOURCE_STATE_COMPUTE_READ);
    d.output = ffxGetResourceDX12(&mImpl->context, output, L"fsrOutput", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    d.jitterOffset = { jitterX, jitterY };
    d.motionVectorScale = { 1.0f, 1.0f };
    d.renderSize = { mRenderW, mRenderH };
    d.enableSharpening = sharpen;
    d.sharpness = sharpness;
    d.frameTimeDelta = (std::max)(frameTimeMs, 1.0f);
    d.preExposure = 1.0f;
    d.reset = reset;
    d.cameraNear = 0.05f;
    d.cameraFar = 0.0f;
    d.cameraFovAngleVertical = fovYRadians;
    d.viewSpaceToMetersFactor = 1.0f;
    return ffxFsr2ContextDispatch(&mImpl->context, &d) == FFX_OK;
#endif
}
