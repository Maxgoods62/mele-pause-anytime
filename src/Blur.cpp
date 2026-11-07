#include "PauseAnytime/Blur.hpp"
#include "Common/Base.hpp"

#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>

// Passes, for N levels (N from the height, see LevelCount):
//   copy       back buffer -> level 0 (CopyResource, ResolveSubresource under MSAA; none if the back buffer can be read)
//   down       level i-1 -> level i, i = 1..N, each half the size of the one before
//   up         level i+1 -> level i, i = N-1..1
//   composite  level 1 -> back buffer: last tent upsample, desaturate, darken, vignette, dither; blended by the fade.
namespace PauseAnytime::Blur
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        constexpr int MaxLevels = 6;
        constexpr UINT MaxClassInstances = 256;
        /// The PS slots the passes bind (t0, s0, b0), the VS constant buffer slots saved with them.
        constexpr UINT Slots = 1;

        constexpr char Source[] = R"hlsl(
cbuffer Params : register(b0)
{
    float2 SrcTexel;      // 1 / size of the texture read
    float  Offset;        // tap spread, 1 = the reference dual filter
    float  Fade;          // composite: opacity 0..1
    float  Brightness;    // composite: multiplier in display (gamma) terms
    float  Desaturate;    // composite: 0 keeps the colours, 1 is grey
    float  Vignette;      // composite: darkening in the corners, 0..1
    float  VignetteInner; // composite: where it starts, 0 centre, 1 corner
    float3 Tint;          // composite: per-channel multiplier in display terms
    float  Dither;        // composite: noise amplitude in output units (1/255 for 8-bit UNORM, 0 for sRGB and float)
    float  VignetteOuter; // composite: where it is full
    float  Aspect;        // composite: target width / height
    float  Gamma;         // composite: 1 for a gamma-encoded target, 2.2 for an sRGB view or a float one (linear values)
    float  Unused;
};

Texture2D<float4> Src : register(t0);
SamplerState Bilinear : register(s0);

struct VSOut
{
    float4 Pos : SV_Position;
    float2 UV : TEXCOORD0;
};

