#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

struct Application
{
    HWND hwnd;
    HINSTANCE hInstance;

    ID3D11Device* device;
    ID3D11DeviceContext* deviceContext;
    IDXGISwapChain* swapChain;
    ID3D11RenderTargetView* renderTargetView;

    int windowWidth;
    int windowHeight;
    bool isRunning;

	// Add these to your Application struct:
	ID3D11Buffer* vertexBuffer;
	ID3D11VertexShader* vertexShader;
	ID3D11PixelShader* pixelShader;
	ID3D11InputLayout* inputLayout;

	// Add these to your Application struct (in addition to triangle stuff):
	ID3D11Buffer* quadVertexBuffer;
	ID3D11VertexShader* quadVertexShader;
	ID3D11PixelShader* quadPixelShader;
	ID3D11InputLayout* quadInputLayout;
	ID3D11SamplerState* samplerState;

	// Add to Application struct:
	IDXGIOutputDuplication* desktopDuplication;
	ID3D11Texture2D* desktopTexture;
	ID3D11ShaderResourceView* desktopSRV;
	ID3D11RenderTargetView* desktopRTV;

	// Compute shader blur resources
    ID3D11ComputeShader* blurComputeShader;
    ID3D11Texture2D* blurTexture;
    ID3D11ShaderResourceView* blurOutputSRV;
    ID3D11UnorderedAccessView* blurOutputUAV;
    ID3D11RenderTargetView* blurOutputRTV;
    ID3D11Buffer* blurConstantBuffer;

	// Mask texture resources
    ID3D11Texture2D* maskTexture;
    ID3D11ShaderResourceView* maskSRV;
	ID3D11RenderTargetView* maskRTV;
};

static Application g_Application = {};

// Vertex structure
struct Vertex
{
    float x, y, z;    // Position
    float r, g, b, a; // Color
};

// Blur parameters constant buffer structure
struct BlurConstants
{
    UINT textureWidth;
    UINT textureHeight;
    float blurRadius;  // How many pixels to blur (e.g., 3.0f for 3-pixel radius)
    float padding;     // Padding to align to 16 bytes
};

// Shader source code
const char* vertexShaderSource = R"(
struct VS_INPUT {
    float3 pos : POSITION;
    float4 color : COLOR;
};

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    output.pos = float4(input.pos, 1.0f);
    output.color = input.color;
    return output;
}
)";

const char* pixelShaderSource = R"(
struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

float4 main(PS_INPUT input) : SV_TARGET {
    return input.color;
}
)";

// Quad vertex structure
struct QuadVertex
{
   float x, y, z;    // Position
   float u, v;       // UV coordinates
};

// Quad shaders
const char* quadVertexShaderSource = R"(
struct VS_INPUT {
   float3 pos : POSITION;
   float2 uv : TEXCOORD;
};

struct VS_OUTPUT {
   float4 pos : SV_POSITION;
   float2 uv : TEXCOORD;
};

VS_OUTPUT main(VS_INPUT input) {
   VS_OUTPUT output;
   output.pos = float4(input.pos, 1.0f);
   output.uv = input.uv;
   return output;
}
)";

const char* quadPixelShaderSource = R"(
Texture2D desktopTexture : register(t0);
SamplerState textureSampler : register(s0);

struct PS_INPUT {
   float4 pos : SV_POSITION;
   float2 uv : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_TARGET {
   return desktopTexture.Sample(textureSampler, input.uv);
}
)";

const char* computeShaderSource = R"(
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
    )";

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

bool InitializeTriangle()
{
    // Triangle vertices (NDC coordinates: -1 to 1)
    Vertex vertices[] = {
        { 0.0f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f}, // Top - Red
        { 0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f, 1.0f}, // Bottom Right - Green
        {-0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f, 1.0f}  // Bottom Left - Blue
    };

    // Create vertex buffer
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(vertices);
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices;

    HRESULT hr = g_Application.device->CreateBuffer(&bufferDesc, &initData, &g_Application.vertexBuffer);
    if (FAILED(hr)) return false;

    // Compile vertex shader
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    hr = D3DCompile(vertexShaderSource, strlen(vertexShaderSource), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) errorBlob->Release();
        return false;
    }

    hr = g_Application.device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_Application.vertexShader);
    if (FAILED(hr)) {
        vsBlob->Release();
        return false;
    }

    // Create input layout
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    hr = g_Application.device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_Application.inputLayout);
    vsBlob->Release();
    if (FAILED(hr)) return false;

    // Compile pixel shader
    ID3DBlob* psBlob = nullptr;
    hr = D3DCompile(pixelShaderSource, strlen(pixelShaderSource), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) errorBlob->Release();
        return false;
    }

    hr = g_Application.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_Application.pixelShader);
    psBlob->Release();
    if (FAILED(hr)) return false;

    return true;
}

