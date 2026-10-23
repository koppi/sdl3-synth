#include "SynthVst3.h"
#include "PlugIds.h"
#include "version.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstbus.h"
#include "public.sdk/source/vst/vstparameters.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/futils.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/vstpresetkeys.h"

#include "Engine.h"
#include "Preset.h"
#include "SynthEditor.h"

#include <algorithm>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace SDL3Synth {

//------------------------------------------------------------------------
// Parameter helpers
//------------------------------------------------------------------------
namespace {

ParamID paramIdForIndex (int index)
{
	return static_cast<ParamID> (index);
}

void setString (String128 dest, const std::string& src)
{
	UString (dest, str16BufferSize (String128)).fromAscii (src.c_str ());
}

} // namespace

//------------------------------------------------------------------------
// SynthVst3
//------------------------------------------------------------------------
SynthVst3::SynthVst3 ()
{
	engine = std::make_shared<SynthEngine> ();

	// One event input (MIDI) and one stereo audio output.
	addAudioOutput (STR16 ("Output"), SpeakerArr::kStereo);
	addEventInput (STR16 ("MIDI In"), 16);

	registerParameters ();

	// Feed host automation back to the engine and, for MIDI CC changes made on
	// the audio thread, queue parameter changes for the host.
	engine->setParameterChangedCallback (
	    [this] (int index, float /*normalized*/) {
		    std::lock_guard<std::mutex> lock (pendingOutputChangesMutex);
		    pendingOutputChanges.emplace_back (
		        paramIdForIndex (index),
		        static_cast<ParamValue> (engine->getParameterNormalized (index)));
	    });
}

//------------------------------------------------------------------------
SynthVst3::~SynthVst3 ()
{
	if (editor)
	{
		// The editor must not access the processor anymore, it can outlive it
		// when the host keeps a reference to the view.
		editor->detachProcessor ();
		editor->release ();
		editor = nullptr;
	}
}

