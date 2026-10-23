#pragma once
// Custom OpenGL loader header for the VST3 editor.
//
// ImGui's bundled loader (imgui_impl_opengl3_loader.h) works out which GL
// library to resolve from by inspecting what the hosting process already has
// loaded (libEGL / libGLX / libGL / libOpenGL). Inside a plug-in that heuristic
// is unreliable: a host with EGL loaded makes the loader prefer
// eglGetProcAddress(), which cannot resolve core GL functions for a GLX context
// and silently returns dummy stubs -- glCreateShader() then returns 0 with no
// GL error set and ImGui reports a vertex-shader compile failure.
//
// This plug-in already links the GL library directly (OpenGL::GL, i.e.
// libOpenGL.so.0 with GLVND), so declaring the real entry points and letting
// the dynamic linker bind them is both correct and simpler.
//
// Force-include this header when compiling imgui_impl_opengl3.cpp for the
// plug-in only (together with IMGUI_IMPL_OPENGL_LOADER_CUSTOM).

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif

#include <SDL3/SDL_opengl.h>
