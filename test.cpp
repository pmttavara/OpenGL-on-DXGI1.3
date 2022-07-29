
#undef UNICODE
#include <intrin.h>
#include <Windows.h>
#include <stdio.h>

#include <comdef.h>
#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <gl/GL.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "opengl32.lib")

#include <dxgidebug.h> // @Remove?
#pragma comment(lib, "dxguid.lib") // @Remove?

#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#include "glext.h"
#include "wglext.h"

// There's some Assert()s on expressions with side-effects on purpose
#define Assert(...) do { if (!(__VA_ARGS__)) { printf("Assert FAIL:\n    \"" #__VA_ARGS__ "\"\n\n"); __debugbreak(); } } while (0);

extern "C" {
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
    __declspec(dllexport) DWORD NvOptimusEnablement = 1;
}

#include <stdlib.h> // @Remove
#include <math.h> // @Remove

bool resize = false;
bool alt_enter = false;
HWND stats = nullptr;
static LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZING:
        if (window == stats) {
            RECT &r = *(RECT *)lparam;
            // This is kind of a huge hack, but it's simpler than figuring out how to efficiently
            // render text to the dxgi surface :)
            if (r.bottom > r.top) {
                r.bottom = r.top;
            }
            return TRUE;
        }
        break;
    case WM_SYSKEYDOWN:
        if ((wparam & 0xffff) == VK_RETURN && ((lparam >> 29) & 1) == 1) {
            alt_enter = true;
            return 0;
        }
        // TODO(Phillip): I need to investigate why alt-enter is beeping.
        //                Seemingly MakeWindowAssociation() is not working on
        //                the swapchain, since it's outputting a debug string
        //                complaining that it can't SetFullscreenState on the
        //                frame latency waitable swapchain. :AltEnterBeep
        break;
    case WM_SIZE:
        resize = true;
        return 0;
    }
    return DefWindowProcA(window, msg, wparam, lparam);
}

bool must_succeed_(HRESULT hr) {
    if (hr < 0) {
        _com_error err(hr);
        printf("COM Error (HRESULT %d): %s\n", hr, err.ErrorMessage());
        return false;
    }
    return true;
}
#define must_succeed(e) Assert(must_succeed_(e))
static void APIENTRY DebugCallback(GLenum source, GLenum type, GLuint id,
    GLenum severity, GLsizei length, const GLchar *message, const void *userParam)
{
    printf("GL Debug: %s\n", message);
    if (/*severity == GL_DEBUG_SEVERITY_MEDIUM ||*/ severity == GL_DEBUG_SEVERITY_HIGH) {
        fflush(stdout);
        Assert(0);
    }
}
static bool gl_success() {
#ifdef NDEBUG
    return true;
#else
    bool success = true;
    while (int err = glGetError()) {
        success = false;
        printf("glGetError(): ");
        switch (err) {
        case GL_INVALID_ENUM:      printf("GL_INVALID_ENUM");      break;
        case GL_INVALID_VALUE:     printf("GL_INVALID_VALUE");     break;
        case GL_INVALID_OPERATION: printf("GL_INVALID_OPERATION"); break;
        case GL_STACK_OVERFLOW:    printf("GL_STACK_OVERFLOW");    break;
        case GL_STACK_UNDERFLOW:   printf("GL_STACK_UNDERFLOW");   break;
        case GL_OUT_OF_MEMORY:     printf("GL_OUT_OF_MEMORY");     break;
        }
        printf("\n");
    }
    return success;
#endif
}

static void rgba_to_clipboard(void *data, int w, int h) {
    //Windows wants flipped, but OpenGL is already flipped so we're fine.
    //Windows suppports RGBA in theory but many programs want BGRA in practice so we swap manually. >_>
    BITMAPV5HEADER header = {};
    header.bV5Size = sizeof(header);
    header.bV5Width = w;
    header.bV5Height = h;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00ff0000;
    header.bV5GreenMask = 0x0000ff00;
    header.bV5BlueMask = 0x000000ff;
    header.bV5AlphaMask = 0xff000000;
    header.bV5CSType = LCS_WINDOWS_COLOR_SPACE; // required for alpha support
    auto global = GlobalAlloc(GMEM_MOVEABLE, sizeof(header) + w * h * 4);
    if (!global) return;
    char *buffer = (char *) GlobalLock(global);
    if (!buffer) return;
    memcpy(buffer, &header, sizeof(header));
    memcpy(buffer + sizeof(header), data, w * h * 4);
    struct RGBA { char rgba[4]; };
    RGBA *dst = (RGBA *) (buffer + sizeof(header));
    RGBA *end = dst + w * h;
    for (RGBA *p = dst; p < end; p++) { auto tmp = p->rgba[0]; p->rgba[0] = p->rgba[2]; p->rgba[2] = tmp; } //rgba->bgra
    GlobalUnlock(global);
    if (!OpenClipboard(NULL)) return;
    EmptyClipboard();
    SetClipboardData(CF_DIBV5, global);
    CloseClipboard();
}