// Function to initialize the compute shader blur system
HRESULT InitializeBlurComputeShader()
{
    HRESULT hr = S_OK;

    // Compile compute shader
    ID3DBlob* computeShaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    hr = D3DCompile(
        computeShaderSource,
        strlen(computeShaderSource),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "cs_5_0",
        0,
        0,
        &computeShaderBlob,
        &errorBlob
    );

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        return hr;
    }

    // Create compute shader
    hr = g_Application.device->CreateComputeShader(
        computeShaderBlob->GetBufferPointer(),
        computeShaderBlob->GetBufferSize(),
        nullptr,
        &g_Application.blurComputeShader
    );
    computeShaderBlob->Release();

    if (FAILED(hr)) return hr;

    // Create blur texture (same size as desktop texture)
    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = g_Application.windowWidth;
    textureDesc.Height = g_Application.windowHeight;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;

    hr = g_Application.device->CreateTexture2D(&textureDesc, nullptr, &g_Application.blurTexture);
    if (FAILED(hr)) return hr;

    // Create SRV for input (will use desktopSRV as input)
    // Create UAV for output
    hr = g_Application.device->CreateUnorderedAccessView(g_Application.blurTexture, nullptr, &g_Application.blurOutputUAV);
    hr = g_Application.device->CreateRenderTargetView(g_Application.blurTexture, nullptr, &g_Application.blurOutputRTV);
    if (FAILED(hr)) return hr;

    // Create SRV for the blur texture (so we can render it)
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = g_Application.device->CreateShaderResourceView(
        g_Application.blurTexture,
        &srvDesc,
        &g_Application.blurOutputSRV
    );
    if (FAILED(hr)) return hr;

    // Create constant buffer for blur parameters
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(BlurConstants);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.MiscFlags = 0;

    hr = g_Application.device->CreateBuffer(&bufferDesc, nullptr, &g_Application.blurConstantBuffer);
    if (FAILED(hr)) return hr;

    return S_OK;
}

bool InitializeDesktopCapture()
{
	// @Important -- get the 'IDXGIOutputDuplication' which allows capturing of desktop

	// Get DXGI adapter from our D3D11 device
	IDXGIDevice* dxgiDevice = nullptr;
	g_Application.device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);

	IDXGIAdapter* dxgiAdapter = nullptr;
	dxgiDevice->GetAdapter(&dxgiAdapter);

	// Get the primary output (monitor)
	IDXGIOutput* dxgiOutput = nullptr;
	dxgiAdapter->EnumOutputs(0, &dxgiOutput);

	IDXGIOutput1* dxgiOutput1 = nullptr;
	dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&dxgiOutput1);

	// Create desktop duplication
	HRESULT hr = dxgiOutput1->DuplicateOutput(g_Application.device, &g_Application.desktopDuplication);

	// Cleanup
	dxgiOutput1->Release();
	dxgiOutput->Release();
	dxgiAdapter->Release();
	dxgiDevice->Release();

	return SUCCEEDED(hr);
}

