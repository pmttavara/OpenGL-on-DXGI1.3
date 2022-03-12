
# OpenGL-on-DXGI1.3

**TLDR its in test.cpp**

**Last updated March 2022**

Ok I think i got it working with DXGI1.3 frame-latency-waitable-swap-chains™ thanks to Special K. Looks like Nicolas Guillemot's work was 99% bug-free for flip model, but `wglUnlockObject` must be called *BEFORE* `Present()` and that's it? Either way, the method in that code turned out to not be a good idea generally -- it spits out `GL_ERROR_INCOMPLETE_ATTACHMENT` for reasons I wasn't interested in investigating, and worse yet, it seems like you can easily accidentally get a MASSIVE virtual resource leak of some weird kind on Nvidia with shared GL framebuffer/renderbuffers, especially if you destroy and create a new one every frame. I have a video of Task Manager leaking one terabyte of graphics memory for some reason. Clearly it's benign, but probably a bad idea. `main.cpp` and `dxgl_flip.c` are where you want to look to if you want to see the leaks in action. Maybe it doesn't leak for you! Anyway, I'm sure it's Doing-Something-Non-GL-Spec-Conformant-Which-Means-It's-Not-A-Driver-Bug-But-Rather-User-Error 🙃🙃. (I should have used Mesa3D to check for errors or something!)

Plus, if you render directly to a shared GLDX renderbuffer like that, then you just simply Will Not Get SRGB as an actually properly supported extension on Nvidia. I didn't see any error messages, it's OpenGL, very great API guys.

So instead, just do what Special K's GL layer does - draw everything to your normal FBO, and then blit the framebuffer in GL-land to a staging renderbuffer that is shared in GL\<->DX, and then do another blit of that staging buffer in DX-land to blit it to the backbuffer, mirroring it vertically in the shader to handle the opposite coordinate systems in GL\<->DX. Like 2 copies of overhead but it seems to work without any resource leak. Also, it is basically triple/quadruple buffering, so you have the opportunity to do fun triple-buffering stuff if you want (exercise for the reader, hint, `WaitForSingleObjectEx(waitable_object, 0, true)`).

Works on Nvidia, and appears to work on AMD but you might see blocking incurred on the first GL call(s) of the frame rather than at swap-time? Not sure, I'll update when I have more info, but always test things yourself.


`test.cpp` is the actual file that does everything. It was written trying to be pretty simple, but I think it's not a simple task, so be warned.

Ignore all the latency throttling and present() logic in there though, I am doing things a bit more correctly in Happenlance now but I haven't backported the right stuff. But since you're wondering, real quick I think this is what you want with `DXGI_SWAP_EFFECT_FLIP_DISCARD`, from my testing on Nvidia (can't say for other vendors, seems like things are complicated everywhere):
- Buffer count of 2, always (pretty sure?), but someone please correct me if that's wrong
- Swap interval of 0 in `Present()`, always (if you want vsync-off-style stuff then pass `ALLOW_TEARING` in flags and still do wait on the waitable object) -- otherwise i'm pretty sure you'll ratchet down to permanently be another extra frame latent if you miss your frame budget once, but someone please correct me if that's wrong
- `SetMaximumFrameLatency(2)` if you want to give people some slack, `SetMaximumFrameLatency(1)` otherwise -- my understanding from my testing is that DXGI will do automatic step-down and step-up of latency for you based on how quickly you can present new frames (as long as you have SwapInterval=0), but someone please correct me if that's wrong

Using `DXGI_FRAME_STATISTICS` and `DWM_TIMING_INFO` are completely cringe and broken on the non-primary display.
*REMINDER THAT DWM CAN'T EVEN COMPOSE AT THE REFRESH RATE OF YOUR SECONDARY DISPLAY(S). Good luck 🙃*

Being able to accomplish this as someone at my current skill level involved the following things:
- Looking heavily to the very insane Special K for reference (looks genuinely goated), he/they basically did this perfectly already, this is the technique I went with
- Consulting with Mārtiņš Možeiko (of course!!!)
- Nicolas Guillemot's invaluable sample, which unsurprisingly used Martins's sample as a base
- Ralph Levien on frame pacing https://raphlinus.github.io/ui/graphics/gpu/2021/10/22/swapchain-frame-pacing.html
- Akimitsu Hogge's talk on frame pacing in CoD https://www.activision.com/cdn/research/Hogge_Akimitsu_Controller_to_display.pdf
- A bunch of MSDN as usual
- Personally DMing the guy on Discord who discovered that DWM was broken
- Learning that one of my monitors has residual flicker if you flicker it too much
- Softlocking DWM and needing to reboot my personal development machine multiple times 
- Lots of PresentMon
- Lots of Billy Joel

# vvvv old readme follows vvvv

# OpenGL-on-DXGI
How to use WGL_NV_DX_interop2 to use OpenGL in a DXGI window

## NVIDIA

Tested on GTX 970.

On NVIDIA, this example works fine with a `DXGI_SWAP_EFFECT_DISCARD` swap chain. It seems NVIDIA drivers are currently not updated for Windows 10 swap chains (like `DXGI_SWAP_EFFECT_FLIP_DISCARD`).

Currently, `FLIP_DISCARD` seems to only work for the swap chain's first buffer (strangely), resulting in flickering. The screen is black for all frames that aren't using the first buffer of the swap chain.

Also, there are some exceptions being thrown when the swap chain buffer is registered as an OpenGL resource, which you can see logged in the debug output window. It's spammy, but seems you can ignore them safely.

Also, I'm getting errors in the framebuffer's status. Not sure why yet. The error is different between the first frame of rendering and subsequent frames.

(Last updated: 2016)

## Intel

Same error as AMD (below). Tested on Intel Iris 540.

(Last updated: 2016)

## AMD

Tested on R9 380.

~~Currently has an error and returns null when I call `wglDXRegisterObjectNV`. The error message says "The system cannot open the device or file specified." Haven't yet figured out how to get around this.~~

Update: Apparently this error no longer happens, and it works now. Tested on driver version 18.9.1.

(Last updated: 2018)

## Conclusions

This extension's support doesn't yet match the capabilities of using DXGI with plain D3D, mostly because the implementation of the extension has not been updated for Windows 10 style FLIP swap chains. It works okay with older swap chain types.

Since most of the bugginess comes from trying to access swap chain buffers from OpenGL, you might be able to get away with using this extension by not trying to wrap the swap chain buffers and instead just doing a copy at the end of your frame. Unfortunately, it's easy to introduce extra presentation latency that way.