static double get_time() {
    static double invfreq;
    LARGE_INTEGER li = {};
    if (!invfreq) {
        Assert(QueryPerformanceFrequency(&li));
        invfreq = 1.0 / li.QuadPart;
    }
    Assert(QueryPerformanceCounter(&li));
    return li.QuadPart * invfreq;
}

static bool setup_process_dpi_awareness() {
    bool             result                 = false;
    HINSTANCE        user32                 = LoadLibraryA("User32.dll");
    HINSTANCE        shcore                 = LoadLibraryA("Shcore.dll");
    BOOL    (WINAPI *spdac     )(long long) = NULL;
    HRESULT (WINAPI *spda      )(      int) = NULL;
    if (user32)           { spdac = (BOOL    (WINAPI *)(long long))GetProcAddress(user32, "SetProcessDpiAwarenessContext"); }
    if (shcore)           { spda  = (HRESULT (WINAPI *)(      int))GetProcAddress(shcore, "SetProcessDpiAwareness"       ); }
    if (!result && spdac) { result = spdac(-4) == TRUE   ; } // SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
    if (!result && spdac) { result = spdac(-3) == TRUE   ; } // SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)
    if (!result && spdac) { result = spdac(-2) == TRUE   ; } // SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE)
    if (!result && spda ) { result = spda (+2) == S_OK   ; } // SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)
    if (!result && spda ) { result = spda (+1) == S_OK   ; } // SetProcessDpiAwareness(PROCESS_SYSTEM_AWARE)
    if (!result)          { result = SetProcessDPIAware(); }
    if (user32)           { FreeLibrary(user32); }
    if (shcore)           { FreeLibrary(shcore); }
    return result;
}

static void update_stats(bool vsync, bool epilepsy, float dt, float frame_latency) {
    char title[512];
    int n = snprintf(title,
        sizeof(title),
        "%.0f FPS (%.1f ms) | >=%.0f frame latency | %s (press V) | [EPILEPSY WARNING] %s (press E) | F11 for fullscreen",
        1.0f / dt,
        dt * 1000,
        frame_latency,
        vsync ? "Vsync" : "No vsync",
        epilepsy ? "Flicker" : "No flicker");
    Assert(n);
    Assert(stats);
    SetWindowTextA(stats, title);
}

int main() {

    setup_process_dpi_awareness();

    unsigned int frame_count = 0;
    bool fullscreen = false;
    bool vsync = true;
    bool epilepsy = false;
    WINDOWPLACEMENT previous_window_placement = {sizeof(previous_window_placement)};

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = "DXGL";
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);

    ATOM atom = RegisterClassA(&wc);
    Assert(atom);

    HWND hwnd = CreateWindowA(wc.lpszClassName, "OpenGL on DXGI 1.3 - https://github.com/pmttavara/OpenGL-on-DXGI1.3",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        640, 480, nullptr, nullptr, nullptr, nullptr);

    stats = CreateWindowA(wc.lpszClassName, "",
        (WS_OVERLAPPEDWINDOW | WS_VISIBLE) & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
        800, 0, nullptr, nullptr, nullptr, nullptr);

    update_stats(vsync, epilepsy, 0, 0);

    HWND gl_hwnd = nullptr;
    {
#if 0
        // Use a separate window for establishing GL context. This
        // seems to break for some uninvestigated reason if you
        // minimize and then restore the main window. Strange!
        gl_hwnd = CreateWindowA("STATIC", "temp", 0,
            CW_USEDEFAULT, CW_USEDEFAULT, 1, 1,
            nullptr, nullptr, nullptr, nullptr);
#else
        gl_hwnd = hwnd;
#endif
        Assert(gl_hwnd);
        auto tempdc = GetDC(gl_hwnd);
        Assert(tempdc);
        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_SUPPORT_OPENGL;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.iLayerType = PFD_MAIN_PLANE;
        int format = ChoosePixelFormat(tempdc, &pfd);
        Assert(format);
        DescribePixelFormat(tempdc, format, sizeof(pfd), &pfd);
        BOOL set = SetPixelFormat(tempdc, format, &pfd);
        Assert(set);
        auto temprc = wglCreateContext(tempdc);
        Assert(temprc);
        BOOL make = wglMakeCurrent(tempdc, temprc);
        Assert(make);
        int attrib0[] = {WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB, 0};
        float fattrib0[] = { 0 };
        unsigned int matching = 0;
        auto wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
        Assert(wglChoosePixelFormatARB);
        Assert(wglChoosePixelFormatARB(tempdc, attrib0, fattrib0, 1, &format, &matching));
        auto wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
        int attrib[] =
        {
            WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB, 0,
        };
        HGLRC newrc = wglCreateContextAttribsARB(tempdc, NULL, attrib);
        Assert(newrc);
        make = wglMakeCurrent(tempdc, newrc);
        Assert(make);
        wglDeleteContext(temprc);
        temprc = newrc;
#ifndef NDEBUG
        auto glDebugMessageCallback = (PFNGLDEBUGMESSAGECALLBACKPROC )wglGetProcAddress("glDebugMessageCallback");
        glDebugMessageCallback(DebugCallback, 0);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
#endif
    }
    //initted once
