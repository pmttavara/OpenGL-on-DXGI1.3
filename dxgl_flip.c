
// EDIT OF https://gist.github.com/mmozeiko/c99f9891ce723234854f0919bfd88eae#file-dxgl_flip-c
// EDIT OF https://gist.github.com/mmozeiko/c99f9891ce723234854f0919bfd88eae#file-dxgl_flip-c
// EDIT OF https://gist.github.com/mmozeiko/c99f9891ce723234854f0919bfd88eae#file-dxgl_flip-c
// EDIT OF https://gist.github.com/mmozeiko/c99f9891ce723234854f0919bfd88eae#file-dxgl_flip-c
// EDIT OF https://gist.github.com/mmozeiko/c99f9891ce723234854f0919bfd88eae#file-dxgl_flip-c
// EDIT OF https://gist.github.com/mmozeiko/c99f9891ce723234854f0919bfd88eae#file-dxgl_flip-c
// EDIT OF https://gist.github.com/mmozeiko/c99f9891ce723234854f0919bfd88eae#file-dxgl_flip-c
// EDIT OF https://gist.github.com/mmozeiko/c99f9891ce723234854f0919bfd88eae#file-dxgl_flip-c
// EDIT OF https://gist.github.com/mmozeiko/c99f9891ce723234854f0919bfd88eae#file-dxgl_flip-c
// EDIT OF https://gist.github.com/mmozeiko/c99f9891ce723234854f0919bfd88eae#file-dxgl_flip-c
// EDIT OF https://gist.github.com/mmozeiko/c99f9891ce723234854f0919bfd88eae#file-dxgl_flip-c
// EDIT OF https://gist.github.com/mmozeiko/c99f9891ce723234854f0919bfd88eae#file-dxgl_flip-c
// EDIT OF https://gist.github.com/mmozeiko/c99f9891ce723234854f0919bfd88eae#file-dxgl_flip-c

#define COBJMACROS
#define INITGUID
#include <intrin.h>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <gl/GL.h>

#include "glext.h" // https://www.opengl.org/registry/api/GL/glext.h
#include "wglext.h" // https://www.opengl.org/registry/api/GL/wglext.h

#pragma comment (lib, "d3d11.lib")
#pragma comment (lib, "dxgi.lib")
#pragma comment (lib, "opengl32.lib")

#define Assert(cond) do { if (!(cond)) __debugbreak(); } while (0)
#define AssertHR(hr) Assert(SUCCEEDED(hr))

static GLuint colorRbuf;
static GLuint dsRbuf;
static GLuint fbuf;

static HANDLE dxDevice;

static ID3D11Device* device;
static ID3D11DeviceContext* context;
static IDXGISwapChain1* swapChain;

static PFNWGLDXOPENDEVICENVPROC wglDXOpenDeviceNV;
static PFNWGLDXCLOSEDEVICENVPROC wglDXCloseDeviceNV;
static PFNWGLDXREGISTEROBJECTNVPROC wglDXRegisterObjectNV;
static PFNWGLDXUNREGISTEROBJECTNVPROC wglDXUnregisterObjectNV;
static PFNWGLDXLOCKOBJECTSNVPROC wglDXLockObjectsNV;
static PFNWGLDXUNLOCKOBJECTSNVPROC wglDXUnlockObjectsNV;

static PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
static PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
static PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers;
static PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers;
static PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
static PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer;

static HWND temp;
static HDC tempdc;
static HGLRC temprc;

static void APIENTRY DebugCallback(GLenum source, GLenum type, GLuint id,
    GLenum severity, GLsizei length, const GLchar *message, const void *userParam)
{
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
    if (severity == GL_DEBUG_SEVERITY_MEDIUM || severity == GL_DEBUG_SEVERITY_HIGH)
    {
        Assert(0);
    }
}

