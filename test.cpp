
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

#include "glext.h"
#include "wglext.h"

#define print_log(fmt, ...) do { printf(sizeof("" fmt "") > 0 ? (fmt) : "", __VA_ARGS__); fflush(stdout); } while (0);

#undef assert
#define assert(...) do { if (!(__VA_ARGS__)) { print_log("ASSERT FAIL:\n    \"" #__VA_ARGS__ "\"\n\n"); __debugbreak(); } } while (0);

#include <stdlib.h> // @Remove
#include <math.h> // @Remove

bool resize = false;
static LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
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
#define must_succeed(e) assert(must_succeed_(e))
static double invfreq = 0;
static double get_time() {
    LARGE_INTEGER li = {0};
    if (!invfreq) {
        QueryPerformanceFrequency(&li);
        invfreq = 1.0 / li.QuadPart;
    }
    QueryPerformanceCounter(&li);
    return li.QuadPart * invfreq;
}
#define time(stmt) do { double time = -get_time(); { stmt; } time += get_time(); print_log("" #stmt " took %f ms\n", time * 1000); } while (0)
// #define must_succeed(e) time(assert(must_succeed_(e)))
static void APIENTRY DebugCallback(GLenum source, GLenum type, GLuint id,
    GLenum severity, GLsizei length, const GLchar *message, const void *userParam)
{
    print_log("GL Debug: %s\n", message);
    if (/*severity == GL_DEBUG_SEVERITY_MEDIUM ||*/ severity == GL_DEBUG_SEVERITY_HIGH) {
        fflush(stdout);
        assert(0);
    }
}
static bool gl_success() {
    bool success = true;
    while (int err = glGetError()) {
        success = false;
        print_log("glGetError(): ");
        switch (err) {
        case GL_INVALID_ENUM:      print_log("GL_INVALID_ENUM");      break;
        case GL_INVALID_VALUE:     print_log("GL_INVALID_VALUE");     break;
        case GL_INVALID_OPERATION: print_log("GL_INVALID_OPERATION"); break;
        case GL_STACK_OVERFLOW:    print_log("GL_STACK_OVERFLOW");    break;
        case GL_STACK_UNDERFLOW:   print_log("GL_STACK_UNDERFLOW");   break;
        case GL_OUT_OF_MEMORY:     print_log("GL_OUT_OF_MEMORY");     break;
        }
        print_log("\n");
    }
    return success;
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

int main() {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = "DXGL";
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);

    ATOM atom = RegisterClassA(&wc);
    assert(atom);

    HWND hwnd = CreateWindowA(wc.lpszClassName, "DXGL",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        640, 480, nullptr, nullptr, nullptr, nullptr);
    HWND gl_hwnd = nullptr;
    {
        auto temp = CreateWindowA("STATIC", "temp", 0,
            CW_USEDEFAULT, CW_USEDEFAULT, 640/2, 480/2,
            nullptr, nullptr, nullptr, nullptr);
        assert(temp);
        temp = hwnd;
        gl_hwnd = temp;
        auto tempdc = GetDC(temp);
        assert(tempdc);
        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_SUPPORT_OPENGL;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.iLayerType = PFD_MAIN_PLANE;
        int format = ChoosePixelFormat(tempdc, &pfd);
        assert(format);
        DescribePixelFormat(tempdc, format, sizeof(pfd), &pfd);
        BOOL set = SetPixelFormat(tempdc, format, &pfd);
        assert(set);
        auto temprc = wglCreateContext(tempdc);
        assert(temprc);
        BOOL make = wglMakeCurrent(tempdc, temprc);
        assert(make);
        int attrib0[] = {WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB, 0};
        float fattrib0[] = { 0 };
        unsigned int matching = 0;
        auto wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
        assert(wglChoosePixelFormatARB);
        assert(wglChoosePixelFormatARB(tempdc, attrib0, fattrib0, 1, &format, &matching));
        auto wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
        int attrib[] =
        {
            WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB, 0,
        };
        HGLRC newrc = wglCreateContextAttribsARB(tempdc, NULL, attrib);
        assert(newrc);
        make = wglMakeCurrent(tempdc, newrc);
        assert(make);
        wglDeleteContext(temprc);
        temprc = newrc;
        auto glDebugMessageCallback = (PFNGLDEBUGMESSAGECALLBACKPROC )wglGetProcAddress("glDebugMessageCallback");
        glDebugMessageCallback(DebugCallback, 0);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    }
    //initted once
    IDXGIDebug1 *dxgi_debug = nullptr; //DEBUG
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
    must_succeed(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug))); //DEBUG
    {
        ID3D11Device *base_device = nullptr;
        ID3D11DeviceContext *base_context = nullptr;
        must_succeed(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION, &base_device, nullptr, &base_context));
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
    { //DEBUG
        ID3D11InfoQueue *info_queue = nullptr;
        must_succeed(device->QueryInterface(IID_PPV_ARGS(&info_queue)));
        info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
        info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
        // info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, true);
        info_queue->Release();
    }
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
        must_succeed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_IDXGIFactory2, (void**)&factory));
        must_succeed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_PRINT_SCREEN | DXGI_MWA_NO_WINDOW_CHANGES));
        //NOTE: if we fail to create the swap chain, do NOT
        //      fall back to older presentation models, since
        //      they confer no advantage over SDL WGL's default.
        must_succeed(factory->CreateSwapChainForHwnd(device, hwnd, &desc, nullptr, nullptr, &base_swapchain));
        must_succeed(base_swapchain->QueryInterface(&swapchain));
        assert(waitable_object = swapchain->GetFrameLatencyWaitableObject());
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
        if (errors) print_log("D3DCompile Errors:\n%s\n", (char *)errors->GetBufferPointer());
        assert(compile_result == S_OK && vs_blob);
        void *bytecode = vs_blob->GetBufferPointer();
        size_t len = vs_blob->GetBufferSize();
        assert(bytecode && len);
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
        if (errors) print_log("D3DCompile Errors:\n%s\n", (char *)errors->GetBufferPointer());
        assert(compile_result == S_OK && ps_blob);
        void *bytecode = ps_blob->GetBufferPointer();
        size_t len = ps_blob->GetBufferSize();
        assert(bytecode && len);
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
    assert(*(void**)&wglDXOpenDeviceNV         = wglGetProcAddress("wglDXOpenDeviceNV"));
    assert(*(void**)&wglDXCloseDeviceNV        = wglGetProcAddress("wglDXCloseDeviceNV"));
    assert(*(void**)&wglDXRegisterObjectNV     = wglGetProcAddress("wglDXRegisterObjectNV"));
    assert(*(void**)&wglDXUnregisterObjectNV   = wglGetProcAddress("wglDXUnregisterObjectNV"));
    assert(*(void**)&wglDXLockObjectsNV        = wglGetProcAddress("wglDXLockObjectsNV"));
    assert(*(void**)&wglDXUnlockObjectsNV      = wglGetProcAddress("wglDXUnlockObjectsNV"));
    assert(*(void**)&glGenFramebuffers         = wglGetProcAddress("glGenFramebuffers"));
    assert(*(void**)&glDeleteFramebuffers      = wglGetProcAddress("glDeleteFramebuffers"));
    assert(*(void**)&glGenRenderbuffers        = wglGetProcAddress("glGenRenderbuffers"));
    assert(*(void**)&glDeleteRenderbuffers     = wglGetProcAddress("glDeleteRenderbuffers"));
    assert(*(void**)&glBindFramebuffer         = wglGetProcAddress("glBindFramebuffer"));
    assert(*(void**)&glFramebufferRenderbuffer = wglGetProcAddress("glFramebufferRenderbuffer"));
    assert(*(void**)&glCheckFramebufferStatus  = wglGetProcAddress("glCheckFramebufferStatus"));
    assert(*(void**)&glBlitFramebuffer         = wglGetProcAddress("glBlitFramebuffer"));
    assert(!!(device_gldx = wglDXOpenDeviceNV(device)) & gl_success());
    //reinitted every resize
    ID3D11RenderTargetView *rtv = nullptr;
    ID3D11Texture2D *staging_color_renderbuffer = nullptr;
    ID3D11ShaderResourceView *staging_color_renderbuffer_view = nullptr;
    GLuint staging_color_renderbuffer_gl = 0;
    HANDLE staging_color_renderbuffer_gldx = 0;
    GLuint staging_framebuffer_gl = 0;

    resize = true;
    unsigned int frame_count = 0;
    bool fullscreen = false;
    bool vsync = true;
    bool epilepsy = false;
    WINDOWPLACEMENT previous_window_placement = {sizeof(previous_window_placement)};
    double last_frame_time_on_cpu = 0;
    double last_frame_time_slept = 0;
    for (;;) {
        double frame_start = get_time();
        {
            print_log("Waiting on frame latency waitable object...\n");
            DWORD wait_result = WaitForSingleObjectEx(waitable_object, 1000, true);
            if (wait_result == WAIT_FAILED) {
                print_log("WARNING: Waiting on the swapchain failed!!!\n");
                assert(false); // what do we do?
            } else if (wait_result == WAIT_TIMEOUT) {
                print_log("WARNING: Waiting on the swapchain exceeded 1 second!!!\n");
            } else {
                assert(wait_result == WAIT_OBJECT_0);
            }
        }
        //assert();
        print_log("\n===== START OF FRAME %d =====\n", frame_count);
        if (vsync) {
            DXGI_FRAME_STATISTICS stats = {};
            HRESULT gfs_result = swapchain->GetFrameStatistics(&stats);
            if (gfs_result == DXGI_ERROR_FRAME_STATISTICS_DISJOINT) {
                // what do we do?
                int k = 0;
                last_frame_time_slept = 0;
            } else {
                assert(gfs_result == S_OK);
                print_log("========== Frame Statistics =============\n"
                          "    PresentCount:            %08u\n"
                          "    PresentRefreshCount:     %08u\n"
                          "    SyncRefreshCount:        %08u\n"
                          "    SyncQPCTime:             %08llu\n",
                          stats.PresentCount, stats.PresentRefreshCount, stats.SyncRefreshCount, stats.SyncQPCTime.QuadPart);
                double secondsOfLastSync = stats.SyncQPCTime.QuadPart * invfreq;
                double preciseRefreshInterval = secondsOfLastSync / stats.SyncRefreshCount;
                print_log("\n    Refresh rate must be %f Hz then\n", 1 / preciseRefreshInterval);
                print_log("=========================================\n");
                // print_log("Frame %d took %f ms\n", frame_count, frame_time_minus_swap * 1000);
                double nextSync = secondsOfLastSync + preciseRefreshInterval;
                double beforeSleep = get_time();
                double timeUntilSync = nextSync - beforeSleep;
                double timeToDoOneFrameOnCPU = last_frame_time_on_cpu; // @Temporary! Pessimistically filter the last N frame times.
                double timeToRenderSceneBufferOnGPU = 0.003f; // @Temporary! Use glQuery(GL_TIMESTAMP) for this, I think.
                double timeToTranscodeOnGPU = 0.003f; // @Temporary! Use some sort of DX timestamps for this.
                double slop = 0.003; // Semi-@Hack? @Inexplicable?
                double timeToSleep = (timeUntilSync - timeToDoOneFrameOnCPU - timeToRenderSceneBufferOnGPU - timeToTranscodeOnGPU) - slop;
                assert(timeToSleep < timeUntilSync);
                int sleepMs = (int)(timeToSleep * 1000);
                if (sleepMs > 0) {
                    Sleep((DWORD)sleepMs);
                    last_frame_time_slept = get_time() - beforeSleep;
                }
                // print_log("Last frame CPU time: %f ms\n", timeToDoOneFrameOnCPU * 1000);
                // print_log("Last frame GPU time: %f ms\n", (timeToRenderSceneBufferOnGPU + timeToTranscodeOnGPU) * 1000);
                // print_log("Throttled for %f ms to reduce latency\n", timeToSleep * 1000);
            }
        }
        
        double frame_start_after_swap_and_sleep = get_time();
        
        bool quit = false;
        bool screenshot = false;
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
                break;
            } else if (msg.message == WM_KEYDOWN) {
                if (msg.wParam == VK_F11) {
                    resize = true;
                    fullscreen = !fullscreen;
                    if (fullscreen) {
                        MONITORINFO mi = {sizeof(mi)};
                        assert(GetWindowPlacement(hwnd, &previous_window_placement));
                        assert(GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi));
                        assert(SetWindowLongA(hwnd, GWL_STYLE, WS_VISIBLE));
                        assert(SetWindowPos(hwnd, HWND_NOTOPMOST, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED));
                    } else {
                        assert(SetWindowLongA(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW));
                        assert(SetWindowPlacement(hwnd, &previous_window_placement));
                        assert(SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED));
                    }
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

        // Update game sim: let's fake some work
        // Sleep((DWORD)((sin(get_time())*.5+.5) * 6));

        if (resize) {
            resize = false;
            if (rtv) {
                glFinish();
                assert(device_gldx && staging_color_renderbuffer_gldx);
                assert(wglDXUnregisterObjectNV(device_gldx, staging_color_renderbuffer_gldx) & gl_success());
                glDeleteFramebuffers(1, &staging_framebuffer_gl); assert(gl_success());
                glDeleteRenderbuffers(1, &staging_color_renderbuffer_gl); assert(gl_success());
                staging_color_renderbuffer_view->Release();
                staging_color_renderbuffer->Release();
                rtv->Release();
                must_succeed(dxgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL));
            }
            must_succeed(swapchain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, desc.Flags));
            must_succeed(swapchain->GetDesc1(&desc)); //call AFTER ResizeBuffers to get the right W+H!!
            //NOTE: if you use a separate fake-window for getting GL, then you need to do this to make GL's FBO resize!!!!!
            if (gl_hwnd != hwnd) {
                RECT rect = {};
                assert(GetWindowRect(hwnd, &rect));
                LONG style = GetWindowLongA(hwnd, GWL_STYLE);
                assert(style);
                assert(SetWindowLongA(gl_hwnd, GWL_STYLE, style & ~WS_VISIBLE));
                assert(SetWindowPos(gl_hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW | SWP_NOZORDER | SWP_HIDEWINDOW | SWP_FRAMECHANGED));
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
                glGenRenderbuffers(1, &staging_color_renderbuffer_gl); assert(gl_success());
                glGenFramebuffers(1, &staging_framebuffer_gl); assert(gl_success());
                assert(!!(staging_color_renderbuffer_gldx = wglDXRegisterObjectNV(device_gldx, staging_color_renderbuffer, staging_color_renderbuffer_gl, GL_RENDERBUFFER, WGL_ACCESS_WRITE_DISCARD_NV)) & gl_success());
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
            // { ID3D11RasterizerState *rs{}; context->RSGetState(&rs); assert(!rs); }
            // { UINT n{}; context->RSGetScissorRects(&n, nullptr); assert(!n); }
            // { ID3D11DepthStencilState *dss{}; context->OMGetDepthStencilState(&dss, nullptr); assert(!dss); }
        }

        glViewport(0, 0, desc.Width, desc.Height);
        {
            // render triangle at mouse pos
            float mouse_x = 0, mouse_y = 0;
            {
                POINT cursor_pos = {};
                RECT cr = {};
                assert(GetCursorPos(&cursor_pos));
                assert(GetClientRect(hwnd, &cr));
                assert(ScreenToClient(hwnd, &cursor_pos));
                mouse_x = cursor_pos.x / ((float)cr.right - cr.left);
                mouse_y = cursor_pos.y / ((float)cr.bottom - cr.top);
                mouse_x = (mouse_x * 2) - 1;
                mouse_y = 1 - (mouse_y * 2);
            }
            // GL CALLS START HERE
            glEnable(GL_FRAMEBUFFER_SRGB);
            glClearColor(0, 0, 0, 1);
            static int counter; if (epilepsy) counter++;
            glClearColor(0.2f * !(counter & 1), 0.2f * (counter & 1), 0, 1);
            if (GetKeyState(VK_LBUTTON) < 0 || GetKeyState('K') < 0) glClearColor(1, 1, 1, 1);
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
            print_log("Screenshot!\n");
            char *src = (char *)malloc(desc.Width * desc.Height * sizeof(unsigned));
            assert(src);
            glReadPixels(0, 0, desc.Width, desc.Height, GL_RGBA, GL_UNSIGNED_BYTE, src); assert(gl_success());
            rgba_to_clipboard((unsigned *)src, desc.Width, desc.Height);
            free(src);
        }
        { // Blit the normal GL framebuffer to the DX-shared staging framebuffer as a color renderbuffer
            assert(wglDXLockObjectsNV(device_gldx, 1, &staging_color_renderbuffer_gldx) & gl_success());
            GLint prev_framebuffer = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_framebuffer); assert(gl_success());
            print_log("Previous framebuffer was %d\n", prev_framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, staging_framebuffer_gl); assert(gl_success());
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, staging_color_renderbuffer_gl); assert(gl_success());
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0); assert(gl_success());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, staging_framebuffer_gl); assert(gl_success());
            GLenum framebufferStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER); assert(gl_success());
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
                print_log("Framebuffer incomplete! It was %s\n", msg);
            }
            glBlitFramebuffer(0, 0, desc.Width, desc.Height,
                              0, 0, desc.Width, desc.Height, GL_COLOR_BUFFER_BIT, GL_NEAREST); assert(gl_success());
            glBindFramebuffer(GL_FRAMEBUFFER, prev_framebuffer); assert(gl_success());
            glFlush();
            assert(wglDXUnlockObjectsNV(device_gldx, 1, &staging_color_renderbuffer_gldx) & gl_success());
        }
        context->OMSetRenderTargets(1, &rtv, nullptr);
        context->Draw(4, 0);
        context->Flush();
        {
            HRESULT present_result = swapchain->Present(vsync, !vsync? DXGI_PRESENT_ALLOW_TEARING : 0);
            if (present_result == DXGI_STATUS_MODE_CHANGED) resize = true; else assert(present_result == S_OK);
            last_frame_time_on_cpu = get_time() - frame_start_after_swap_and_sleep;
        }
        double frame_time = get_time() - frame_start;
        print_log("\n  Frame %d took %f ms (%f ms slept, then %f ms on CPU, then %f swap)\n", frame_count, frame_time * 1000, last_frame_time_slept * 1000, last_frame_time_on_cpu * 1000, (frame_time - last_frame_time_on_cpu - last_frame_time_slept) * 1000);
        print_log("\n===== END OF FRAME %d =====\n", frame_count);
        frame_count++;
    }
    
    if (rtv) { // @Duplicate
        glFinish();
        assert(device_gldx && staging_color_renderbuffer_gldx);
        assert(wglDXUnregisterObjectNV(device_gldx, staging_color_renderbuffer_gldx) & gl_success());
        glDeleteFramebuffers(1, &staging_framebuffer_gl); assert(gl_success());
        glDeleteRenderbuffers(1, &staging_color_renderbuffer_gl); assert(gl_success());
        staging_color_renderbuffer_view->Release();
        staging_color_renderbuffer->Release();
        rtv->Release();
        must_succeed(dxgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL));
    }
    vertex_layout->Release();
    sampler_state->Release();
    pixel_shader ->Release();
    vertex_shader->Release();
    blend_state  ->Release();
    CloseHandle(waitable_object);
    swapchain    ->Release();
    context      ->Release();
    assert(!!wglDXCloseDeviceNV(device_gldx) & gl_success());
    device       ->Release();
    dxgi_debug   ->Release(); //DEBUG
    

    return 0;
}