bool InitializeWindow(int width, int height)
{
    g_Application.hInstance = GetModuleHandle(nullptr);
    g_Application.windowWidth = width;
    g_Application.windowHeight = height;

    // Register window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = g_Application.hInstance;
    wc.lpszClassName = "DX11WindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = nullptr; // No background for transparency

    if (!RegisterClass(&wc))
        return false;

	auto style = WS_EX_LAYERED | WS_EX_TOPMOST; // @Important

    // Create window
    g_Application.hwnd = CreateWindowEx(
        style,
        "DX11WindowClass",
        "DirectX 11 Window",
        WS_POPUP, // @Important
        CW_USEDEFAULT, CW_USEDEFAULT,
        width, height,
        nullptr, nullptr,
        g_Application.hInstance,
        nullptr
    );

    if (!g_Application.hwnd)
        return false;

	SetLayeredWindowAttributes(g_Application.hwnd, 0, 255, LWA_ALPHA); // @Important
	SetLayeredWindowAttributes(g_Application.hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY); // @Important
	SetWindowDisplayAffinity(g_Application.hwnd, WDA_EXCLUDEFROMCAPTURE); // @Important

    return true;
}

bool InitializeDirectX()
{
    // Create device and device context
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &g_Application.device,
        nullptr,
        &g_Application.deviceContext
    );

    if (FAILED(hr))
        return false;

	// Create swap chain
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 1;
    swapChainDesc.BufferDesc.Width = g_Application.windowWidth;
    swapChainDesc.BufferDesc.Height = g_Application.windowHeight;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = g_Application.hwnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGIDevice* dxgiDevice;
    g_Application.device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);

    IDXGIAdapter* dxgiAdapter;
    dxgiDevice->GetAdapter(&dxgiAdapter);

    IDXGIFactory* dxgiFactory;
    dxgiAdapter->GetParent(__uuidof(IDXGIFactory), (void**)&dxgiFactory);

    hr = dxgiFactory->CreateSwapChain(g_Application.device, &swapChainDesc, &g_Application.swapChain);

    dxgiFactory->Release();
    dxgiAdapter->Release();
    dxgiDevice->Release();

    if (FAILED(hr))
        return false;

    // Create render target view
    ID3D11Texture2D* backBuffer;
    g_Application.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);

    hr = g_Application.device->CreateRenderTargetView(backBuffer, nullptr, &g_Application.renderTargetView);
    backBuffer->Release();

    if (FAILED(hr))
        return false;

    // Set viewport
    D3D11_VIEWPORT viewport = {};
    viewport.Width = (float)g_Application.windowWidth;
    viewport.Height = (float)g_Application.windowHeight;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;

    g_Application.deviceContext->RSSetViewports(1, &viewport);

	// Create texture for our window region
	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Width = g_Application.windowWidth;
	textureDesc.Height = g_Application.windowHeight;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

	g_Application.device->CreateTexture2D(&textureDesc, nullptr, &g_Application.desktopTexture);
	g_Application.device->CreateShaderResourceView(g_Application.desktopTexture, nullptr, &g_Application.desktopSRV);
	g_Application.device->CreateRenderTargetView(g_Application.desktopTexture, nullptr, &g_Application.desktopRTV);

	hr = g_Application.device->CreateTexture2D(&textureDesc, nullptr, &g_Application.maskTexture);
	g_Application.device->CreateShaderResourceView(g_Application.maskTexture, nullptr, &g_Application.maskSRV);
	g_Application.device->CreateRenderTargetView(g_Application.maskTexture, nullptr, &g_Application.maskRTV);

    return true;
}