static void Create(HWND window)
{
    // GL context on temporary window, no drawing will happen to this window
    {
        temp = CreateWindowA("STATIC", "temp", WS_OVERLAPPED,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            NULL, NULL, NULL, NULL);
        Assert(temp);

        tempdc = GetDC(temp);
        Assert(tempdc);

        PIXELFORMATDESCRIPTOR pfd =
        {
            .nSize = sizeof(pfd),
            .nVersion = 1,
            .dwFlags = PFD_SUPPORT_OPENGL,
            .iPixelType = PFD_TYPE_RGBA,
            .iLayerType = PFD_MAIN_PLANE,
        };

        int format = ChoosePixelFormat(tempdc, &pfd);
        Assert(format);

        DescribePixelFormat(tempdc, format, sizeof(pfd), &pfd);
        BOOL set = SetPixelFormat(tempdc, format, &pfd);
        Assert(set);

        temprc = wglCreateContext(tempdc);
        Assert(temprc);

        BOOL make = wglMakeCurrent(tempdc, temprc);
        Assert(make);

        PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = (void*)wglGetProcAddress("wglCreateContextAttribsARB");

        int attrib[] =
        {
            WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
            0,
        };

        HGLRC newrc = wglCreateContextAttribsARB(tempdc, NULL, attrib);
        Assert(newrc);

        make = wglMakeCurrent(tempdc, newrc);
        Assert(make);

        wglDeleteContext(temprc);
        temprc = newrc;

        PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback = (void*)wglGetProcAddress("glDebugMessageCallback");
        glDebugMessageCallback(DebugCallback, 0);

        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    }

    wglDXOpenDeviceNV = (void*)wglGetProcAddress("wglDXOpenDeviceNV");
    wglDXCloseDeviceNV = (void*)wglGetProcAddress("wglDXCloseDeviceNV");

    wglDXRegisterObjectNV = (void*)wglGetProcAddress("wglDXRegisterObjectNV");
    wglDXUnregisterObjectNV = (void*)wglGetProcAddress("wglDXUnregisterObjectNV");

    wglDXLockObjectsNV = (void*)wglGetProcAddress("wglDXLockObjectsNV");
    wglDXUnlockObjectsNV = (void*)wglGetProcAddress("wglDXUnlockObjectsNV");

    glGenFramebuffers = (void*)wglGetProcAddress("glGenFramebuffers");
    glDeleteFramebuffers = (void*)wglGetProcAddress("glDeleteFramebuffers");

    glGenRenderbuffers = (void*)wglGetProcAddress("glGenRenderbuffers");
    glDeleteRenderbuffers = (void*)wglGetProcAddress("glDeleteRenderbuffers");

    glBindFramebuffer = (void*)wglGetProcAddress("glBindFramebuffer");
    glFramebufferRenderbuffer = (void*)wglGetProcAddress("glFramebufferRenderbuffer");

    // create D3D11 device & context
    {
        UINT flags = D3D11_CREATE_DEVICE_DEBUG;

        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
        HRESULT hr = D3D11CreateDevice(
            NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, levels, ARRAYSIZE(levels),
            D3D11_SDK_VERSION, &device, NULL, &context);
        AssertHR(hr);
    }

    // enable VERY USEFUL debug break on API errors
    {
        ID3D11InfoQueue* info;
        ID3D11Device_QueryInterface(device, &IID_ID3D11InfoQueue, (void**)&info);
        ID3D11InfoQueue_SetBreakOnSeverity(info, D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        ID3D11InfoQueue_SetBreakOnSeverity(info, D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
        ID3D11InfoQueue_Release(info);
    }

    // create DXGI swap chain
    {
        IDXGIFactory2* factory;
        HRESULT hr = CreateDXGIFactory(&IID_IDXGIFactory2, &factory);
        AssertHR(hr);

        DXGI_SWAP_CHAIN_DESC1 desc =
        {
            .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
            .SampleDesc = { 1, 0 },
            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = 2,
            .Scaling = DXGI_SCALING_NONE,
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        };

        hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown*)device, window, &desc, NULL, NULL, &swapChain);
        AssertHR(hr);

        IDXGIFactory_Release(factory);
    }

    dxDevice = wglDXOpenDeviceNV(device);
    Assert(dxDevice);

    glGenRenderbuffers(1, &colorRbuf);
    glGenRenderbuffers(1, &dsRbuf);
    glGenFramebuffers(1, &fbuf);

    glBindFramebuffer(GL_FRAMEBUFFER, fbuf);
}

static void Destroy()
{
    ID3D11DeviceContext_ClearState(context);

    glDeleteFramebuffers(1, &fbuf);
    glDeleteRenderbuffers(1, &colorRbuf);
    glDeleteRenderbuffers(1, &dsRbuf);

    wglDXCloseDeviceNV(dxDevice);

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(temprc);
    ReleaseDC(temp, tempdc);

    ID3D11DeviceContext_ClearState(context);
    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);
    IDXGISwapChain_Release(swapChain);
}

static void Resize(int width, int height)
{
    ID3D11DeviceContext_ClearState(context);

    HRESULT hr = IDXGISwapChain_ResizeBuffers(swapChain, 0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    AssertHR(hr);

    D3D11_VIEWPORT view =
    {
        .TopLeftX = 0.f,
        .TopLeftY = 0.f,
        .Width = (float)width,
        .Height = (float)height,
        .MinDepth = 0.f,
        .MaxDepth = 1.f,
    };
    ID3D11DeviceContext_RSSetViewports(context, 1, &view);

    glViewport(0, 0, width, height);
}

static LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_CREATE:
        Create(window);
        return 0;

    case WM_DESTROY:
        Destroy();
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
        Resize(LOWORD(lparam), HIWORD(lparam));
        return 0;
    }

    return DefWindowProcA(window, msg, wparam, lparam);
}