#ifndef NDEBUG
    IDXGIDebug1 *dxgi_debug = nullptr; //DEBUG
#endif
    ID3D11Device1 *device = nullptr;
    HANDLE device_gldx = nullptr;
    ID3D11DeviceContext1 *context = nullptr;
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    IDXGISwapChain2 *swapchain = nullptr;
    HANDLE waitable_object = nullptr;
    ID3D11BlendState *blend_state = nullptr;
    ID3D11VertexShader *vertex_shader = nullptr;
    ID3D11PixelShader *pixel_shader = nullptr;
    ID3D11SamplerState *sampler_state = nullptr;
    ID3D11InputLayout *vertex_layout = nullptr;
#ifndef NDEBUG
    must_succeed(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug))); //DEBUG
#endif
    {
        ID3D11Device *base_device = nullptr;
        ID3D11DeviceContext *base_context = nullptr;
#ifdef NDEBUG
        int flags = 0;
#else
        int flags = D3D11_CREATE_DEVICE_DEBUG;
#endif
        must_succeed(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &base_device, nullptr, &base_context));
        must_succeed(base_device->QueryInterface(IID_PPV_ARGS(&device)));
        must_succeed(base_context->QueryInterface(IID_PPV_ARGS(&context)));
        base_context->Release();
        base_device->Release();
    }
    {
        IDXGIDevice1 *dxgi_device = nullptr;
        must_succeed(device->QueryInterface(IID_PPV_ARGS(&dxgi_device)));
        must_succeed(dxgi_device->SetMaximumFrameLatency(1));
        dxgi_device->Release();
    }
#ifndef NDEBUG
    { //DEBUG
        ID3D11InfoQueue *info_queue = nullptr;
        must_succeed(device->QueryInterface(IID_PPV_ARGS(&info_queue)));
        info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
        info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
        // info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, true);
        info_queue->Release();
    }
