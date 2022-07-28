
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

If you're wondering how to set up your basic frame pacing, then I think this is what you want, based on my testing with Nvidia (can't say for other vendors -- seems like things are complicated everywhere).
- `DXGI_SWAP_EFFECT_FLIP_DISCARD`.
- Buffer count of 2, always (pretty sure? -- please correct me if that's wrong)
- In Independent Flip mode: Swap interval of 0 in `Present()`, always (if you want vsync-off-style stuff then pass `ALLOW_TEARING` in flags and still do wait on the waitable object) -- otherwise i'm pretty sure you'll ratchet down to permanently be another extra frame latent if you miss your frame budget once, but someone please correct me if that's wrong
- In Flip mode: You want user sleep throttling if you present with a swap interval of 0. Otherwise, present with a swap interval of 1.
- `SetMaximumFrameLatency(2)` if you want to give people some slack, `SetMaximumFrameLatency(1)` otherwise -- my understanding from my testing is that DXGI will do automatic step-down and step-up of latency for you based on how quickly you can present new frames (as long as you have SwapInterval=0), but someone please correct me if that's wrong
- When vsync is enabled, you *may* want to call `DwmFlush()` before `WaitForSingleObjectEx`, but be *very* measured about that decision. (Read test.cpp for why.)

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