// One triangle over the whole target: no vertex buffer, no input layout.
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    o.UV = float2((id << 1) & 2, id & 2);
    o.Pos = float4(o.UV * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float3 Fetch(float2 uv) { return Src.SampleLevel(Bilinear, uv, 0).rgb; }

// Dual filter downsample (Bjorge, SIGGRAPH 2015): the centre (a 2x2 box through the bilinear tap) at 4/8 and four diagonal
// taps at 1/8, a 4x4 footprint at Offset 1.
float4 PSDown(VSOut i) : SV_Target
{
    float2 d = SrcTexel * Offset;
    float3 s = Fetch(i.UV) * 4;
    s += Fetch(i.UV - d);
    s += Fetch(i.UV + d);
    s += Fetch(i.UV + float2(d.x, -d.y));
    s += Fetch(i.UV - float2(d.x, -d.y));
    return float4(s * 0.125, 1);
}

// Dual filter upsample: a tent of 8 bilinear taps, one texel of the smaller image away on the axes, half a texel on the diagonals.
float3 Tent(float2 uv, float2 h)
{
    float3 s = Fetch(uv + float2(-2 * h.x, 0));
    s += Fetch(uv + float2(-h.x, h.y)) * 2;
    s += Fetch(uv + float2(0, 2 * h.y));
    s += Fetch(uv + float2(h.x, h.y)) * 2;
    s += Fetch(uv + float2(2 * h.x, 0));
    s += Fetch(uv + float2(h.x, -h.y)) * 2;
    s += Fetch(uv + float2(0, -2 * h.y));
    s += Fetch(uv + float2(-h.x, -h.y)) * 2;
    return s / 12;
}

float4 PSUp(VSOut i) : SV_Target
{
    return float4(Tent(i.UV, SrcTexel * (0.5 * Offset)), 1);
}

float4 PSComposite(VSOut i) : SV_Target
{
    float3 c = Tent(i.UV, SrcTexel * (0.5 * Offset));
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = lerp(c, luma.xxx, Desaturate);
    float2 p = (i.UV - 0.5) * float2(Aspect, 1);
    float r = length(p) * 2 * rsqrt(Aspect * Aspect + 1);
    float v = 1 - Vignette * smoothstep(VignetteInner, VignetteOuter, r);
    // Brightness, Tint and the vignette are display-referred: raised to 2.2 for linear values.
    c *= pow(max(Brightness * v * Tint, 0), Gamma);
    // Interleaved gradient noise (Jimenez 2014): no banding in the dark gradients of an 8-bit target.
    float n = frac(52.9829189 * frac(dot(i.Pos.xy, float2(0.06711056, 0.00583715))));
    c += (n - 0.5) * Dither;
    return float4(c, Fade);
}
)hlsl";

        /// cbuffer Params: 4 float4, same order.
        struct Constants
        {
            float SrcTexel[2];
            float Offset;
            float Fade;
            float Brightness;
            float Desaturate;
            float Vignette;
            float VignetteInner;
            float Tint[3];
            float Dither;
            float VignetteOuter;
            float Aspect;
            float Gamma;
            float Unused;
        };
        static_assert(sizeof(Constants) == 64);

        struct Level
        {
            ComPtr<ID3D11Texture2D> Texture;
            ComPtr<ID3D11ShaderResourceView> View;
            ComPtr<ID3D11RenderTargetView> Target;
            UINT Width = 0;
            UINT Height = 0;
        };

        /// What the sized resources were made for: any change remakes them.
        struct Key
        {
            UINT Width = 0;
            UINT Height = 0;
            UINT Samples = 0;
            DXGI_FORMAT BackFormat = DXGI_FORMAT_UNKNOWN;
            DXGI_FORMAT ViewFormat = DXGI_FORMAT_UNKNOWN;
            /// Identity only: no reference is held through it.
            std::uintptr_t Back = 0;
            int Levels = 0;
            bool operator==(Key const&) const = default;
        };

        struct State
        {
            ComPtr<ID3D11Device> Device;
            ComPtr<ID3D11VertexShader> VS;
            ComPtr<ID3D11PixelShader> Down, Up, Composite;
            ComPtr<ID3D11SamplerState> Sampler;
            ComPtr<ID3D11RasterizerState> Raster;
            ComPtr<ID3D11DepthStencilState> NoDepth;
            ComPtr<ID3D11BlendState> Fading;
            ComPtr<ID3D11Buffer> Params;
            ComPtr<ID3DUserDefinedAnnotation> Annotation;
            bool AnnotationTried = false;
            bool Ready = false;
            bool Failed = false;

            /// [0] the frame (a copy, or a view of the back buffer itself), [1..N] the chain.
            std::array<Level, MaxLevels + 1> Levels;
            Key Sized;
            bool HaveSized = false;
            /// A key no resources could be made for: not retried, not logged again, until the next Release.
            Key Refused;
            bool HaveRefused = false;
            /// The back buffer and view formats of the last "not handled" warning: Release, at the end of each pause, forgets a
            /// refused key, so the next pause tries again, but a format that can't change is not warned of again.
            DXGI_FORMAT WarnedBack = DXGI_FORMAT_UNKNOWN;
            DXGI_FORMAT WarnedView = DXGI_FORMAT_UNKNOWN;
            bool Direct = false;
            /// The target view is sRGB or float: the composite's colour math is on linear values.
            bool Linear = false;
            float Dither = 0;
        };
        /// Leaked on purpose: no D3D Release from DllMain at process exit, after d3d11.dll may have cleaned up.
        State& S = *new State{};

        /// What a Get call fills: raw references, released at the end of the scope.
        template <class T, UINT N>
        struct Refs
        {
            T* Items[N]{};
            UINT Count = N;
            Refs() = default;
            Refs(Refs const&) = delete;
            Refs& operator=(Refs const&) = delete;
            ~Refs()
            {
                for (auto* item : Items)
                {
                    if (item != nullptr) item->Release();
                }
            }
        };

        template <class T>
        struct ShaderSlot
        {
            ComPtr<T> Shader;
            Refs<ID3D11ClassInstance, MaxClassInstances> Instances;
        };

        /// The pipeline state Draw changes, plus IA buffers and VS constant buffers for completeness. ~11 KB, on the stack.
        /// Not saved: CS, the other shader stages' resources, PS UAVs (OMSetRenderTargets keeps them), stream output (off
        /// with the GS), slots past Slots (never bound here).
        struct SavedState
        {
            ComPtr<ID3D11InputLayout> Layout;
            D3D11_PRIMITIVE_TOPOLOGY Topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
            Refs<ID3D11Buffer, 1> VertexBuffer;
            UINT Stride = 0;
            UINT VertexOffset = 0;
            ComPtr<ID3D11Buffer> IndexBuffer;
            DXGI_FORMAT IndexFormat = DXGI_FORMAT_UNKNOWN;
            UINT IndexOffset = 0;

            ShaderSlot<ID3D11VertexShader> VS;
            ShaderSlot<ID3D11HullShader> HS;
            ShaderSlot<ID3D11DomainShader> DS;
            ShaderSlot<ID3D11GeometryShader> GS;
            ShaderSlot<ID3D11PixelShader> PS;
            Refs<ID3D11ShaderResourceView, Slots> PSViews;
            Refs<ID3D11SamplerState, Slots> PSSamplers;
            Refs<ID3D11Buffer, Slots> PSBuffers;
            Refs<ID3D11Buffer, Slots> VSBuffers;

            ComPtr<ID3D11RasterizerState> Raster;
            UINT ViewportCount = 0;
            UINT ScissorCount = 0;
            D3D11_VIEWPORT Viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
            D3D11_RECT Scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};

            ComPtr<ID3D11BlendState> Blend;
            FLOAT BlendFactor[4]{};
            UINT SampleMask = 0;
            ComPtr<ID3D11DepthStencilState> DepthStencil;
            UINT StencilRef = 0;
            Refs<ID3D11RenderTargetView, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> Targets;
            ComPtr<ID3D11DepthStencilView> DepthView;

            /// A predicate left on would skip the copy and the draws.
            ComPtr<ID3D11Predicate> Predicate;
            BOOL PredicateValue = FALSE;

            void Capture(ID3D11DeviceContext* c)
            {
                c->IAGetInputLayout(&Layout);
                c->IAGetPrimitiveTopology(&Topology);
                c->IAGetVertexBuffers(0, 1, VertexBuffer.Items, &Stride, &VertexOffset);
                c->IAGetIndexBuffer(&IndexBuffer, &IndexFormat, &IndexOffset);

                c->VSGetShader(&VS.Shader, VS.Instances.Items, &VS.Instances.Count);
                c->HSGetShader(&HS.Shader, HS.Instances.Items, &HS.Instances.Count);
                c->DSGetShader(&DS.Shader, DS.Instances.Items, &DS.Instances.Count);
                c->GSGetShader(&GS.Shader, GS.Instances.Items, &GS.Instances.Count);
                c->PSGetShader(&PS.Shader, PS.Instances.Items, &PS.Instances.Count);
                c->PSGetShaderResources(0, Slots, PSViews.Items);
                c->PSGetSamplers(0, Slots, PSSamplers.Items);
                c->PSGetConstantBuffers(0, Slots, PSBuffers.Items);
                c->VSGetConstantBuffers(0, Slots, VSBuffers.Items);

                c->RSGetState(&Raster);
                c->RSGetViewports(&ViewportCount, nullptr);
                ViewportCount = std::min<UINT>(ViewportCount, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
                c->RSGetViewports(&ViewportCount, Viewports);
                c->RSGetScissorRects(&ScissorCount, nullptr);
                ScissorCount = std::min<UINT>(ScissorCount, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
                c->RSGetScissorRects(&ScissorCount, Scissors);

                c->OMGetBlendState(&Blend, BlendFactor, &SampleMask);
                c->OMGetDepthStencilState(&DepthStencil, &StencilRef);
                c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, Targets.Items, &DepthView);
                c->GetPredication(&Predicate, &PredicateValue);
            }

            void Restore(ID3D11DeviceContext* c)
            {
                // Our view off first, the game's targets next, its views last: no bind is refused as a read/write hazard.
                ID3D11ShaderResourceView* const none[Slots]{};
                c->PSSetShaderResources(0, Slots, none);
                c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, Targets.Items, DepthView.Get());
                c->OMSetBlendState(Blend.Get(), BlendFactor, SampleMask);
                c->OMSetDepthStencilState(DepthStencil.Get(), StencilRef);

                c->RSSetState(Raster.Get());
                c->RSSetViewports(ViewportCount, Viewports);
                c->RSSetScissorRects(ScissorCount, Scissors);

                c->PSSetShaderResources(0, Slots, PSViews.Items);
                c->PSSetSamplers(0, Slots, PSSamplers.Items);
                c->PSSetConstantBuffers(0, Slots, PSBuffers.Items);
                c->VSSetConstantBuffers(0, Slots, VSBuffers.Items);
                c->VSSetShader(VS.Shader.Get(), VS.Instances.Items, VS.Instances.Count);
                c->HSSetShader(HS.Shader.Get(), HS.Instances.Items, HS.Instances.Count);
                c->DSSetShader(DS.Shader.Get(), DS.Instances.Items, DS.Instances.Count);
                c->GSSetShader(GS.Shader.Get(), GS.Instances.Items, GS.Instances.Count);
                c->PSSetShader(PS.Shader.Get(), PS.Instances.Items, PS.Instances.Count);

                c->IASetInputLayout(Layout.Get());
                c->IASetPrimitiveTopology(Topology);
                c->IASetVertexBuffers(0, 1, VertexBuffer.Items, &Stride, &VertexOffset);
                c->IASetIndexBuffer(IndexBuffer.Get(), IndexFormat, IndexOffset);
                c->SetPredication(Predicate.Get(), PredicateValue);
            }
        };

        /// The typeless group a copy of the back buffer is made in (CopyResource needs the same group), UNKNOWN if not handled.
        DXGI_FORMAT Family(DXGI_FORMAT format)
        {
            switch (format)
            {
            case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
            case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            case DXGI_FORMAT_B8G8R8A8_UNORM:
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
            case DXGI_FORMAT_B8G8R8X8_TYPELESS:
            case DXGI_FORMAT_B8G8R8X8_UNORM:
            case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_TYPELESS;
            case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            case DXGI_FORMAT_R10G10B10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_TYPELESS;
            case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            case DXGI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_TYPELESS;
            default: return DXGI_FORMAT_UNKNOWN;
            }
        }

        struct Encoding
        {
            /// The chain's format, UNKNOWN if the view format is not handled.
            DXGI_FORMAT Chain = DXGI_FORMAT_UNKNOWN;
            bool Linear = false;
            float Dither = 0;
        };

        Encoding EncodingOf(DXGI_FORMAT view)
        {
            switch (view)
            {
            // Gamma-encoded 8 bit: 10-bit fixed point is 4x finer than the image, at the same 4 bytes.
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_B8G8R8A8_UNORM:
            case DXGI_FORMAT_B8G8R8X8_UNORM: return { DXGI_FORMAT_R10G10B10A2_UNORM, false, 1.f / 255 };
            // sRGB views read and write linear values: a float format keeps the darks.
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return { DXGI_FORMAT_R11G11B10_FLOAT, true, 0.f };
            case DXGI_FORMAT_R10G10B10A2_UNORM: return { DXGI_FORMAT_R16G16B16A16_FLOAT, false, 1.f / 1023 };
            // scRGB: linear, past 1 and below 0.
            case DXGI_FORMAT_R16G16B16A16_FLOAT: return { DXGI_FORMAT_R16G16B16A16_FLOAT, true, 0.f };
            default: return {};
            }
        }

        /// Levels1080 at 1080p, one more per doubling of the height: the blur covers the same share of the screen at any
        /// resolution. The smallest level keeps 8 rows at least.
        int LevelCount(UINT height, int levels1080)
        {
            float const scale = std::max(static_cast<float>(height) / 1080.f, 1.f / 64);
            int levels = std::clamp(levels1080 + static_cast<int>(std::lround(std::log2(scale))), 1, MaxLevels);
            while (levels > 1 && (height >> levels) < 8) levels--;
            return levels;
        }

        /// What rounding the level count left over goes into the tap spread: 0.71..1.41 between the doublings.
        float SpreadScale(UINT height, int levels, int levels1080)
        {
            float const scale = static_cast<float>(height) / 1080.f / std::exp2(static_cast<float>(levels - levels1080));
            return std::clamp(scale, 0.5f, 2.f);
        }

        bool Compile(pD3DCompile compile, char const* entry, char const* profile, ComPtr<ID3DBlob>& code)
        {
            ComPtr<ID3DBlob> errors;
            HRESULT const hr = compile(Source, sizeof Source - 1, "PauseAnytimeBlur.hlsl", nullptr, nullptr, entry, profile,
                                       D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
            if (SUCCEEDED(hr)) return true;
            LEASI_ERROR("pause blur: {} ({}) does not compile (0x{:08X}): {}", entry, profile, static_cast<unsigned>(hr),
                        errors ? static_cast<char const*>(errors->GetBufferPointer()) : "no message");
            return false;
        }

        /// The blobs come from d3dcompiler_47.dll and are released in it: they all die before the caller frees the library.
        bool BuildShaders(pD3DCompile compile)
        {
            ComPtr<ID3DBlob> vs, down, up, composite;
            if (!Compile(compile, "VSMain", "vs_4_0", vs) || !Compile(compile, "PSDown", "ps_4_0", down) ||
                !Compile(compile, "PSUp", "ps_4_0", up) || !Compile(compile, "PSComposite", "ps_4_0", composite))
                return false;
            auto* const d = S.Device.Get();
            HRESULT hr = d->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &S.VS);
            if (SUCCEEDED(hr)) hr = d->CreatePixelShader(down->GetBufferPointer(), down->GetBufferSize(), nullptr, &S.Down);
            if (SUCCEEDED(hr)) hr = d->CreatePixelShader(up->GetBufferPointer(), up->GetBufferSize(), nullptr, &S.Up);
            if (SUCCEEDED(hr)) hr = d->CreatePixelShader(composite->GetBufferPointer(), composite->GetBufferSize(), nullptr, &S.Composite);
            if (FAILED(hr)) LEASI_ERROR("pause blur: shader creation failed (0x{:08X})", static_cast<unsigned>(hr));
            return SUCCEEDED(hr);
        }

        bool BuildStates()
        {
            auto* const d = S.Device.Get();

            D3D11_SAMPLER_DESC sampler{};
            sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sampler.MaxLOD = D3D11_FLOAT32_MAX;

            // Scissor off: the game's scissor rects don't matter and stay as they are.
            D3D11_RASTERIZER_DESC raster{};
            raster.FillMode = D3D11_FILL_SOLID;
            raster.CullMode = D3D11_CULL_NONE;
            raster.DepthClipEnable = TRUE;

            D3D11_DEPTH_STENCIL_DESC depth{};
            depth.DepthEnable = FALSE;
            depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
            depth.StencilEnable = FALSE;
            depth.FrontFace.StencilFailOp = depth.FrontFace.StencilDepthFailOp = depth.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
            depth.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
            depth.BackFace = depth.FrontFace;

            // While fading: lerp(frame, blur, Fade). The back buffer's alpha is left as it is.
            D3D11_BLEND_DESC blend{};
            auto& rt = blend.RenderTarget[0];
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOp = D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D11_BLEND_ZERO;
            rt.DestBlendAlpha = D3D11_BLEND_ONE;
            rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            rt.RenderTargetWriteMask = static_cast<UINT8>(D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE);

            D3D11_BUFFER_DESC buffer{};
            buffer.ByteWidth = static_cast<UINT>(sizeof(Constants));
            buffer.Usage = D3D11_USAGE_DYNAMIC;
            buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            HRESULT hr = d->CreateSamplerState(&sampler, &S.Sampler);
            if (SUCCEEDED(hr)) hr = d->CreateRasterizerState(&raster, &S.Raster);
            if (SUCCEEDED(hr)) hr = d->CreateDepthStencilState(&depth, &S.NoDepth);
            if (SUCCEEDED(hr)) hr = d->CreateBlendState(&blend, &S.Fading);
            if (SUCCEEDED(hr)) hr = d->CreateBuffer(&buffer, nullptr, &S.Params);
            if (FAILED(hr)) LEASI_ERROR("pause blur: state creation failed (0x{:08X})", static_cast<unsigned>(hr));
            return SUCCEEDED(hr);
        }

        void ReleaseSized()
        {
            for (auto& level : S.Levels) level = {};
            S.HaveSized = false;
            S.Direct = false;
        }

        bool MakeLevel(Level& level, UINT width, UINT height, DXGI_FORMAT format)
        {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = format;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            level.Width = width;
            level.Height = height;
            return SUCCEEDED(S.Device->CreateTexture2D(&desc, nullptr, &level.Texture)) &&
                   SUCCEEDED(S.Device->CreateShaderResourceView(level.Texture.Get(), nullptr, &level.View)) &&
                   SUCCEEDED(S.Device->CreateRenderTargetView(level.Texture.Get(), nullptr, &level.Target));
        }

        bool CreateSized(ID3D11Texture2D* back, D3D11_TEXTURE2D_DESC const& bb, Key const& key)
        {
            auto const family = Family(bb.Format);
            auto const encoding = EncodingOf(key.ViewFormat);
            if (family == DXGI_FORMAT_UNKNOWN || encoding.Chain == DXGI_FORMAT_UNKNOWN || Family(key.ViewFormat) != family)
            {
                if (bb.Format != S.WarnedBack || key.ViewFormat != S.WarnedView)
                {
                    S.WarnedBack = bb.Format;
                    S.WarnedView = key.ViewFormat;
                    LEASI_WARN("pause blur: back buffer format {} with a view in {} not handled: no blur at this size", static_cast<int>(bb.Format),
                               static_cast<int>(key.ViewFormat));
                }
                return false;
            }

            // Level 0 in the target view's format: an sRGB target is read as sRGB, so the math is on linear values both ways.
            auto& frame = S.Levels[0];
            frame.Width = bb.Width;
            frame.Height = bb.Height;
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = key.ViewFormat;
            view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels = 1;
            // A back buffer made with DXGI_USAGE_SHADER_INPUT is read in place: no copy.
            S.Direct = (bb.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 && bb.SampleDesc.Count == 1 &&
                       SUCCEEDED(S.Device->CreateShaderResourceView(back, &view, &frame.View));
            if (!S.Direct)
            {
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width = bb.Width;
                desc.Height = bb.Height;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = family;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                HRESULT hr = S.Device->CreateTexture2D(&desc, nullptr, &frame.Texture);
                if (SUCCEEDED(hr)) hr = S.Device->CreateShaderResourceView(frame.Texture.Get(), &view, &frame.View);
                if (FAILED(hr))
                {
                    LEASI_ERROR("pause blur: no {}x{} copy of the back buffer (0x{:08X})", bb.Width, bb.Height, static_cast<unsigned>(hr));
                    return false;
                }
            }

            UINT width = bb.Width, height = bb.Height;
            for (int i = 1; i <= key.Levels; i++)
            {
                width = std::max(1u, (width + 1) / 2);
                height = std::max(1u, (height + 1) / 2);
                if (!MakeLevel(S.Levels[i], width, height, encoding.Chain))
                {
                    LEASI_ERROR("pause blur: no {}x{} level {} in format {}", width, height, i, static_cast<int>(encoding.Chain));
                    return false;
                }
            }
            S.Linear = encoding.Linear;
            S.Dither = encoding.Dither;
            LEASI_INFO("pause blur: back buffer {}x{} format {} view {} samples {}, {} levels down to {}x{}, {}", bb.Width, bb.Height,
                       static_cast<int>(bb.Format), static_cast<int>(key.ViewFormat), bb.SampleDesc.Count, key.Levels, width, height,
                       S.Direct ? "read in place" : "copied");
            return true;
        }

        bool Upload(ID3D11DeviceContext* c, Constants const& k)
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(c->Map(S.Params.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
            std::memcpy(mapped.pData, &k, sizeof k);
            c->Unmap(S.Params.Get(), 0);
            return true;
        }

        bool Pass(ID3D11DeviceContext* c, Constants& k, Level const& source, ID3D11RenderTargetView* target, UINT width, UINT height)
        {
            k.SrcTexel[0] = 1.f / static_cast<float>(source.Width);
            k.SrcTexel[1] = 1.f / static_cast<float>(source.Height);
            if (!Upload(c, k)) return false;
            // The target first: it takes the previous pass's target off the output, so the source binds for reading.
            ID3D11ShaderResourceView* const none = nullptr;
            c->PSSetShaderResources(0, 1, &none);
            c->OMSetRenderTargets(1, &target, nullptr);
            D3D11_VIEWPORT const viewport{ 0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f };
            c->RSSetViewports(1, &viewport);
            auto* const view = source.View.Get();
            c->PSSetShaderResources(0, 1, &view);
            c->Draw(3, 0);
            return true;
        }
    }

    bool Init(ID3D11Device* device)
    {
        if (device == nullptr || S.Failed) return false;
        if (S.Ready && S.Device.Get() == device) return true;
        Shutdown();
        S.Device = device;
        // System32 only: no d3dcompiler_47.dll planted next to the game is picked up.
        auto const library = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto const compile = library != nullptr ? reinterpret_cast<pD3DCompile>(reinterpret_cast<void*>(GetProcAddress(library, "D3DCompile"))) : nullptr;
        if (compile == nullptr) LEASI_ERROR("pause blur: no D3DCompile in d3dcompiler_47.dll (error {})", GetLastError());
        bool const built = compile != nullptr && BuildShaders(compile);
        if (library != nullptr) FreeLibrary(library);
        if (!built || !BuildStates())
        {
            Shutdown();
            S.Failed = true;
            LEASI_ERROR("pause blur: off for the session");
            return false;
        }
        S.Ready = true;
        return true;
    }

    void Release()
    {
        ReleaseSized();
        S.HaveRefused = false;
    }

    void Shutdown()
    {
        Release();
        S.VS.Reset();
        S.Down.Reset();
        S.Up.Reset();
        S.Composite.Reset();
        S.Sampler.Reset();
        S.Raster.Reset();
        S.NoDepth.Reset();
        S.Fading.Reset();
        S.Params.Reset();
        S.Annotation.Reset();
        S.AnnotationTried = false;
        S.Device.Reset();
        S.Ready = false;
    }

    bool Draw(ID3D11DeviceContext* context, ID3D11Texture2D* backBuffer, ID3D11RenderTargetView* target, float fade, Settings const& settings)
    {
        if (S.Failed || context == nullptr || backBuffer == nullptr || target == nullptr) return false;
        fade = std::clamp(fade, 0.f, 1.f);
        if (fade <= 0) return false;
        {
            ComPtr<ID3D11Device> device;
            context->GetDevice(&device);
            if (!S.Ready || device.Get() != S.Device.Get())
            {
                if (S.Ready) LEASI_WARN("pause blur: the context's device changed, starting again");
                if (!Init(device.Get())) return false;
            }
        }
        if (!S.AnnotationTried)
        {
            // RenderDoc and PIX show the passes under one event (D3D11.1 runtime; none is fine).
            S.AnnotationTried = true;
            context->QueryInterface(__uuidof(ID3DUserDefinedAnnotation), reinterpret_cast<void**>(S.Annotation.ReleaseAndGetAddressOf()));
        }

        D3D11_TEXTURE2D_DESC bb{};
        backBuffer->GetDesc(&bb);
        D3D11_RENDER_TARGET_VIEW_DESC rv{};
        target->GetDesc(&rv);
        int const levels1080 = std::clamp(settings.Levels1080, 1, MaxLevels);
        Key const key{ bb.Width, bb.Height, bb.SampleDesc.Count, bb.Format, rv.Format, reinterpret_cast<std::uintptr_t>(backBuffer),
                       LevelCount(bb.Height, levels1080) };
        if (!S.HaveSized || !(key == S.Sized))
        {
            if (S.HaveRefused && key == S.Refused) return false;
            ReleaseSized();
            if (!CreateSized(backBuffer, bb, key))
            {
                ReleaseSized();
                S.Refused = key;
                S.HaveRefused = true;
                return false;
            }
            S.Sized = key;
            S.HaveSized = true;
        }

        Constants k{};
        k.Offset = settings.Offset * SpreadScale(bb.Height, key.Levels, levels1080);
        k.Fade = fade;
        k.Brightness = settings.Brightness;
        k.Desaturate = settings.Desaturate;
        k.Vignette = settings.Vignette;
        k.VignetteInner = settings.VignetteInner;
        k.VignetteOuter = std::max(settings.VignetteOuter, settings.VignetteInner + 0.01f);
        std::copy(std::begin(settings.Tint), std::end(settings.Tint), k.Tint);
        k.Dither = S.Dither;
        k.Aspect = static_cast<float>(bb.Width) / static_cast<float>(bb.Height);
        k.Gamma = S.Linear ? 2.2f : 1.f;

        SavedState saved;
        saved.Capture(context);
        if (S.Annotation) S.Annotation->BeginEvent(L"Pause Anytime blur");

        context->SetPredication(nullptr, FALSE);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(S.VS.Get(), nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->RSSetState(S.Raster.Get());
        context->OMSetDepthStencilState(S.NoDepth.Get(), 0);
        auto* const sampler = S.Sampler.Get();
        context->PSSetSamplers(0, 1, &sampler);
        auto* const params = S.Params.Get();
        context->PSSetConstantBuffers(0, 1, &params);
        float const factor[4]{};

        bool ok = true;
        if (!S.Direct)
        {
            if (bb.SampleDesc.Count > 1)
                context->ResolveSubresource(S.Levels[0].Texture.Get(), 0, backBuffer, 0, key.ViewFormat);
            else
                context->CopyResource(S.Levels[0].Texture.Get(), backBuffer);
        }
        context->OMSetBlendState(nullptr, factor, 0xFFFFFFFF);
        context->PSSetShader(S.Down.Get(), nullptr, 0);
        for (int i = 1; ok && i <= key.Levels; i++)
            ok = Pass(context, k, S.Levels[i - 1], S.Levels[i].Target.Get(), S.Levels[i].Width, S.Levels[i].Height);
        context->PSSetShader(S.Up.Get(), nullptr, 0);
        for (int i = key.Levels - 1; ok && i >= 1; i--)
            ok = Pass(context, k, S.Levels[i + 1], S.Levels[i].Target.Get(), S.Levels[i].Width, S.Levels[i].Height);
        if (ok)
        {
            // Opaque once faded in (the shader writes alpha 1): the back buffer is written, not read.
            context->OMSetBlendState(fade < 1 ? S.Fading.Get() : nullptr, factor, 0xFFFFFFFF);
            context->PSSetShader(S.Composite.Get(), nullptr, 0);
            ok = Pass(context, k, S.Levels[1], target, bb.Width, bb.Height);
        }

        saved.Restore(context);
        if (S.Annotation) S.Annotation->EndEvent();
        if (!ok)
        {
            LEASI_ERROR("pause blur: the constant buffer could not be mapped: off for the session");
            Shutdown();
            S.Failed = true;
        }
        return ok;
    }

    DXGI_FORMAT TypedFormat(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default: return format;
        }
    }
}
