#pragma once

#include <d3d11.h>

// The pause backdrop: the whole frame blurred (dual Kawase), darkened, a little desaturated, with a soft vignette, drawn into the
// back buffer from the IDXGISwapChain::Present detour, before the ImGui text and icons. Render thread only: nothing is locked.
namespace PauseAnytime::Blur
{
    struct Settings
    {
        /// Downsample levels at 1080p; one more per doubling of the height (4K: +1), clamped to 1..6. 4 is a strong blur, 5
        /// leaves colour and rough shapes only.
        int Levels1080 = 4;
        /// Tap spread, 1 = the reference dual filter. Past ~2 wide taps ring on sharp edges: add a level instead.
        float Offset = 1.5f;
        /// Multiplier in display (gamma) terms, also on an sRGB or float back buffer: 0.5 is half as bright.
        float Brightness = 0.5f;
        /// 0 keeps the colours, 1 is grey.
        float Desaturate = 0.3f;
        /// Extra darkening toward the corners (0 none, 1 black), from VignetteInner to VignetteOuter (0 centre, 1 corner).
        float Vignette = 0.4f;
        float VignetteInner = 0.45f;
        float VignetteOuter = 1.15f;
        /// Per-channel multiplier in display terms.
        float Tint[3] = { 1.f, 1.f, 1.f };
    };

    /// Compiles the shaders (d3dcompiler_47.dll, ~20 ms once: call it at the first Present, not at the first pause) and makes the fixed states. False (logged) turns the blur
    /// off for the session. Draw calls it itself, and again if the context's device changes.
    bool Init(ID3D11Device* device);

    /// The per-size resources. Call it in the ResizeBuffers detour before the original: they may hold a view of the back buffer.
    void Release();

    /// Everything, shaders included. Init may run again after it.
    void Shutdown();

    /// Blurs the back buffer into itself through <paramref name="target"/> (a view of <paramref name="backBuffer"/>, the one
    /// ImGui draws through next). <paramref name="fade"/> 0..1 is the effect's opacity: 0 draws nothing. Every piece of
    /// pipeline state it changes is set back. False when nothing was drawn.
    bool Draw(ID3D11DeviceContext* context, ID3D11Texture2D* backBuffer, ID3D11RenderTargetView* target, float fade,
              Settings const& settings = {});

    /// The view format for a TYPELESS back buffer (8-bit: UNORM, not sRGB: UE3 writes gamma-encoded values); any other
    /// format comes back as it is. For the caller's render target view.
    DXGI_FORMAT TypedFormat(DXGI_FORMAT format);

    /// Moves a fade value toward 1 (shown) or 0 (hidden) at 1/duration per second.
    inline float Step(float fade, bool shown, float seconds, float duration = 0.2f)
    {
        float const step = duration > 0 ? seconds / duration : 1.f;
        if (shown) return fade + step < 1 ? fade + step : 1.f;
        return fade - step > 0 ? fade - step : 0.f;
    }

    /// Smoothstep: Draw(..., Ease(fade)) starts and ends the fade gently.
    inline float Ease(float t) { return t * t * (3 - 2 * t); }
}