bool InitializeQuad()
{
   // Full-screen quad vertices (NDC coordinates)
   QuadVertex vertices[] = {
       {-1.0f, -1.0f, 0.0f,  0.0f, 1.0f}, // Bottom Left
       {-1.0f,  1.0f, 0.0f,  0.0f, 0.0f}, // Top Left
       { 1.0f, -1.0f, 0.0f,  1.0f, 1.0f}, // Bottom Right
       { 1.0f,  1.0f, 0.0f,  1.0f, 0.0f}  // Top Right
   };

   // Create vertex buffer
   D3D11_BUFFER_DESC bufferDesc = {};
   bufferDesc.Usage = D3D11_USAGE_DEFAULT;
   bufferDesc.ByteWidth = sizeof(vertices);
   bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

   D3D11_SUBRESOURCE_DATA initData = {};
   initData.pSysMem = vertices;

   HRESULT hr = g_Application.device->CreateBuffer(&bufferDesc, &initData, &g_Application.quadVertexBuffer);
   if (FAILED(hr)) return false;

   // Compile vertex shader
   ID3DBlob* vsBlob = nullptr;
   hr = D3DCompile(quadVertexShaderSource, strlen(quadVertexShaderSource), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, nullptr);
   if (FAILED(hr)) return false;

   hr = g_Application.device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_Application.quadVertexShader);
   if (FAILED(hr)) {
       vsBlob->Release();
       return false;
   }

   // Create input layout
   D3D11_INPUT_ELEMENT_DESC layout[] = {
       {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
       {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
   };

   hr = g_Application.device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_Application.quadInputLayout);
   vsBlob->Release();
   if (FAILED(hr)) return false;

   // Compile pixel shader
   ID3DBlob* psBlob = nullptr;
   hr = D3DCompile(quadPixelShaderSource, strlen(quadPixelShaderSource), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, nullptr);
   if (FAILED(hr)) return false;

   hr = g_Application.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_Application.quadPixelShader);
   psBlob->Release();
   if (FAILED(hr)) return false;

   // Create sampler state
   D3D11_SAMPLER_DESC samplerDesc = {};
   samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
   samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
   samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
   samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
   samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
   samplerDesc.MinLOD = 0;
   samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

   hr = g_Application.device->CreateSamplerState(&samplerDesc, &g_Application.samplerState);
   if (FAILED(hr)) return false;

   return true;
}

bool GrabDesktopBehindWindow()
{
	// Get current frame from desktop duplication
   IDXGIResource* desktopResource = nullptr;
   DXGI_OUTDUPL_FRAME_INFO frameInfo;

   // @Important
   HRESULT hr = g_Application.desktopDuplication->AcquireNextFrame(0, &frameInfo, &desktopResource);
   if (hr == DXGI_ERROR_WAIT_TIMEOUT)
   {
	   hr = g_Application.desktopDuplication->AcquireNextFrame(1, &frameInfo, &desktopResource);
   }

   if (FAILED(hr))
   {
	   if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false; // No new frame
       if (hr == DXGI_ERROR_ACCESS_LOST)
       {
           // Desktop duplication lost, need to recreate
           g_Application.desktopDuplication->Release();
           g_Application.desktopDuplication = nullptr;
           InitializeDesktopCapture();
           return false;
       }
       return false;
   }

   if (frameInfo.LastPresentTime.HighPart == 0) {
	   // not interested in just mouse updates, which can happen much faster than 60fps if you really shake the mouse
       //g_Application.desktopDuplication->ReleaseFrame();
	   //return false;
   }

   // Get the desktop texture
   ID3D11Texture2D* acquiredDesktopImage = nullptr;
   desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&acquiredDesktopImage);

   // Get window position on screen
   RECT windowRect;
   GetWindowRect(g_Application.hwnd, &windowRect);

   // Copy the region behind our window
   D3D11_BOX sourceBox;
   sourceBox.left = windowRect.left;
   sourceBox.top = windowRect.top;
   sourceBox.right = windowRect.right;
   sourceBox.bottom = windowRect.bottom;
   sourceBox.front = 0;
   sourceBox.back = 1;

   // @Important
   g_Application.deviceContext->CopySubresourceRegion(
       g_Application.desktopTexture,
       0, 0, 0, 0,
       acquiredDesktopImage,
       0,
       &sourceBox
   );
   g_Application.deviceContext->Flush();

   // Cleanup
   acquiredDesktopImage->Release();
   desktopResource->Release();
   g_Application.desktopDuplication->ReleaseFrame();

   return true;
}

