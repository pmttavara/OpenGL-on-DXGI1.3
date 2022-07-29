
# OpenGL on DXGI1.3

**TLDR: The main code file example for the project is in test.cpp. Download and build the Visual Studio project file.**

**Last updated July 2022**

This is a minimum-viable-product proof of concept program for drawing to an OpenGL rendering context, but presenting via a new DXGI 1.3 frame latency waitable swap chain. This confers the latency and control advantages of those swapchains without requiring a developer to port their entire renderer from OpenGL to DirectX. In theory, this would also allow a developer to gradually transition their program from OpenGL to DirectX in steps, with the two APIs working together in the interim.

The process is as follows:

- Draw in OpenGL to your normal FBO
- Blit the framebuffer in GL-land to a staging renderbuffer that is shared in GL\<->DX
- Blit that staging buffer in DX-land to the DX backbuffer, mirroring it vertically in the shader to handle the opposite coordinate systems in GL\<->DX

This incurs 2 copies of overhead. Also, it is basically triple/quadruple buffering, so you have the opportunity to use the intermediate staging buffers for a triple-buffer system if desired (e.g., leverage `WaitForSingleObjectEx(waitable_object, 0, true)` to implement unlimited framerates without screen tearing even in Independent Flip model hardware composition).

Thoroughly tested on Nvidia cards, and appears to work on AMD cards tested, but I think you might see blocking incurred on the first GL call(s) of the frame rather than at swap-time. I'll update when I have more info, but always test things yourself.

`test.cpp` is the actual file that does everything. It was written trying to be pretty simple, but I think it's not a simple task, so be warned.
It also contains a sprinkling of comments that go further into the weeds on some of the behaviour and how it affects frame pacing.

If you're wondering how to set up your basic frame pacing, then I think this is what you want, based on my testing with Nvidia (can't say for other vendors).
- As per Microsoft recommendation, `DXGI_SWAP_EFFECT_FLIP_DISCARD` with a buffer count of 2, and wait on the waitable object at frame start before input processing.
- `SetMaximumFrameLatency(1)`.
- There's no easy way to know when you're in Independent Flip vs. Flip, so you likely want to call `Present(vsync ? 1 : 0, vsync ? 0 : ALLOW_TEARING)`.
- If you're (somehow) sure you're in Independent Flip mode: `Present(0, vsync ? 0 : ALLOW_TEARING)`.
- If you're (somehow) sure you're in Flip mode:
    - Either call `Present(1)`, or
    - `Present(0)` + `DwmFlush()` before input processing, or possibly even
    - `Present(0)` + `WaitForVBlank()` before input processing, or
    - `Present(0)` + user sleep throttling before input processing.

As an aside, it seems that `DXGI_FRAME_STATISTICS` and `DWM_TIMING_INFO` are broken on the non-primary display. DWM apparently can't even compose at the refresh rate of the secondary display(s).

## Acknowledgements
I extend my gratitude to the gracious help and material of these parties:
- [Special K](https://special-k.info/) for the [code](https://github.com/SpecialKO/SpecialK/blob/NOCEGUI/src/render/gl/opengl.cpp) that I eventually based mine on
- [Nicolas Guillemot](https://github.com/nlguillemot/OpenGL-on-DXGI) for inspiring me to start this project
- [Mārtiņš Možeiko](https://twitter.com/mmozeiko) and [his sample](https://gist.github.com/mmozeiko/c99f9891ce723234854f0919bfd88eae#file-dxgl_flip-c) for being of critical help as usual
- [Ralph Levien](https://raphlinus.github.io/ui/graphics/gpu/2021/10/22/swapchain-frame-pacing.html) and [Akimitsu Hogge](https://www.activision.com/cdn/research/Hogge_Akimitsu_Controller_to_display.pdf) for introducing me to frame pacing
- MSDN for documentation on all the relevant calls
- [bplu4t2f](https://github.com/bplu4t2f/vsync_test/blob/master/README.md) whom I personally talked with about DWM's broken insanity
- [PresentMon authors](https://github.com/GameTechDev/PresentMon/graphs/contributors)
- Billy Joel
