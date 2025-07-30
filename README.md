# Transparent DX11 Blur Window (Windows Desktop Duplication + Compute Shader Blur)

This project demonstrates how to create a **transparent topmost window** using the **Win32 API** and **Direct3D 11**, which can **blur the content behind it** in real time.

The application combines several low-level graphics and system techniques:
- DXGI **desktop duplication** API to capture what's behind the window
- A **compute shader** for real-time blurring
- **Layered Win32 windows** with transparency and click-through settings
- Direct3D11 rendering pipeline with full-screen textured quads

## What This App Does

- Initializes a borderless, topmost transparent window using `WS_EX_LAYERED` and `WS_POPUP`.
- Uses DXGI desktop duplication to grab the pixels directly underneath the window.
- Applies a simple box-blur via a compute shader, respecting a masking region.
- Renders the blurred image back to the transparent window using a fullscreen quad.
- Updates every frame, creating a live blurred region on top of the Windows desktop.

## @Important Lines and Why They Matter

The following lines are crucial for the functionality of the app:

### 1. Creating a Transparent, Topmost Layered Window

```cpp
auto style = WS_EX_LAYERED | WS_EX_TOPMOST; // @Important
g_Application.hwnd = CreateWindowEx(
    style,
    "DX11WindowClass",
    "DirectX 11 Window",
    WS_POPUP, // @Important
    ...
);
SetLayeredWindowAttributes(g_Application.hwnd, 0, 255, LWA_ALPHA); // @Important
SetLayeredWindowAttributes(g_Application.hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY); // @Important
SetWindowDisplayAffinity(g_Application.hwnd, WDA_EXCLUDEFROMCAPTURE); // @Important
````

* Enables layered, transparent rendering (`WS_EX_LAYERED`).
* Removes window borders (`WS_POPUP`).
* Makes the window always on top (`WS_EX_TOPMOST`).
* Excludes the window from being captured (prevents recursion when duplicating desktop).
* Sets up alpha and colorkey transparency so we can paint only the blurred regions.

### 2. Acquiring Desktop Content Using DXGI Desktop Duplication

```cpp
HRESULT hr = dxgiOutput1->DuplicateOutput(g_Application.device, &g_Application.desktopDuplication); // @Important
```

* Captures the screen behind the window in real-time.
* Critical for rendering "what's behind" into a texture we can blur.

### 3. Copying Captured Desktop Pixels Into Our Texture

```cpp
g_Application.deviceContext->CopySubresourceRegion(
    g_Application.desktopTexture,
    0, 0, 0, 0,
    acquiredDesktopImage,
    0,
    &sourceBox
); // @Important
```

* Extracts only the region of the screen that matches our window bounds.
* This becomes the base for blurring.

### 4. Compute Shader Blur Logic

```hlsl
cbuffer BlurConstants : register(b0)
{
    uint textureWidth;
    uint textureHeight;
    float blurRadius;
    float padding;
};

Texture2D<float4> InputTexture : register(t0);
Texture2D<float4> MaskTexture : register(t1);
RWTexture2D<float4> OutputTexture : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= textureWidth || id.y >= textureHeight)
        return;

    // Sample the mask at current pixel
    float4 maskValue = MaskTexture[id.xy];

     // If mask is empty (alpha = 0), output transparent black
    if (maskValue.a <= 0.0)
    {
        OutputTexture[id.xy] = float4(0, 0, 0, 0);
        return;
    }

    float4 color = float4(0, 0, 0, 0);
    float samples = 0;

    int radius = (int)blurRadius;

    // Simple box blur - sample surrounding pixels
    for (int x = -radius; x <= radius; x++)
    {
        for (int y = -radius; y <= radius; y++)
        {
            int sampleX = (int)id.x + x;
            int sampleY = (int)id.y + y;

            // Clamp to texture bounds
            sampleX = clamp(sampleX, 0, (int)textureWidth - 1);
            sampleY = clamp(sampleY, 0, (int)textureHeight - 1);

            color += InputTexture[uint2(sampleX, sampleY)];
            samples += 1.0;
        }
    }

    // Average the samples
    color /= samples;

    // Use mask alpha to blend between blurred and transparent
    color.a *= maskValue.a;
    OutputTexture[id.xy] = color;
}
```

* A simple but effective box blur runs per-pixel.
* Thread group size and dispatch dimensions control parallelism.

## License
MIT License or your preferred license.
