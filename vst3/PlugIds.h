#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace SDL3Synth {

// {53444C33-5359-4E54-5653-5433504C5547}
static const Steinberg::FUID kSynthVst3ProcessorUID (0x53444C33, 0x53594E54, 0x56535433,
                                                     0x504C5547);
// ViewType::kEditor ("editor") is what hosts ask for via createView().
static constexpr const char* kViewName = "editor";

} // namespace SDL3Synth
