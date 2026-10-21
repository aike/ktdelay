//------------------------------------------------------------------------
// KT Delay - class IDs and parameter IDs
// Copyright (c) 2026 aike. SPDX-License-Identifier: MIT
//------------------------------------------------------------------------
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace KT {

//------------------------------------------------------------------------
// Unique class IDs (processor / controller)
//------------------------------------------------------------------------
static const Steinberg::FUID kKTDelayProcessorUID (0xE4B7EC89, 0x0B0F489D, 0xB6EE9737, 0xC605845F);
static const Steinberg::FUID kKTDelayControllerUID (0x6536582B, 0x4CF6425C, 0x80DEDD3D, 0xA62A12AC);

#define KTDelayVST3Category "Fx|Delay"

//------------------------------------------------------------------------
// Parameter IDs
//------------------------------------------------------------------------
enum KTDelayParams : Steinberg::Vst::ParamID
{
	kDelayTimeId = 0, // 0 .. 400 ms
	kFeedbackId = 1,  // 0 .. 100 %
	kWetLevelId = 2,  // 0 .. 100 %
	kDuckingId = 3,   // 0 .. 100 %
	kBypassId = 4,    // on / off

	kNumParams
};

//------------------------------------------------------------------------
// Parameter ranges / defaults (plain values)
//------------------------------------------------------------------------
constexpr double kMaxDelayMs = 400.0;
constexpr double kDefaultDelayMs = 250.0;
constexpr double kDefaultFeedback = 0.30; // normalized
constexpr double kDefaultWetLevel = 0.50; // normalized
constexpr double kDefaultDucking = 0.0;   // normalized

//------------------------------------------------------------------------
} // namespace KT
