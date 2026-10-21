//------------------------------------------------------------------------
// KT Delay - audio processor
// Copyright (c) 2026 aike. SPDX-License-Identifier: MIT
//------------------------------------------------------------------------
#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include <vector>

namespace KT {

//------------------------------------------------------------------------
// Stereo delay with feedback, wet level, bypass and dry-signal ducking.
// The delayed signal is not filtered or degraded in any way.
//------------------------------------------------------------------------
class KTDelayProcessor : public Steinberg::Vst::AudioEffect
{
public:
	KTDelayProcessor ();
	~KTDelayProcessor () SMTG_OVERRIDE;

	static Steinberg::FUnknown* createInstance (void* /*context*/)
	{
		return (Steinberg::Vst::IAudioProcessor*)new KTDelayProcessor;
	}

	//--- AudioEffect overrides -----------------------------------------------
	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setBusArrangements (Steinberg::Vst::SpeakerArrangement* inputs,
	                                                  Steinberg::int32 numIns,
	                                                  Steinberg::Vst::SpeakerArrangement* outputs,
	                                                  Steinberg::int32 numOuts) SMTG_OVERRIDE;
	Steinberg::uint32 PLUGIN_API getLatencySamples () SMTG_OVERRIDE { return 0; }
	Steinberg::uint32 PLUGIN_API getTailSamples () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

private:
	template <typename SampleType>
	void processAudio (SampleType** in, SampleType** out, Steinberg::int32 numChannels,
	                   Steinberg::int32 numSamples);

	void handleParameterChanges (Steinberg::Vst::IParameterChanges* changes);
	void resetState ();
	void updateCoefficients ();

	//--- parameters (normalized 0..1, as set by host) ------------------------
	double mDelayTimeNorm = 250.0 / 400.0;
	double mFeedbackNorm = 0.30;
	double mWetLevelNorm = 0.50;
	double mDuckingNorm = 0.0;
	bool mBypass = false;

	//--- smoothed runtime values ---------------------------------------------
	double mDelaySamplesTarget = 0.0;
	double mDelaySamplesCurrent = 0.0;
	double mFeedbackCurrent = 0.0;
	double mWetCurrent = 0.0;
	double mDuckCurrent = 0.0;
	double mBypassMix = 0.0; // 0 = effect on, 1 = fully bypassed (smoothed)

	double mSmoothCoef = 0.0;       // one-pole coefficient for parameter smoothing
	double mDelaySmoothCoef = 0.0;  // slower smoothing for the delay time (pitch artefacts)
	double mEnvAttackCoef = 0.0;    // ducking envelope follower
	double mEnvReleaseCoef = 0.0;
	double mEnvelope = 0.0;         // dry-signal envelope (linear peak)

	//--- delay lines -----------------------------------------------------------
	std::vector<float> mBuffer[2];
	Steinberg::int32 mBufferSize = 0;
	Steinberg::int32 mWritePos = 0;
	double mSampleRate = 44100.0;
};

//------------------------------------------------------------------------
} // namespace KT