void ApplyBlurEffect(float blurRadius = 13.0f)
{
    if (!g_Application.blurComputeShader || !g_Application.desktopSRV)
        return;

    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = g_Application.deviceContext->Map(
        g_Application.blurConstantBuffer,
        0,
        D3D11_MAP_WRITE_DISCARD,
        0,
        &mappedResource
    );

    if (SUCCEEDED(hr))
    {
        BlurConstants* constants = (BlurConstants*)mappedResource.pData;
        constants->textureWidth = g_Application.windowWidth;
        constants->textureHeight = g_Application.windowHeight;
        constants->blurRadius = blurRadius;
        constants->padding = 0.0f;

        g_Application.deviceContext->Unmap(g_Application.blurConstantBuffer, 0);
    }

	static ID3D11UnorderedAccessView* const NullUAV[] = { nullptr, nullptr,  nullptr, nullptr };
	static ID3D11ShaderResourceView* const NullSRV[] = { nullptr, nullptr,  nullptr, nullptr };
	static ID3D11RenderTargetView* const NullRTV[] = { nullptr, nullptr,  nullptr, nullptr };
	static ID3D11Buffer* const NullBuffer[] = { nullptr, nullptr,  nullptr, nullptr };
	g_Application.deviceContext->PSSetShaderResources(0, 3, &NullSRV[0]);
	g_Application.deviceContext->OMSetRenderTargets(0, nullptr, nullptr);

    // Set compute shader and resources
    g_Application.deviceContext->CSSetShader(g_Application.blurComputeShader, nullptr, 0);
    g_Application.deviceContext->CSSetConstantBuffers(0, 1, &g_Application.blurConstantBuffer);

	ID3D11ShaderResourceView* srvs[2] = { g_Application.desktopSRV, g_Application.maskSRV };
    g_Application.deviceContext->CSSetShaderResources(0, 2, srvs);

    g_Application.deviceContext->CSSetUnorderedAccessViews(0, 1, &g_Application.blurOutputUAV, nullptr);

    // Dispatch compute shader
    UINT dispatchX = (g_Application.windowWidth + 7) / 8;  // 8x8 thread groups
    UINT dispatchY = (g_Application.windowHeight + 7) / 8;
    g_Application.deviceContext->Dispatch(dispatchX, dispatchY, 1);

    // Unbind resources
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    g_Application.deviceContext->CSSetUnorderedAccessViews(0, 3, &NullUAV[0], nullptr);
    g_Application.deviceContext->CSSetShaderResources(0, 3, &NullSRV[0]);
    g_Application.deviceContext->CSSetShader(nullptr, nullptr, 0);
}

void RenderDesktopQuad()
{
   // Set vertex buffer
	UINT stride = sizeof(QuadVertex);
	UINT offset = 0;
	g_Application.deviceContext->IASetVertexBuffers(0, 1, &g_Application.quadVertexBuffer, &stride, &offset);

	// Set primitive topology
	g_Application.deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	// Set input layout
	g_Application.deviceContext->IASetInputLayout(g_Application.quadInputLayout);

	// Set shaders
	g_Application.deviceContext->VSSetShader(g_Application.quadVertexShader, nullptr, 0);
	g_Application.deviceContext->PSSetShader(g_Application.quadPixelShader, nullptr, 0);

	// Set texture and sampler
	g_Application.deviceContext->PSSetShaderResources(0, 1, &g_Application.desktopSRV);
	g_Application.deviceContext->PSSetSamplers(0, 1, &g_Application.samplerState);

	// Draw quad
	g_Application.deviceContext->Draw(4, 0);
}

void RenderBlurQuad()
{
   // Set vertex buffer
	UINT stride = sizeof(QuadVertex);
	UINT offset = 0;
	g_Application.deviceContext->IASetVertexBuffers(0, 1, &g_Application.quadVertexBuffer, &stride, &offset);

	// Set primitive topology
	g_Application.deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	// Set input layout
	g_Application.deviceContext->IASetInputLayout(g_Application.quadInputLayout);

	// Set shaders
	g_Application.deviceContext->VSSetShader(g_Application.quadVertexShader, nullptr, 0);
	g_Application.deviceContext->PSSetShader(g_Application.quadPixelShader, nullptr, 0);

	// Set texture and sampler
	g_Application.deviceContext->PSSetShaderResources(0, 1, &g_Application.blurOutputSRV);
	g_Application.deviceContext->PSSetSamplers(0, 1, &g_Application.samplerState);

	// Draw quad
	g_Application.deviceContext->Draw(4, 0);
}

void RenderTriangle()
{
    // Set vertex buffer
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    g_Application.deviceContext->IASetVertexBuffers(0, 1, &g_Application.vertexBuffer, &stride, &offset);

    // Set primitive topology
    g_Application.deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Set input layout
    g_Application.deviceContext->IASetInputLayout(g_Application.inputLayout);

    // Set shaders
    g_Application.deviceContext->VSSetShader(g_Application.vertexShader, nullptr, 0);
    g_Application.deviceContext->PSSetShader(g_Application.pixelShader, nullptr, 0);

    // Draw triangle
    g_Application.deviceContext->Draw(3, 0);
}

