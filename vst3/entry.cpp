//------------------------------------------------------------------------
// SDL3 Synth VST3 factory entry point.
//------------------------------------------------------------------------
#include "PlugIds.h"
#include "SynthVst3.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

#define stringPluginName "SDL3 Synth"

BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2 (INLINE_UID_FROM_FUID (SDL3Synth::kSynthVst3ProcessorUID),
            PClassInfo::kManyInstances, // cardinality
            kVstAudioEffectClass,       // component category
            stringPluginName,           // plug-in name
            0,                          // class flags (single component plug-in)
            PlugType::kInstrumentSynth, // subcategory
            FULL_VERSION_STR,           // version
            kVstVersionString,          // SDK version
            SDL3Synth::SynthVst3::createInstance)

END_FACTORY