//------------------------------------------------------------------------
void SynthVst3::registerParameters ()
{
	const int count = engine->getParameterCount ();
	for (int i = 0; i < count; ++i)
	{
		const SynthParameter& p = engine->getParameter (i);
		const ParamID tag = paramIdForIndex (i);

		// Hidden parameters (mod wheel / pitch bend) are automation targets for
		// MIDI mapping but must not appear as automatable controls.
		const int32 flags = p.hidden
		                        ? (ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly)
		                        : ParameterInfo::kCanAutomate;

		auto* param = new RangeParameter (
		    STR16 ("placeholder"), tag, nullptr, p.minValue, p.maxValue,
		    p.value ? *p.value : p.defaultValue, kStepCountContinuous, flags, kRootUnitId);
		setString (param->getInfo ().title, p.name);
		setString (param->getInfo ().units, p.units);
		parameters.addParameter (param);
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthVst3::initialize (FUnknown* context)
{
	tresult result = SingleComponentEffect::initialize (context);
	if (result != kResultOk)
		return result;

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthVst3::terminate ()
{
	return SingleComponentEffect::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthVst3::setActive (TBool state)
{
	if (state)
		engine->allNotesOff ();
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthVst3::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                  SpeakerArrangement* outputs, int32 numOuts)
{
	// Accept a single mono or stereo output and no audio input.
	if (numIns != 0)
		return kResultFalse;
	if (numOuts != 1)
		return kResultFalse;

	if (outputs[0] != SpeakerArr::kMono && outputs[0] != SpeakerArr::kStereo)
		return kResultFalse;

	AudioBus* bus = FCast<AudioBus> (audioOutputs.at (0));
	if (!bus)
		return kResultFalse;

	if (bus->getArrangement () != outputs[0])
	{
		bus->setArrangement (outputs[0]);
		// Keep the controller side in sync: the host may query the layout.
	}
	return kResultTrue;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthVst3::canProcessSampleSize (int32 symbolicSampleSize)
{
	if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
		return kResultTrue;
	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthVst3::setupProcessing (ProcessSetup& setup)
{
	tresult result = SingleComponentEffect::setupProcessing (setup);
	if (result != kResultOk)
		return result;

	engine->setSampleRate (static_cast<float> (setup.sampleRate));

	maxSamplesPerBlock = setup.maxSamplesPerBlock;
	scratchLeft.assign (static_cast<size_t> (maxSamplesPerBlock), 0.0f);
	scratchRight.assign (static_cast<size_t> (maxSamplesPerBlock), 0.0f);
	return kResultOk;
}

//------------------------------------------------------------------------
uint32 PLUGIN_API SynthVst3::getTailSamples ()
{
	// Reverb and delay tails; report the SDK's "infinite" marker so the host
	// keeps feeding us while voices ring out.
	return kInfiniteTail;
}

//------------------------------------------------------------------------
void SynthVst3::applyPendingParameterChanges (IParameterChanges* changes)
{
	if (!changes)
		return;

	std::vector<std::pair<ParamID, ParamValue>> pending;
	{
		std::lock_guard<std::mutex> lock (pendingOutputChangesMutex);
		pending.swap (pendingOutputChanges);
	}
	if (pending.empty ())
		return;

	int32 queueIndex = 0;
	IParamValueQueue* queue = changes->addParameterData (pending.front ().first, queueIndex);
	if (!queue)
		return;

	for (const auto& entry : pending)
	{
		int32 pointIndex = 0;
		queue->addPoint (0, entry.second, pointIndex);
	}
}

//------------------------------------------------------------------------
void SynthVst3::applyInputEvents (IEventList* events)
{
	if (!events)
		return;

	const int32 count = events->getEventCount ();
	for (int32 i = 0; i < count; ++i)
	{
		Event event;
		if (events->getEvent (i, event) != kResultOk)
			continue;

		switch (event.type)
		{
			case Event::kNoteOnEvent:
			{
				const uint8_t msg[3] = {0x90,
				                        static_cast<uint8_t> (event.noteOn.pitch & 0x7F),
				                        static_cast<uint8_t> (event.noteOn.velocity >= 0.5f
				                                                  ? 100
				                                                  : static_cast<int> (event.noteOn.velocity * 127.0f))};
				engine->handleMidiMessage (msg, 3);
				break;
			}
			case Event::kNoteOffEvent:
			{
				const uint8_t msg[3] = {0x80,
				                        static_cast<uint8_t> (event.noteOff.pitch & 0x7F), 0};
				engine->handleMidiMessage (msg, 3);
				break;
			}
			case Event::kDataEvent:
			{
				const DataEvent& data = event.data;
				if (data.bytes && data.size > 0)
					engine->handleMidiMessage (data.bytes, static_cast<int> (data.size));
				break;
			}
			case Event::kLegacyMIDICCOutEvent:
			{
				const LegacyMIDICCOutEvent& cc = event.midiCCOut;
				const int controller = cc.controlNumber;
				if (controller == kPitchBend)
				{
					const int value = (cc.value2 << 7) | cc.value;
					const uint8_t msg[3] = {0xE0, static_cast<uint8_t> (value & 0x7F),
					                        static_cast<uint8_t> ((value >> 7) & 0x7F)};
					engine->handleMidiMessage (msg, 3);
				}
				else
				{
					const int status = 0xB0 | (cc.channel & 0x0F);
					const uint8_t msg[3] = {static_cast<uint8_t> (status),
					                        static_cast<uint8_t> (controller & 0x7F),
					                        static_cast<uint8_t> (cc.value & 0x7F)};
					engine->handleMidiMessage (msg, 3);
				}
				break;
			}
			default:
				break;
		}
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthVst3::process (ProcessData& data)
{
	// 1. Host automation -> engine.
	if (data.inputParameterChanges)
	{
		const int32 queueCount = data.inputParameterChanges->getParameterCount ();
		for (int32 q = 0; q < queueCount; ++q)
		{
			IParamValueQueue* queue = data.inputParameterChanges->getParameterData (q);
			if (!queue)
				continue;
			const int32 pointCount = queue->getPointCount ();
			if (pointCount <= 0)
				continue;
			ParamValue value = 0.0;
			int32 sampleOffset = 0;
			// Use the most recent point in the queue.
			if (queue->getPoint (pointCount - 1, sampleOffset, value) == kResultOk)
				engine->setParameterNormalized (static_cast<int> (queue->getParameterId ()), value);
		}
	}

	// 2. MIDI events -> engine.
	applyInputEvents (data.inputEvents);

	// 3. Audio.
	const int32 numFrames = data.numSamples;
	if (numFrames > 0)
	{
		if (static_cast<int32> (scratchLeft.size ()) < numFrames)
		{
			scratchLeft.assign (static_cast<size_t> (numFrames), 0.0f);
			scratchRight.assign (static_cast<size_t> (numFrames), 0.0f);
		}

		engine->process (scratchLeft.data (), scratchRight.data (), numFrames);

		if (data.numOutputs > 0)
		{
			AudioBusBuffers& out = data.outputs[0];
			const int32 numChannels = out.numChannels;
			if (out.channelBuffers32)
			{
				float* left = out.channelBuffers32[0];
				float* right = numChannels > 1 ? out.channelBuffers32[1] : nullptr;
				if (left)
				{
					for (int32 i = 0; i < numFrames; ++i)
					{
						if (right)
						{
							left[i] = scratchLeft[i];
							right[i] = scratchRight[i];
						}
						else
						{
							left[i] = 0.5f * (scratchLeft[i] + scratchRight[i]);
						}
					}
				}
			}
			out.silenceFlags = 0;
		}
	}
	else if (data.numOutputs > 0)
	{
		data.outputs[0].silenceFlags = 0;
	}

	// 4. Engine side effects (MIDI CC automation) -> host.
	applyPendingParameterChanges (data.outputParameterChanges);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthVst3::setParamNormalized (ParamID tag, ParamValue value)
{
	tresult result = SingleComponentEffect::setParamNormalized (tag, value);
	if (result == kResultOk)
		engine->setParameterNormalized (static_cast<int> (tag), value);
	return result;
}

//------------------------------------------------------------------------
ParamValue PLUGIN_API SynthVst3::getParamNormalized (ParamID tag)
{
	return static_cast<ParamValue> (
	    engine->getParameterNormalized (static_cast<int> (tag)));
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthVst3::setComponentState (IBStream* state)
{
	return readState (state);
}

//------------------------------------------------------------------------
IPlugView* PLUGIN_API SynthVst3::createView (FIDString name)
{
	if (name && FIDStringsEqual (name, kViewName))
	{
		if (!editor)
		{
			editor = new SynthEditor (*this);
			editor->addRef ();
		}
		return editor;
	}
	return nullptr;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthVst3::getMidiControllerAssignment (int32 busIndex, int16 /*channel*/,
                                                           CtrlNumber midiControllerNumber,
                                                           ParamID& tag)
{
	// Only the first (and only) event bus exposes CC mappings.
	if (busIndex != 0)
		return kResultFalse;

	switch (midiControllerNumber)
	{
		case kCtrlVolume:
			tag = paramIdForIndex (0); // master_volume
			return kResultTrue;
		case kCtrlPan:
			tag = paramIdForIndex (1); // pan
			return kResultTrue;
		case kCtrlModWheel:
			tag = paramIdForIndex (engine->getParameterCount () - 2); // mod_wheel
			return kResultTrue;
		case kPitchBend:
			tag = paramIdForIndex (engine->getParameterCount () - 1); // pitch_bend
			return kResultTrue;
		default:
			break;
	}
	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthVst3::queryInterface (const TUID _iid, void** obj)
{
	DEF_INTERFACE (IMidiMapping)
	return SingleComponentEffect::queryInterface (_iid, obj);
}

//------------------------------------------------------------------------
// Editor -> host automation
//------------------------------------------------------------------------
void SynthVst3::onEditorEditBegin (ParamID tag)
{
	beginEdit (tag);
}

//------------------------------------------------------------------------
void SynthVst3::onEditorEditEnd (ParamID tag)
{
	endEdit (tag);
}

//------------------------------------------------------------------------
void SynthVst3::onEditorParameterChanged (ParamID tag, ParamValue value)
{
	performEdit (tag, value);
}

//------------------------------------------------------------------------
// State (Component + Controller share the same engine)
//------------------------------------------------------------------------
tresult SynthVst3::writeState (IBStream* state)
{
	if (!state)
		return kInvalidArgument;

	std::string json;
	Preset::saveToString (*engine, json);

	IBStreamer streamer (state, kLittleEndian);
	int32 size = static_cast<int32> (json.size ());
	if (!streamer.writeInt32 (size))
		return kInternalError;
	if (size > 0 && streamer.writeRaw (const_cast<char*> (json.data ()), size) != size)
		return kInternalError;
	return kResultOk;
}

//------------------------------------------------------------------------
tresult SynthVst3::readState (IBStream* state)
{
	if (!state)
		return kInvalidArgument;

	IBStreamer streamer (state, kLittleEndian);
	int32 size = 0;
	if (!streamer.readInt32 (size) || size < 0)
		return kInternalError;

	std::string json;
	json.resize (static_cast<size_t> (size));
	if (size > 0)
	{
		if (streamer.readRaw (&json[0], size) != size)
			return kInternalError;
	}

	Preset::loadFromString (*engine, json);

	// Refresh the controller's cached parameter values so the host sees the
	// loaded state reflected immediately.
	const int count = std::min (engine->getParameterCount (), getParameterCount ());
	for (int i = 0; i < count; ++i)
	{
		SingleComponentEffect::setParamNormalized (
		    paramIdForIndex (i),
		    static_cast<ParamValue> (engine->getParameterNormalized (i)));
	}
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthVst3::getState (IBStream* state)
{
	return writeState (state);
}

//------------------------------------------------------------------------
tresult PLUGIN_API SynthVst3::setState (IBStream* state)
{
	return readState (state);
}

} // namespace SDL3Synth