void Render()
{
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_Application.deviceContext->ClearRenderTargetView(g_Application.renderTargetView, clearColor);
	g_Application.deviceContext->ClearRenderTargetView(g_Application.desktopRTV, clearColor);
	g_Application.deviceContext->ClearRenderTargetView(g_Application.blurOutputRTV, clearColor);
	g_Application.deviceContext->ClearRenderTargetView(g_Application.maskRTV, clearColor);

	g_Application.deviceContext->OMSetRenderTargets(1, &g_Application.maskRTV, nullptr);
	RenderTriangle();

	g_Application.deviceContext->OMSetRenderTargets(1, &g_Application.renderTargetView, nullptr);
    GrabDesktopBehindWindow();

	ApplyBlurEffect();

	g_Application.deviceContext->OMSetRenderTargets(1, &g_Application.renderTargetView, nullptr);
	RenderBlurQuad();
	// RenderTriangle();

	// Present the frame
    g_Application.swapChain->Present(1, 0);
}

void Cleanup()
{
    if (g_Application.renderTargetView)
    {
        g_Application.renderTargetView->Release();
        g_Application.renderTargetView = nullptr;
    }

    if (g_Application.swapChain)
    {
        g_Application.swapChain->Release();
        g_Application.swapChain = nullptr;
    }

    if (g_Application.deviceContext)
    {
        g_Application.deviceContext->Release();
        g_Application.deviceContext = nullptr;
    }

    if (g_Application.device)
    {
        g_Application.device->Release();
        g_Application.device = nullptr;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
	  case WM_DESTROY:
	  {
		  g_Application.isRunning = false;
		  PostQuitMessage(0);
		  return 0;
	  }

	  case WM_NCHITTEST:
	  {
		  // Allow dragging the borderless window
		  LRESULT hit = DefWindowProc(hwnd, uMsg, wParam, lParam);
		  if (hit == HTCLIENT) hit = HTCAPTION;
		  return hit;
	  }

	  case WM_SIZE:
	  {
		  if (g_Application.swapChain)
		  {
			  g_Application.windowWidth = LOWORD(lParam);
			  g_Application.windowHeight = HIWORD(lParam);

			  // Release render target view
			  if (g_Application.renderTargetView)
			  {
				  g_Application.renderTargetView->Release();
				  g_Application.renderTargetView = nullptr;
			  }

			  // Resize swap chain buffers
			  g_Application.swapChain->ResizeBuffers(0, g_Application.windowWidth, g_Application.windowHeight, DXGI_FORMAT_UNKNOWN, 0);

			  // Recreate render target view
			  ID3D11Texture2D* backBuffer;
			  g_Application.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
			  g_Application.device->CreateRenderTargetView(backBuffer, nullptr, &g_Application.renderTargetView);
			  backBuffer->Release();
		  }
		  return 0;
	  }

	  case WM_MOVE:
	  {
		  if (g_Application.swapChain)
		  {
			  Render();
			  g_Application.swapChain->Present(1, 0);
		  }

		  return 0;
	  }
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Initialize window
    if (!InitializeWindow(800, 600))
    {
        MessageBox(nullptr, "Failed to create window", "Error", MB_OK);
        return -1;
    }

    // Initialize DirectX
    if (!InitializeDirectX())
    {
        MessageBox(nullptr, "Failed to initialize DirectX", "Error", MB_OK);
        Cleanup();
        return -1;
    }

	InitializeTriangle();
	InitializeDesktopCapture();
	InitializeQuad();
	InitializeBlurComputeShader();

    g_Application.isRunning = true;

	g_Application.swapChain->Present(1, 0);
	g_Application.swapChain->Present(1, 0);
	g_Application.swapChain->Present(1, 0);

	ShowWindow(g_Application.hwnd, SW_SHOW);
	UpdateWindow(g_Application.hwnd);

    // Main loop
    MSG msg = {};
    while (g_Application.isRunning)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

		if (!g_Application.isRunning)
        {
			break;
        }

		Render();

    }

    Cleanup();
    return 0;
}
