#pragma once

#include "public.sdk/source/common/pluginview.h"

#include "base/source/timer.h"
#include "pluginterfaces/gui/iplugview.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

struct SDL_Window;
struct SDL_GLContextState;
class SynthGui;
class SynthEngine;

namespace SDL3Synth {

class SynthVst3;

//------------------------------------------------------------------------
/** VST3 editor view.
 *
 * The SDL GUI is embedded into a window supplied by the host. On every platform
 * SDL can adopt an existing native window, so no reparenting is required:
 *  - Linux   : the host X11 window id (kPlatformTypeX11EmbedWindowID)
 *  - Windows : the host HWND (kPlatformTypeHWND)
 *  - macOS   : the host NSView (kPlatformTypeNSView)
 *
 * A timer (or, on Linux without a run loop, a worker thread) drives the frame
 * loop from the host's UI thread.
 */
class SynthEditor : public Steinberg::CPluginView,
                    public Steinberg::ITimerCallback,
                    public Steinberg::Linux::ITimerHandler
{
public:
	explicit SynthEditor (SynthVst3& processor);
	~SynthEditor () SMTG_OVERRIDE;

	//---from IPlugView-------------------------------------------------------
	Steinberg::tresult PLUGIN_API isPlatformTypeSupported (Steinberg::FIDString type) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API attached (void* parent, Steinberg::FIDString type) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API removed () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getSize (Steinberg::ViewRect* size) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API canResize () SMTG_OVERRIDE { return Steinberg::kResultFalse; }
	Steinberg::tresult PLUGIN_API setFrame (Steinberg::IPlugFrame* frame) SMTG_OVERRIDE;

	//---from Steinberg::ITimerCallback (framebuffer driven by a Timer)--------
	void onTimer (Steinberg::Timer* timer) SMTG_OVERRIDE;

	//---from Steinberg::Linux::ITimerHandler---------------------------------
	void PLUGIN_API onTimer () SMTG_OVERRIDE;

	// Called by the processor before it is destroyed: from then on the editor
	// must not talk to the processor anymore. The engine stays alive because
	// the editor holds a reference to it.
	void detachProcessor ();

	//---Interface------------------------------------------------------------
	OBJ_METHODS (SynthEditor, CPluginView)
	DEFINE_INTERFACES
		DEF_INTERFACE (Steinberg::Linux::ITimerHandler)
	END_DEFINE_INTERFACES (CPluginView)
	REFCOUNT_METHODS (CPluginView)

private:
	bool openWindow (void* parent, Steinberg::FIDString type);
	void closeWindow ();
	void tick ();
	void startTimer ();
	void stopTimer ();
	static void threadMain (SynthEditor* self);

	// The processor is null as soon as it has been destroyed.
	SynthVst3* processor {nullptr};
	// The engine is shared with the processor so that it outlives it.
	std::shared_ptr<SynthEngine> engine;
	std::unique_ptr<SynthGui> gui;

	SDL_Window* window {nullptr};
	SDL_GLContextState* glContext {nullptr};
	std::string assetDirectory;

	// Timer strategies (only one is used at a time).
	Steinberg::Timer* timer {nullptr};
	Steinberg::IPtr<Steinberg::Linux::IRunLoop> runLoop;
	bool runLoopTimerRegistered {false};
	std::thread workerThread;
	std::atomic<bool> workerRunning {false};

	std::mutex tickMutex;
};

} // namespace SDL3Synth
