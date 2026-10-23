#include "SynthEditor.h"

#include "SynthVst3.h"

#include "gl_loader.h"

#include <SDL3/SDL.h>

#include "Gui.h"

#include "base/source/timer.h"
#include "pluginterfaces/base/futils.h"
#include "pluginterfaces/gui/iplugview.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace SDL3Synth {

//------------------------------------------------------------------------
// Platform helpers
//------------------------------------------------------------------------
namespace {

constexpr int kEditorWidth = 1100;
constexpr int kEditorHeight = 760;

// Locate the plug-in bundle's Resources/data directory so the GUI can load its
// font. The VST3 bundle layout is <bundle>/Contents/<arch>/<lib> with resources
// in <bundle>/Contents/Resources.
std::string findAssetDirectory ()
{
	std::string exePath;

#if defined(_WIN32)
	HMODULE module = nullptr;
	if (!GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                         reinterpret_cast<LPCSTR> (&findAssetDirectory), &module))
		return "data";
	char path[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameA (module, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return "data";
	exePath.assign (path, length);
	const size_t contentsPos = exePath.find ("\\Contents\\");
	if (contentsPos == std::string::npos)
		return "data";
	return exePath.substr (0, contentsPos) + "\\Contents\\Resources\\data";
#else
	Dl_info info;
	if (dladdr (reinterpret_cast<const void*> (&findAssetDirectory), &info) == 0 ||
	    !info.dli_fname)
		return "data";
	exePath = info.dli_fname;
	const size_t contentsPos = exePath.find ("/Contents/");
	if (contentsPos == std::string::npos)
		return "data";
	return exePath.substr (0, contentsPos) + "/Contents/Resources/data";
#endif
}

// Keep SDL's X11 backend from trying to fall back to Wayland: the host supplies
// an X11 window id, which only the X11 backend understands.
struct X11DriverHint
{
	X11DriverHint ()
	{
#if defined(__linux__) && !defined(__ANDROID__)
		SDL_SetHint (SDL_HINT_VIDEO_DRIVER, "x11");
#endif
	}
};
X11DriverHint gX11DriverHint;

struct Attempt
{
	int major;
	int minor;
	int profile;
	// GLSL version handed to ImGui. The host decides which visual/context we
	// end up with, so each attempt carries a shader version it can render.
	const char* glslVersion;
};

const Attempt kGlAttempts[] = {
    {3, 2, SDL_GL_CONTEXT_PROFILE_CORE, "#version 150"},
    {3, 0, 0, "#version 130"},
    {2, 1, 0, "#version 120"},
};

} // namespace

//------------------------------------------------------------------------
SynthEditor::SynthEditor (SynthVst3& proc)
	: processor (&proc), engine (proc.getEnginePtr ())
{
	rect.left = 0;
	rect.top = 0;
	rect.right = kEditorWidth;
	rect.bottom = kEditorHeight;
}

//------------------------------------------------------------------------
void SynthEditor::detachProcessor ()
{
	processor = nullptr;
}

//------------------------------------------------------------------------
SynthEditor::~SynthEditor ()
{
	// The frame loop has to be stopped before the window and the OpenGL
	// context go away, otherwise the timer thread would use freed memory.
	stopTimer ();
	closeWindow ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthEditor::isPlatformTypeSupported (FIDString type)
{
	if (!type)
		return kResultFalse;
	if (FIDStringsEqual (type, kPlatformTypeX11EmbedWindowID) ||
	    FIDStringsEqual (type, kPlatformTypeHWND) || FIDStringsEqual (type, kPlatformTypeNSView))
		return kResultTrue;
	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthEditor::setFrame (IPlugFrame* frame)
{
	CPluginView::setFrame (frame);
	return kResultTrue;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthEditor::getSize (ViewRect* size)
{
	if (!size)
		return kInvalidArgument;
	*size = rect;
	return kResultTrue;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthEditor::attached (void* parent, FIDString type)
{
	if (systemWindow)
		return kResultFalse; // already attached

	if (!parent)
		return kInvalidArgument;

	if (!openWindow (parent, type))
		return kResultFalse;

	systemWindow = parent;
	startTimer ();
	return kResultTrue;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthEditor::removed ()
{
	stopTimer ();
	closeWindow ();
	systemWindow = nullptr;
	return kResultTrue;
}

//------------------------------------------------------------------------
bool SynthEditor::openWindow (void* parent, FIDString type)
{
	// The host may already have the video subsystem up (it usually has not),
	// so remember whether we are the ones who started it.
	SDL_InitSubSystem (SDL_INIT_VIDEO);

	SDL_PropertiesID props = SDL_CreateProperties ();
	SDL_SetStringProperty (props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "SDL3 Synth");
	SDL_SetNumberProperty (props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, kEditorWidth);
	SDL_SetNumberProperty (props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, kEditorHeight);
	SDL_SetBooleanProperty (props, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);

	if (FIDStringsEqual (type, kPlatformTypeX11EmbedWindowID))
	{
		SDL_SetNumberProperty (props, SDL_PROP_WINDOW_CREATE_X11_WINDOW_NUMBER,
		                       static_cast<Sint64> (reinterpret_cast<uintptr_t> (parent)));
	}
	else if (FIDStringsEqual (type, kPlatformTypeHWND))
	{
		SDL_SetPointerProperty (props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, parent);
	}
	else if (FIDStringsEqual (type, kPlatformTypeNSView))
	{
		SDL_SetPointerProperty (props, SDL_PROP_WINDOW_CREATE_COCOA_VIEW_POINTER, parent);
	}
	else
	{
		SDL_DestroyProperties (props);
		return false;
	}

	window = SDL_CreateWindowWithProperties (props);
	SDL_DestroyProperties (props);
	if (!window)
		return false;

	// Try a small ladder of GL versions. The embedded window's visual is
	// controlled by the host, so a core profile context is not guaranteed.
	glContext = nullptr;
	const char* glslVersion = kGlAttempts[0].glslVersion;
	for (const Attempt& attempt : kGlAttempts)
	{
		SDL_GL_SetAttribute (SDL_GL_CONTEXT_FLAGS, 0);
		SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK, attempt.profile);
		SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, attempt.major);
		SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, attempt.minor);
		SDL_GL_SetAttribute (SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 24);
		SDL_GL_SetAttribute (SDL_GL_STENCIL_SIZE, 8);
		SDL_ClearError ();
		glContext = SDL_GL_CreateContext (window);
		if (glContext)
		{
			glslVersion = attempt.glslVersion;
			break;
		}
	}

	if (!glContext)
	{
		SDL_DestroyWindow (window);
		window = nullptr;
		return false;
	}

	SDL_GL_MakeCurrent (window, glContext);
	SDL_GL_SetSwapInterval (1);

	gui = std::make_unique<SynthGui> (*engine, window, false);
	gui->setAssetDirectory (findAssetDirectory ());
	if (!gui->init (glslVersion))
	{
		gui.reset ();
		SDL_GL_DestroyContext (glContext);
		glContext = nullptr;
		SDL_DestroyWindow (window);
		window = nullptr;
		return false;
	}

	// Keep the editor in sync with the current engine state.
	gui->statusMessage = "SDL3 Synth";

	// Wire editor edits back to the host as automation gestures.
	SynthEngine& synth = *engine;
	synth.knobs.onEditBegin = [this] (int index) {
		if (processor)
			processor->onEditorEditBegin (static_cast<ParamID> (index));
	};
	synth.knobs.onEditEnd = [this] (int index) {
		if (processor)
			processor->onEditorEditEnd (static_cast<ParamID> (index));
	};
	synth.knobs.onGuiParameterChanged = [this, &synth] (int index) {
		if (!processor)
			return;
		processor->onEditorParameterChanged (static_cast<ParamID> (index),
		                                    static_cast<ParamValue> (synth.getParameterNormalized (index)));
	};

	// Release the OpenGL context so that the frame-loop thread (the host's UI
	// thread or the worker thread) can make it current. Otherwise the context
	// stays current on this thread and the frame loop cannot draw.
	if (window && glContext)
		SDL_GL_MakeCurrent (window, nullptr);

	return true;
}

//------------------------------------------------------------------------
void SynthEditor::closeWindow ()
{
	SynthEngine& synth = *engine;
	synth.knobs.onEditBegin = nullptr;
	synth.knobs.onEditEnd = nullptr;
	synth.knobs.onGuiParameterChanged = nullptr;

	if (gui)
	{
		// The GUI teardown deletes OpenGL objects, so the context has to be
		// current on this thread. This is not the case when the host tears
		// the plug-in down from another thread, in which case the objects are
		// left to the driver, which frees them with the context.
		if (window && glContext)
			SDL_GL_MakeCurrent (window, glContext);

		gui->shutdown ();
		gui.reset ();
	}
	if (glContext)
	{
		SDL_GL_DestroyContext (glContext);
		glContext = nullptr;
	}
	if (window)
	{
		// SDL never destroys an externally owned window.
		SDL_DestroyWindow (window);
		window = nullptr;
	}
}

//------------------------------------------------------------------------
void SynthEditor::startTimer ()
{
	// Fall back to the SDK timer where the platform supports it.
	timer = Timer::create (this, 1000 / 60);
	if (timer)
		return;

	// Last resort: drive the frames from a worker thread.
	workerRunning = true;
	workerThread = std::thread (threadMain, this);
}

//------------------------------------------------------------------------
void SynthEditor::stopTimer ()
{
	if (timer)
	{
		timer->stop ();
		timer->release ();
		timer = nullptr;
	}

	if (workerRunning.exchange (false))
	{
		if (workerThread.joinable ())
			workerThread.join ();
	}
}

//------------------------------------------------------------------------
void SynthEditor::threadMain (SynthEditor* self)
{
	while (self->workerRunning.load ())
	{
		self->tick ();
		SDL_Delay (1000 / 60);
	}

	// Release the OpenGL context, otherwise the thread that destroys the
	// editor cannot make it current again.
	if (self->window && self->glContext)
		SDL_GL_MakeCurrent (self->window, nullptr);
}

//------------------------------------------------------------------------
void SynthEditor::onTimer (Timer* /*timer*/)
{
	tick ();
}



//------------------------------------------------------------------------
void SynthEditor::tick ()
{
	std::lock_guard<std::mutex> lock (tickMutex);
	if (!window || !glContext || !gui)
		return;

	SDL_PumpEvents ();

	SDL_Event event;
	while (SDL_PollEvent (&event))
	{
		// Closing the editor is the host's decision; ignore requests that would
		// otherwise tear the window down from underneath us.
		if (event.type == SDL_EVENT_QUIT)
			continue;
		if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
		    event.window.windowID == SDL_GetWindowID (window))
			continue;
		gui->processEvent (event);
	}

	SDL_GL_MakeCurrent (window, glContext);
	gui->newFrame ();
	gui->draw ();
	gui->render ();
	SDL_GL_SwapWindow (window);
}

} // namespace SDL3Synth
