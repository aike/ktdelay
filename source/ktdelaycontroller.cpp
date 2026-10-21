//------------------------------------------------------------------------
// KT Delay - edit controller
// Copyright (c) 2026 aike. SPDX-License-Identifier: MIT
//------------------------------------------------------------------------
#include "ktdelaycontroller.h"
#include "ktdelaycids.h"
#include "ktdelayeditor.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace KT {

//------------------------------------------------------------------------
tresult PLUGIN_API KTDelayController::initialize (FUnknown* context)
{
	tresult result = EditController::initialize (context);
	if (result != kResultOk)
		return result;

	// Delay Time: 0 .. 400 ms
	auto* delayParam = new RangeParameter (STR16 ("Delay Time"), kDelayTimeId, STR16 ("ms"), 0.0,
	                                       kMaxDelayMs, kDefaultDelayMs, 0,
	                                       ParameterInfo::kCanAutomate);
	delayParam->setPrecision (0);
	parameters.addParameter (delayParam);

	// Feedback: 0 .. 100 %
	auto* feedbackParam = new RangeParameter (STR16 ("Feedback"), kFeedbackId, STR16 ("%"), 0.0,
	                                          100.0, kDefaultFeedback * 100.0, 0,
	                                          ParameterInfo::kCanAutomate);
	feedbackParam->setPrecision (0);
	parameters.addParameter (feedbackParam);

	// Wet Level: 0 .. 100 %
	auto* wetParam = new RangeParameter (STR16 ("Wet Level"), kWetLevelId, STR16 ("%"), 0.0,
	                                     100.0, kDefaultWetLevel * 100.0, 0,
	                                     ParameterInfo::kCanAutomate);
	wetParam->setPrecision (0);
	parameters.addParameter (wetParam);

	// Ducking: 0 .. 100 %
	auto* duckParam = new RangeParameter (STR16 ("Ducking"), kDuckingId, STR16 ("%"), 0.0, 100.0,
	                                      kDefaultDucking * 100.0, 0,
	                                      ParameterInfo::kCanAutomate);
	duckParam->setPrecision (0);
	parameters.addParameter (duckParam);

	// Bypass
	parameters.addParameter (STR16 ("Bypass"), nullptr, 1, 0.0,
	                         ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, kBypassId);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API KTDelayController::terminate ()
{
	mEditors.clear ();
	return EditController::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API KTDelayController::setComponentState (IBStream* state)
{
	if (!state)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	float delayTime = 0.f, feedback = 0.f, wet = 0.f, ducking = 0.f;
	int32 bypass = 0;
	if (!streamer.readFloat (delayTime))
		return kResultFalse;
	if (!streamer.readFloat (feedback))
		return kResultFalse;
	if (!streamer.readFloat (wet))
		return kResultFalse;
	if (!streamer.readFloat (ducking))
		return kResultFalse;
	if (!streamer.readInt32 (bypass))
		return kResultFalse;

	setParamNormalized (kDelayTimeId, delayTime);
	setParamNormalized (kFeedbackId, feedback);
	setParamNormalized (kWetLevelId, wet);
	setParamNormalized (kDuckingId, ducking);
	setParamNormalized (kBypassId, bypass ? 1.0 : 0.0);

	return kResultOk;
}

//------------------------------------------------------------------------
IPlugView* PLUGIN_API KTDelayController::createView (FIDString name)
{
	if (FIDStringsEqual (name, ViewType::kEditor))
		return new KTDelayEditor (this);
	return nullptr;
}

//------------------------------------------------------------------------
tresult PLUGIN_API KTDelayController::setParamNormalized (ParamID tag, ParamValue value)
{
	tresult result = EditController::setParamNormalized (tag, value);
	if (result == kResultOk)
	{
		for (auto* editor : mEditors)
			editor->update (tag, value);
	}
	return result;
}

//------------------------------------------------------------------------
void KTDelayController::addEditor (KTDelayEditor* editor)
{
	if (std::find (mEditors.begin (), mEditors.end (), editor) == mEditors.end ())
		mEditors.push_back (editor);
}

//------------------------------------------------------------------------
void KTDelayController::removeEditor (KTDelayEditor* editor)
{
	mEditors.erase (std::remove (mEditors.begin (), mEditors.end (), editor), mEditors.end ());
}

//------------------------------------------------------------------------
void KTDelayController::formatDelayTime (ParamValue normalized, char* buffer, size_t size)
{
	const double ms = std::min (1.0, std::max (0.0, normalized)) * kMaxDelayMs;
	std::snprintf (buffer, size, "%.0f ms", ms);
}

//------------------------------------------------------------------------
} // namespace KT
