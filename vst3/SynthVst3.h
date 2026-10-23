#pragma once

// Must always come first: it defines the setState -> setEditorState workaround
// used by the SingleComponentEffect inheritance below.
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

class SynthEngine;

namespace SDL3Synth {

class SynthEditor;

//------------------------------------------------------------------------
/** SDL3 Synth as a single component VST3 instrument.
 *
 * Audio processing and the parameter model are provided by the shared
 * SynthEngine. The editor (SynthEditor) embeds a SynthGui into a host supplied
 * window.
 */
class SynthVst3 : public Steinberg::Vst::SingleComponentEffect,
                  public Steinberg::Vst::IMidiMapping
{
public:
	SynthVst3 ();
	~SynthVst3 () SMTG_OVERRIDE;

	static Steinberg::FUnknown* createInstance (void* /*context*/)
	{
		return static_cast<Steinberg::Vst::IAudioProcessor*> (new SynthVst3);
	}

	//---from IPluginBase-----------------------------------------------------
	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;

	//---from IComponent------------------------------------------------------
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	//---from IAudioProcessor-------------------------------------------------
	Steinberg::tresult PLUGIN_API setBusArrangements (
	    Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
	    Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
	Steinberg::uint32 PLUGIN_API getLatencySamples () SMTG_OVERRIDE { return 0; }
	Steinberg::uint32 PLUGIN_API getTailSamples () SMTG_OVERRIDE;

	//---from IEditController-------------------------------------------------
	Steinberg::tresult PLUGIN_API setParamNormalized (Steinberg::Vst::ParamID tag,
	                                                  Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;
	Steinberg::Vst::ParamValue PLUGIN_API getParamNormalized (Steinberg::Vst::ParamID tag) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE;

	//---from IMidiMapping----------------------------------------------------
	Steinberg::tresult PLUGIN_API getMidiControllerAssignment (
	    Steinberg::int32 busIndex, Steinberg::int16 channel,
	    Steinberg::Vst::CtrlNumber midiControllerNumber,
	    Steinberg::Vst::ParamID& tag) SMTG_OVERRIDE;

	//---Interface------------------------------------------------------------
	OBJ_METHODS (SynthVst3, SingleComponentEffect)
	Steinberg::tresult PLUGIN_API queryInterface (const Steinberg::TUID iid,
	                                              void** obj) SMTG_OVERRIDE;
	REFCOUNT_METHODS (SingleComponentEffect)

	//---Internal-------------------------------------------------------------
	SynthEngine& getEngine () { return *engine; }
	std::shared_ptr<SynthEngine> getEnginePtr () { return engine; }

	// Called from the editor when the user moves a knob.
	void onEditorEditBegin (Steinberg::Vst::ParamID tag);
	void onEditorEditEnd (Steinberg::Vst::ParamID tag);
	void onEditorParameterChanged (Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value);

private:
	void registerParameters ();
	void applyPendingParameterChanges (Steinberg::Vst::IParameterChanges* changes);
	void applyInputEvents (Steinberg::Vst::IEventList* events);
	Steinberg::tresult writeState (Steinberg::IBStream* state);
	Steinberg::tresult readState (Steinberg::IBStream* state);

	std::shared_ptr<SynthEngine> engine;
	SynthEditor* editor {nullptr};

	// Parameter values pushed by the engine when a MIDI CC or a host automation
	// value changes one of the engine parameters from the audio thread.
	std::vector<std::pair<Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue>> pendingOutputChanges;
	std::mutex pendingOutputChangesMutex;

	Steinberg::int32 maxSamplesPerBlock {0};
	std::vector<float> scratchLeft;
	std::vector<float> scratchRight;
};

} // namespace SDL3Synth