int main()
{
    WNDCLASSA wc =
    {
        .lpfnWndProc = WindowProc,
        .lpszClassName = "DXGL",
    };

    ATOM atom = RegisterClassA(&wc);
    Assert(atom);

    HWND window = CreateWindowExA(
        WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, "DXGL",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, NULL, NULL);
    Assert(window);

    int running = 1;
    int fullscreen = 0;
    WINDOWPLACEMENT previous_window_placement = {sizeof(previous_window_placement)};
    for (;;)
    {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                running = 0;
                break;
            } else if (msg.message == WM_KEYDOWN) {
                if (msg.wParam == VK_F11) {
                    fullscreen = !fullscreen;
                    if (fullscreen) {
                        MONITORINFO mi = {sizeof(mi)};
                        if (GetWindowPlacement(window, &previous_window_placement) &&
                            GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &mi)) {
                            SetWindowLongA(window, GWL_STYLE, WS_VISIBLE);
                            SetWindowPos(window, HWND_NOTOPMOST,
                                         mi.rcMonitor.left, mi.rcMonitor.top,
                                         mi.rcMonitor.right - mi.rcMonitor.left,
                                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                                         SWP_FRAMECHANGED);
                        }
                    } else {
                        SetWindowLongA(window, GWL_STYLE, WS_OVERLAPPEDWINDOW);
                        SetWindowPlacement(window, &previous_window_placement);
                        SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0,
                                     SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
                    }
                }
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!running)
        {
            break;
        }

        HANDLE dxColor, dxDepthStencil;
        ID3D11RenderTargetView* colorView;
        ID3D11DepthStencilView* dsView;
        {
            ID3D11Texture2D* colorBuffer;
            HRESULT hr = IDXGISwapChain_GetBuffer(swapChain, 0, &IID_ID3D11Texture2D, &colorBuffer);
            AssertHR(hr);

            //NOTE: it is clear to me that GL-on-DX does not respect sRGB conversion
            //      which means that it is GENUINELY BROKEN!!!!!!
            hr = ID3D11Device_CreateRenderTargetView(device, (void*)colorBuffer, NULL, &colorView);
            AssertHR(hr);

            // create depth-stencil view based on size from color buffer
            // this can be cached from previous frame if size has not changed
            // or you could skip depth buffer completely (just use GL only depth renderbuffer)
            ID3D11Texture2D* dsBuffer;
            {
                D3D11_TEXTURE2D_DESC desc;
                ID3D11Texture2D_GetDesc(colorBuffer, &desc);
                desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

                hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &dsBuffer);
                AssertHR(hr);

                hr = ID3D11Device_CreateDepthStencilView(device, (void*)dsBuffer, NULL, &dsView);
                AssertHR(hr);
            }

            dxColor = wglDXRegisterObjectNV(dxDevice, colorBuffer, colorRbuf, GL_RENDERBUFFER, WGL_ACCESS_READ_WRITE_NV);
            Assert(dxColor);

            dxDepthStencil = wglDXRegisterObjectNV(dxDevice, dsBuffer, dsRbuf, GL_RENDERBUFFER, WGL_ACCESS_READ_WRITE_NV);
            Assert(dxDepthStencil);

            ID3D11Texture2D_Release(colorBuffer);
            ID3D11Texture2D_Release(dsBuffer);
        }

        // render with D3D
        {
            FLOAT cornflowerBlue[] = { 100.f / 255.f, 149.f / 255.f, 237.f / 255.f, 1.f };
            ID3D11DeviceContext_OMSetRenderTargets(context, 1, &colorView, dsView);
            ID3D11DeviceContext_ClearRenderTargetView(context, colorView, cornflowerBlue);
            ID3D11DeviceContext_ClearDepthStencilView(context, dsView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0, 0);
        }

        HANDLE dxObjects[] = { dxColor, dxDepthStencil };
        wglDXLockObjectsNV(dxDevice, _countof(dxObjects), dxObjects);

        // render with GL
        {
            glBindFramebuffer(GL_FRAMEBUFFER, fbuf);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorRbuf);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, dsRbuf);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, dsRbuf);

            glBegin(GL_TRIANGLES);
            glColor3f(1, 0, 0);
            glVertex2f(0.f, -0.5f);
            glColor3f(0, 1, 0);
            glVertex2f(0.5f, 0.5f);
            glColor3f(0, 0, 1);
            glVertex2f(-0.5f, 0.5f);
            glEnd();

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        wglDXUnlockObjectsNV(dxDevice, _countof(dxObjects), dxObjects);

        wglDXUnregisterObjectNV(dxDevice, dxColor);
        wglDXUnregisterObjectNV(dxDevice, dxDepthStencil);

        ID3D11RenderTargetView_Release(colorView);
        ID3D11DepthStencilView_Release(dsView);

        HRESULT hr = IDXGISwapChain_Present(swapChain, 1, 0);
        Assert(SUCCEEDED(hr));
    }
}