#endif
    {
        IDXGIFactory2 *factory = nullptr;
        IDXGISwapChain1 *base_swapchain = nullptr;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER;
        desc.BufferCount = 2;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        desc.Scaling = DXGI_SCALING_NONE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        UINT create_dxgi_factory_flags = 0;
#ifndef NDEBUG
        { //DEBUG
            create_dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
        }
#endif
        must_succeed(CreateDXGIFactory2(create_dxgi_factory_flags, IID_IDXGIFactory2, (void**)&factory));
        //NOTE: if you fail to create the swap chain, you likely don't
        //      want to fall back to older presentation models, since AFAICT
        //      they confer no latency advantages over WGL's legacy swapchain.
        must_succeed(factory->CreateSwapChainForHwnd(device, hwnd, &desc, nullptr, nullptr, &base_swapchain));
        must_succeed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_PRINT_SCREEN | DXGI_MWA_NO_WINDOW_CHANGES)); // :AltEnterBeep
        must_succeed(base_swapchain->QueryInterface(&swapchain));
        Assert(waitable_object = swapchain->GetFrameLatencyWaitableObject());
        base_swapchain->Release();
        factory->Release();
    }
    {
        D3D11_BLEND_DESC blend_desc = {};
        blend_desc.AlphaToCoverageEnable = FALSE;
        blend_desc.RenderTarget[0].BlendEnable = FALSE;
        blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
        blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        must_succeed(device->CreateBlendState(&blend_desc, &blend_state));
    }
    {
        static const char vs_source[] = R"(
struct VertexShaderOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
struct VertexShaderInput {
    uint vertex : SV_VERTEXID;
};
VertexShaderOutput main(VertexShaderInput input) {
    VertexShaderOutput output;
    output.uv = float2(input.vertex & 1, input.vertex >> 1);
    output.position = float4((output.uv.x - 0.5f) * 2, -(output.uv.y - 0.5f) * 2, 0.0f, 1.0f);
    output.uv.y = 1.0f - output.uv.y;
    return output;
}
)";
        ID3DBlob *vs_blob = nullptr;
        ID3DBlob *errors = nullptr;
        HRESULT compile_result = D3DCompile(vs_source, sizeof(vs_source), "gldx_vs.fx", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_4_0", 0, 0, &vs_blob, &errors);
        if (errors) printf("D3DCompile Errors:\n%s\n", (char *)errors->GetBufferPointer());
        Assert(compile_result == S_OK && vs_blob);
        void *bytecode = vs_blob->GetBufferPointer();
        size_t len = vs_blob->GetBufferSize();
        Assert(bytecode && len);
        must_succeed(device->CreateVertexShader(bytecode, len, nullptr, &vertex_shader));
        D3D11_INPUT_ELEMENT_DESC local_layout[] = {{"", 0, DXGI_FORMAT_R32_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        must_succeed(device->CreateInputLayout(local_layout, 1, bytecode, len, &vertex_layout));
        vs_blob->Release();
    }
    {
        static const char ps_source[] = R"(
#pragma warning(disable : 3571)
struct VertexShaderOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
sampler smp : register(s0);
Texture2D staging_color_renderbuffer : register(t0);
float4 main(VertexShaderOutput input) : SV_TARGET {
    // if (((int(input.position.x) ^ int(input.position.y)) & 1) == 0) discard;
    return staging_color_renderbuffer.Sample(smp, input.uv);
}
)";
        ID3DBlob *ps_blob = nullptr;
        ID3DBlob *errors = nullptr;
        HRESULT compile_result = D3DCompile(ps_source, sizeof(ps_source), "gldx_ps.fx", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_4_0", 0, 0, &ps_blob, &errors);
        if (errors) printf("D3DCompile Errors:\n%s\n", (char *)errors->GetBufferPointer());
        Assert(compile_result == S_OK && ps_blob);
        void *bytecode = ps_blob->GetBufferPointer();
        size_t len = ps_blob->GetBufferSize();
        Assert(bytecode && len);
        must_succeed(device->CreatePixelShader(bytecode, len, nullptr, &pixel_shader));
        ps_blob->Release();
    }
    {
        D3D11_SAMPLER_DESC sampler_desc = {};
        sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_desc.MaxAnisotropy = 1;
        sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler_desc.MinLOD = -D3D11_FLOAT32_MAX;
        sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
        must_succeed(device->CreateSamplerState(&sampler_desc, &sampler_state));
    }
    PFNWGLDXOPENDEVICENVPROC wglDXOpenDeviceNV;
    PFNWGLDXCLOSEDEVICENVPROC wglDXCloseDeviceNV;
    PFNWGLDXREGISTEROBJECTNVPROC wglDXRegisterObjectNV;
    PFNWGLDXUNREGISTEROBJECTNVPROC wglDXUnregisterObjectNV;
    PFNWGLDXLOCKOBJECTSNVPROC wglDXLockObjectsNV;
    PFNWGLDXUNLOCKOBJECTSNVPROC wglDXUnlockObjectsNV;
    PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
    PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
    PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers;
    PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers;
    PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus;
    PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer;
    Assert(*(void**)&wglDXOpenDeviceNV         = wglGetProcAddress("wglDXOpenDeviceNV"));
    Assert(*(void**)&wglDXCloseDeviceNV        = wglGetProcAddress("wglDXCloseDeviceNV"));
    Assert(*(void**)&wglDXRegisterObjectNV     = wglGetProcAddress("wglDXRegisterObjectNV"));
    Assert(*(void**)&wglDXUnregisterObjectNV   = wglGetProcAddress("wglDXUnregisterObjectNV"));
    Assert(*(void**)&wglDXLockObjectsNV        = wglGetProcAddress("wglDXLockObjectsNV"));
    Assert(*(void**)&wglDXUnlockObjectsNV      = wglGetProcAddress("wglDXUnlockObjectsNV"));
    Assert(*(void**)&glGenFramebuffers         = wglGetProcAddress("glGenFramebuffers"));
    Assert(*(void**)&glDeleteFramebuffers      = wglGetProcAddress("glDeleteFramebuffers"));
    Assert(*(void**)&glGenRenderbuffers        = wglGetProcAddress("glGenRenderbuffers"));
    Assert(*(void**)&glDeleteRenderbuffers     = wglGetProcAddress("glDeleteRenderbuffers"));
    Assert(*(void**)&glBindFramebuffer         = wglGetProcAddress("glBindFramebuffer"));
    Assert(*(void**)&glFramebufferRenderbuffer = wglGetProcAddress("glFramebufferRenderbuffer"));
    Assert(*(void**)&glCheckFramebufferStatus  = wglGetProcAddress("glCheckFramebufferStatus"));
    Assert(*(void**)&glBlitFramebuffer         = wglGetProcAddress("glBlitFramebuffer"));
    Assert(!!(device_gldx = wglDXOpenDeviceNV(device)) & gl_success());
    //reinitted every resize
    ID3D11RenderTargetView *rtv = nullptr;
    ID3D11Texture2D *staging_color_renderbuffer = nullptr;
    ID3D11ShaderResourceView *staging_color_renderbuffer_view = nullptr;
    GLuint staging_color_renderbuffer_gl = 0;
    HANDLE staging_color_renderbuffer_gldx = 0;
    GLuint staging_framebuffer_gl = 0;

    resize = true;
    double prev = get_time();
    for (;;) {
        {
            // See comment before Present().
            // if (vsync && frame_count > 0) {
            //     DwmFlush();
            // }
            // printf("Waiting on frame latency waitable object...\n");
            DWORD wait_result = WaitForSingleObjectEx(waitable_object, 1000, true);
            if (wait_result == WAIT_FAILED) {
                printf("WARNING: Waiting on the swapchain failed!!!\n");
                Assert(false); // what do we do?
            } else if (wait_result == WAIT_TIMEOUT) {
                printf("WARNING: Waiting on the swapchain exceeded 1 second!!!\n");
            } else {
                Assert(wait_result == WAIT_OBJECT_0);
            }
        }
        // printf("\n===== START OF FRAME %d =====\n", frame_count);
        float frame_latency = 0;
        if (vsync) {
            DXGI_FRAME_STATISTICS stats = {};
            HRESULT gfs_result = swapchain->GetFrameStatistics(&stats);
            if (gfs_result != DXGI_ERROR_FRAME_STATISTICS_DISJOINT) {
                Assert(gfs_result == S_OK);
                // SyncQPCTime increments while the computer is asleep, but SyncRefreshCount doesn't.
                // If you use those values to compute refresh rate, you should subtract off some base number
                // off SyncRefreshCount and SyncQPCTime based on the start of the program.
                // And you have to refresh that base number every time the computer wakes up from boot!
                DWM_TIMING_INFO timing = {sizeof(timing)};
                if (DwmGetCompositionTimingInfo(nullptr, &timing) == S_OK) {
                    if (stats.SyncQPCTime.QuadPart && stats.SyncRefreshCount) {
                        signed long long syncDelay = timing.qpcVBlank - stats.SyncQPCTime.QuadPart;
                        frame_latency = (float)(syncDelay / (double)timing.qpcRefreshPeriod);
                    }
                }
            }
        }
        double next = get_time();
        float dt = (float)(next - prev);
        prev = next;

        bool quit = false;
        bool screenshot = false;
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
                break;
            } else if (msg.message == WM_KEYDOWN) {
                if (msg.wParam == VK_F11) {
                    alt_enter = true;
                } else if (msg.wParam == 'V') {
                    vsync ^= 1;
                } else if (msg.wParam == 'E') {
                    epilepsy ^= 1;
                } else if (msg.wParam == VK_F5) {
                    screenshot = true;
                }
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (quit) break;

        if (alt_enter) {
            alt_enter = false;
            resize = true;
            fullscreen = !fullscreen;
            if (fullscreen) {
                MONITORINFO mi = {sizeof(mi)};
                Assert(GetWindowPlacement(hwnd, &previous_window_placement));
                Assert(GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi));
                Assert(SetWindowLongA(hwnd, GWL_STYLE, WS_VISIBLE));
                Assert(SetWindowPos(hwnd, HWND_NOTOPMOST, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED));
            } else {
                Assert(SetWindowLongA(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW));
                Assert(SetWindowPlacement(hwnd, &previous_window_placement));
                Assert(SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED));
            }
        }

        // Update game sim: let's fake some work
        // Sleep((DWORD)((sin(get_time())*.5+.5) * 6));

        if (resize) {
            resize = false;
            if (rtv) {
                glFinish();
                Assert(device_gldx && staging_color_renderbuffer_gldx);
                Assert(wglDXUnregisterObjectNV(device_gldx, staging_color_renderbuffer_gldx) & gl_success());
                glDeleteFramebuffers(1, &staging_framebuffer_gl); Assert(gl_success());
                glDeleteRenderbuffers(1, &staging_color_renderbuffer_gl); Assert(gl_success());
                staging_color_renderbuffer_view->Release();
                staging_color_renderbuffer->Release();
                rtv->Release();
#ifndef NDEBUG
                must_succeed(dxgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL));
#endif
            }
            must_succeed(swapchain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, desc.Flags));
            must_succeed(swapchain->GetDesc1(&desc)); //call AFTER ResizeBuffers to get the right W+H!!
            //NOTE: if you use a separate fake-window for getting GL, then you need to do this to make GL's FBO resize!!!!!
            if (gl_hwnd != hwnd) {
                RECT rect = {};
                Assert(GetWindowRect(hwnd, &rect));
                LONG style = GetWindowLongA(hwnd, GWL_STYLE);
                Assert(style);
                Assert(SetWindowLongA(gl_hwnd, GWL_STYLE, style & ~WS_VISIBLE));
                Assert(SetWindowPos(gl_hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW | SWP_NOZORDER | SWP_HIDEWINDOW | SWP_FRAMECHANGED));
            }
            {
                ID3D11Texture2D *rtv_texture = nullptr;
                must_succeed(swapchain->GetBuffer(0, IID_PPV_ARGS(&rtv_texture)));
                must_succeed(device->CreateRenderTargetView(rtv_texture, nullptr, &rtv));
                D3D11_TEXTURE2D_DESC staging_color_renderbuffer_desc = {};
                rtv_texture->GetDesc(&staging_color_renderbuffer_desc);
                staging_color_renderbuffer_desc.ArraySize = 1;
                staging_color_renderbuffer_desc.MipLevels = 1;
                staging_color_renderbuffer_desc.SampleDesc.Count = 1;
                staging_color_renderbuffer_desc.SampleDesc.Quality = 0;
                staging_color_renderbuffer_desc.Usage = D3D11_USAGE_DEFAULT;
                staging_color_renderbuffer_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                staging_color_renderbuffer_desc.CPUAccessFlags = 0;
                staging_color_renderbuffer_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
                must_succeed(device->CreateTexture2D(&staging_color_renderbuffer_desc, nullptr, &staging_color_renderbuffer));
                must_succeed(device->CreateShaderResourceView(staging_color_renderbuffer, nullptr, &staging_color_renderbuffer_view));
                glGenRenderbuffers(1, &staging_color_renderbuffer_gl); Assert(gl_success());
                glGenFramebuffers(1, &staging_framebuffer_gl); Assert(gl_success());
                Assert(!!(staging_color_renderbuffer_gldx = wglDXRegisterObjectNV(device_gldx, staging_color_renderbuffer, staging_color_renderbuffer_gl, GL_RENDERBUFFER, WGL_ACCESS_WRITE_DISCARD_NV)) & gl_success());
                rtv_texture->Release();
            }
            context->ClearState();
            D3D11_VIEWPORT view = {};
            view.Width = (float)desc.Width;
            view.Height = (float)desc.Height;
            view.MaxDepth = 1;
            context->RSSetViewports(1, &view);
            context->PSSetShaderResources(0, 1, &staging_color_renderbuffer_view);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            ID3D11Buffer *ia_vertex_buffer = nullptr;
            UINT ia_vertex_buffer_stride = 0;
            UINT ia_vertex_buffer_offset = 0;
            context->IASetVertexBuffers(0, 1, &ia_vertex_buffer, &ia_vertex_buffer_stride, &ia_vertex_buffer_offset);
            context->IASetInputLayout(vertex_layout);
            context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
            static const FLOAT blend_factor[4] = {};
            context->OMSetBlendState(blend_state, blend_factor, 0xffffffff);
            context->VSSetShader(vertex_shader, nullptr, 0);
            context->PSSetShader(pixel_shader, nullptr, 0);
            context->PSSetSamplers(0, 1, &sampler_state);
            // // DEBUG: i think all the more involved extra stuff are guaranteed zeroed via ClearState();
            // { ID3D11RasterizerState *rs{}; context->RSGetState(&rs); Assert(!rs); }
            // { UINT n{}; context->RSGetScissorRects(&n, nullptr); Assert(!n); }
            // { ID3D11DepthStencilState *dss{}; context->OMGetDepthStencilState(&dss, nullptr); Assert(!dss); }
        }

        glViewport(0, 0, desc.Width, desc.Height);
        {
            // render triangle at mouse pos
            float mouse_x = 0, mouse_y = 0;
            {
                POINT cursor_pos = {};
                RECT cr = {};
                Assert(GetCursorPos(&cursor_pos));
                Assert(GetClientRect(hwnd, &cr));
                Assert(ScreenToClient(hwnd, &cursor_pos));
                mouse_x = cursor_pos.x / ((float)cr.right - cr.left);
                mouse_y = cursor_pos.y / ((float)cr.bottom - cr.top);
                mouse_x = (mouse_x * 2) - 1;
                mouse_y = 1 - (mouse_y * 2);
            }
            // GL CALLS START HERE
            glEnable(GL_FRAMEBUFFER_SRGB);
            float r = 0, g = 0, b = 0;
            if (GetKeyState(VK_LBUTTON) < 0 || GetKeyState('K') < 0) {
                if (epilepsy) {
                    r = 0.2f * !(frame_count & 1);
                    g = 0.2f *  (frame_count & 1);
                } else {
                    r = 0.02f;
                    g = 0.02f;
                    b = 0.02f;
                }
            } else if (epilepsy) {
                r = 0.1f * !(frame_count & 1);
                g = 0.1f *  (frame_count & 1);
            }
            glClearColor(r, g, b, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glBegin(GL_TRIANGLES);
            glColor3f(1, 0, 0);
            glVertex2f(0 * 0.05f + mouse_x, -0.5f * 0.05f + mouse_y);
            glColor3f(0, 1, 0);
            glVertex2f(0.5f * 0.05f + mouse_x, 0.5f * 0.05f + mouse_y);
            glColor3f(0, 0, 1);
            glVertex2f(-0.5f * 0.05f + mouse_x, 0.5f * 0.05f + mouse_y);
            glEnd();
            // GL CALLS END HERE
        }
        if (screenshot) { //DEBUG
            printf("Screenshot!\n");
            char *src = (char *)malloc(desc.Width * desc.Height * sizeof(unsigned));
            Assert(src);
            glReadPixels(0, 0, desc.Width, desc.Height, GL_RGBA, GL_UNSIGNED_BYTE, src); Assert(gl_success());
            rgba_to_clipboard((unsigned *)src, desc.Width, desc.Height);
            free(src);
        }
        { // Blit the normal GL framebuffer to the DX-shared staging framebuffer as a color renderbuffer
            Assert(wglDXLockObjectsNV(device_gldx, 1, &staging_color_renderbuffer_gldx) & gl_success());
            GLint prev_framebuffer = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_framebuffer); Assert(gl_success());
            // printf("Previous framebuffer was %d\n", prev_framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, staging_framebuffer_gl); Assert(gl_success());
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, staging_color_renderbuffer_gl); Assert(gl_success());
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0); Assert(gl_success());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, staging_framebuffer_gl); Assert(gl_success());
            GLenum framebufferStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER); Assert(gl_success());
            if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
                const char *msg = "";
                switch (framebufferStatus) {
                case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:         msg = "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";         break;
                case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: msg = "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT"; break;
                case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:        msg = "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER";        break;
                case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:        msg = "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER";        break;
                case GL_FRAMEBUFFER_UNSUPPORTED:                   msg = "GL_FRAMEBUFFER_UNSUPPORTED";                   break;
                case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:        msg = "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";        break;
                case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:      msg = "GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS";      break;
                }
                printf("Framebuffer incomplete! It was %s\n", msg);
            }
            glBlitFramebuffer(0, 0, desc.Width, desc.Height,
                              0, 0, desc.Width, desc.Height, GL_COLOR_BUFFER_BIT, GL_NEAREST); Assert(gl_success());
            glBindFramebuffer(GL_FRAMEBUFFER, prev_framebuffer); Assert(gl_success());
            glFlush();
            Assert(wglDXUnlockObjectsNV(device_gldx, 1, &staging_color_renderbuffer_gldx) & gl_success());
        }
        context->OMSetRenderTargets(1, &rtv, nullptr);
        context->Draw(4, 0);
        context->Flush();
        // There is a question of whether you want a swap interval of 0.
        //
        // In Independent Flip, I think you always want a swap interval of 0 to
        // prevent the possibility of any up-ratcheting of latency (swap
        // interval 0 tells DXGI "this is the most recent frame, show it ASAP").
        // I haven't figured out whether SetMaximumFrameLatency(1) is sufficient
        // to prevent that up-ratchet. (If you have information on that then
        // please contact me!)
        //
        // The disadvantage of Present(0) is that in Flip mode, the framerate
        // becomes unbounded, so you would need user sleep throttling or to call
        // DwmFlush()/WaitForVBlank() before waiting on the waitable object. (I
        // don't know of a way to determine whether you are in Flip or
        // Independent Flip without checking ETW in admin mode or using
        // heuristics like approximating frame latency.)
        //
        // The thing about throttling the framerate in Flip mode (e.g. in
        // windowed mode) is that you by design incur an extra frame of latency
        // that you could have otherwise gotten away without in Flip mode if you
        // can render at high framerates. You ratchet your own minimum latency
        // up from merely 1 frame to 2. I think this should be a decision on
        // your part as a game or application developer, and it depends on your
        // interpretation of what the phrase "vsync on" really means. (In Flip
        // you're always "v-synced"! Does that necessarily mean your framerate
        // should be capped to 60 Hz?)
        //
        // On a 1080p 60Hz monitor with PresentMon monitoring the app, I could
        // not identify any difference in behaviour between Present(1) and
        // Present(0) + DwmFlush(), and I also could not identify any increased
        // latency by calling DwmFlush() in Independent Flip. Obviously, in
        // Flip, both Present(1) and DwmFlush() add ~1 frame of latency by
        // successfully throttling the framerate, as expected.
        //
        // Generally, I would not recommend DwmFlush(), since I suspect it may
        // wait the wrong amount on discrepant-refresh-rate multimonitor setups,
        // and it feels like a hack in this case. (Then again, so might the
        // waitable object!!!) DwmFlush() is extremely useful in raw OpenGL
        // renderers, though -- call it after SwapBuffers() so your game doesn't
        // do a spinlock-wait for vblank and peg a CPU core to 100% (thanks
        // WGL).

        {
            HRESULT present_result = swapchain->Present(vsync, !vsync? DXGI_PRESENT_ALLOW_TEARING : 0);
            if (present_result == DXGI_STATUS_MODE_CHANGED) resize = true;
            else Assert(present_result == S_OK || present_result == DXGI_STATUS_OCCLUDED);
        }
        // printf("\n===== END OF FRAME %d =====\n", frame_count);
        frame_count++;
        if (frame_count % 30 == 0) {
            update_stats(vsync, epilepsy, dt, frame_latency);
        }
    }
    
    if (rtv) { // @Duplicate
        glFinish();
        Assert(device_gldx && staging_color_renderbuffer_gldx);
        Assert(wglDXUnregisterObjectNV(device_gldx, staging_color_renderbuffer_gldx) & gl_success());
        glDeleteFramebuffers(1, &staging_framebuffer_gl); Assert(gl_success());
        glDeleteRenderbuffers(1, &staging_color_renderbuffer_gl); Assert(gl_success());
        staging_color_renderbuffer_view->Release();
        staging_color_renderbuffer->Release();
        rtv->Release();
#ifndef NDEBUG
        must_succeed(dxgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL));
#endif
    }
    vertex_layout->Release();
    sampler_state->Release();
    pixel_shader ->Release();
    vertex_shader->Release();
    blend_state  ->Release();
    CloseHandle(waitable_object);
    swapchain    ->Release();
    context      ->Release();
    Assert(!!wglDXCloseDeviceNV(device_gldx) & gl_success());
    device       ->Release();
#ifndef NDEBUG
    dxgi_debug   ->Release(); //DEBUG
#endif
    

    return 0;
}
INT WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    PSTR lpCmdLine, INT nCmdShow) { return main(); }
