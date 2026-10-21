//------------------------------------------------------------------------
// KT Delay - audio processor
// Copyright (c) 2026 aike. SPDX-License-Identifier: MIT
//------------------------------------------------------------------------
#include "ktdelayprocessor.h"
#include "ktdelaycids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace KT {

namespace {
// 100 % on the knob maps to this feedback gain so that the loop stays stable.
constexpr double kMaxFeedbackGain = 0.98;
// Ducking: the dry envelope (dBFS) is mapped linearly from kDuckLowDb (no ducking)
// to kDuckHighDb (full ducking).
constexpr double kDuckLowDb = -40.0;
constexpr double kDuckHighDb = -10.0;

inline double onePoleCoef (double timeSec, double sampleRate)
{
	if (timeSec <= 0.0 || sampleRate <= 0.0)
		return 0.0;
	return std::exp (-1.0 / (timeSec * sampleRate));
}
} // anonymous

//------------------------------------------------------------------------
KTDelayProcessor::KTDelayProcessor ()
{
	setControllerClass (kKTDelayControllerUID);
}

//------------------------------------------------------------------------
KTDelayProcessor::~KTDelayProcessor () = default;

//------------------------------------------------------------------------
tresult PLUGIN_API KTDelayProcessor::initialize (FUnknown* context)
{
	tresult result = AudioEffect::initialize (context);
	if (result != kResultOk)
		return result;

	addAudioInput (STR16 ("Stereo In"), SpeakerArr::kStereo);
	addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API KTDelayProcessor::terminate ()
{
	mBuffer[0].clear ();
	mBuffer[1].clear ();
	return AudioEffect::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API KTDelayProcessor::setActive (TBool state)
{
	if (state)
		resetState ();
	return AudioEffect::setActive (state);
}

//------------------------------------------------------------------------
tresult PLUGIN_API KTDelayProcessor::setupProcessing (ProcessSetup& newSetup)
{
	mSampleRate = newSetup.sampleRate > 0.0 ? newSetup.sampleRate : 44100.0;

	// max delay + a little headroom for the interpolation read
	mBufferSize = static_cast<int32> (std::ceil (kMaxDelayMs * 0.001 * mSampleRate)) + 8;
	mBuffer[0].assign (mBufferSize, 0.f);
	mBuffer[1].assign (mBufferSize, 0.f);

	updateCoefficients ();
	resetState ();

	return AudioEffect::setupProcessing (newSetup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API KTDelayProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
	if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
		return kResultTrue;
	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API KTDelayProcessor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                         SpeakerArrangement* outputs, int32 numOuts)
{
	// stereo in / stereo out only
	if (numIns == 1 && numOuts == 1 && inputs[0] == SpeakerArr::kStereo &&
	    outputs[0] == SpeakerArr::kStereo)
	{
		return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
	}
	return kResultFalse;
}

//------------------------------------------------------------------------
uint32 PLUGIN_API KTDelayProcessor::getTailSamples ()
{
	// with feedback the tail can be very long
	return kInfiniteTail;
}

//------------------------------------------------------------------------
void KTDelayProcessor::updateCoefficients ()
{
	mSmoothCoef = onePoleCoef (0.005, mSampleRate);       // 5 ms  (gains)
	mDelaySmoothCoef = onePoleCoef (0.030, mSampleRate);  // 30 ms (delay time glide)
	mEnvAttackCoef = onePoleCoef (0.003, mSampleRate);    // 3 ms
	mEnvReleaseCoef = onePoleCoef (0.200, mSampleRate);   // 200 ms
	mDelaySamplesTarget = mDelayTimeNorm * kMaxDelayMs * 0.001 * mSampleRate;
}

//------------------------------------------------------------------------
void KTDelayProcessor::resetState ()
{
	std::fill (mBuffer[0].begin (), mBuffer[0].end (), 0.f);
	std::fill (mBuffer[1].begin (), mBuffer[1].end (), 0.f);
	mWritePos = 0;
	mEnvelope = 0.0;

	mDelaySamplesTarget = mDelayTimeNorm * kMaxDelayMs * 0.001 * mSampleRate;
	mDelaySamplesCurrent = mDelaySamplesTarget;
	mFeedbackCurrent = mFeedbackNorm * kMaxFeedbackGain;
	mWetCurrent = mWetLevelNorm;
	mDuckCurrent = mDuckingNorm;
	mBypassMix = mBypass ? 1.0 : 0.0;
}

//------------------------------------------------------------------------
void KTDelayProcessor::handleParameterChanges (IParameterChanges* changes)
{
	if (!changes)
		return;

	int32 numParams = changes->getParameterCount ();
	for (int32 i = 0; i < numParams; ++i)
	{
		IParamValueQueue* queue = changes->getParameterData (i);
		if (!queue)
			continue;

		int32 numPoints = queue->getPointCount ();
		if (numPoints <= 0)
			continue;

		ParamValue value = 0.0;
		int32 sampleOffset = 0;
		if (queue->getPoint (numPoints - 1, sampleOffset, value) != kResultTrue)
			continue;

		switch (queue->getParameterId ())
		{
			case kDelayTimeId:
				mDelayTimeNorm = value;
				mDelaySamplesTarget = mDelayTimeNorm * kMaxDelayMs * 0.001 * mSampleRate;
				break;
			case kFeedbackId: mFeedbackNorm = value; break;
			case kWetLevelId: mWetLevelNorm = value; break;
			case kDuckingId: mDuckingNorm = value; break;
			case kBypassId: mBypass = value > 0.5; break;
			default: break;
		}
	}
}

//------------------------------------------------------------------------
template <typename SampleType>
void KTDelayProcessor::processAudio (SampleType** in, SampleType** out, int32 numChannels,
                                     int32 numSamples)
{
	if (mBufferSize < 4)
	{
		// not prepared: pass through
		for (int32 ch = 0; ch < numChannels; ++ch)
			if (in[ch] != out[ch])
				std::copy (in[ch], in[ch] + numSamples, out[ch]);
		return;
	}

	float* bufL = mBuffer[0].data ();
	float* bufR = mBuffer[1].data ();
	const int32 size = mBufferSize;
	const double maxDelay = static_cast<double> (size - 3);

	const double gSmooth = 1.0 - mSmoothCoef;
	const double gDelay = 1.0 - mDelaySmoothCoef;
	const double gAtt = 1.0 - mEnvAttackCoef;
	const double gRel = 1.0 - mEnvReleaseCoef;
	const double invDuckRange = 1.0 / (kDuckHighDb - kDuckLowDb);

	const double feedbackTarget = mFeedbackNorm * kMaxFeedbackGain;
	const double bypassTarget = mBypass ? 1.0 : 0.0;

	SampleType* inL = in[0];
	SampleType* inR = numChannels > 1 ? in[1] : in[0];
	SampleType* outL = out[0];
	SampleType* outR = numChannels > 1 ? out[1] : nullptr;

	for (int32 i = 0; i < numSamples; ++i)
	{
		//--- smooth parameters ---------------------------------------------
		mDelaySamplesCurrent += (mDelaySamplesTarget - mDelaySamplesCurrent) * gDelay;
		mFeedbackCurrent += (feedbackTarget - mFeedbackCurrent) * gSmooth;
		mWetCurrent += (mWetLevelNorm - mWetCurrent) * gSmooth;
		mDuckCurrent += (mDuckingNorm - mDuckCurrent) * gSmooth;
		mBypassMix += (bypassTarget - mBypassMix) * gSmooth;

		const double dryL = static_cast<double> (inL[i]);
		const double dryR = static_cast<double> (inR[i]);

		//--- dry envelope follower (for ducking) -----------------------------
		const double peak = std::max (std::fabs (dryL), std::fabs (dryR));
		if (peak > mEnvelope)
			mEnvelope += (peak - mEnvelope) * gAtt;
		else
			mEnvelope += (peak - mEnvelope) * gRel;

		double loudness = 0.0;
		if (mEnvelope > 1.0e-5)
		{
			const double db = 20.0 * std::log10 (mEnvelope);
			loudness = (db - kDuckLowDb) * invDuckRange;
			loudness = std::min (1.0, std::max (0.0, loudness));
		}
		const double duckGain = 1.0 - mDuckCurrent * loudness;

		//--- read delayed sample (linear interpolation) ----------------------
		double d = std::min (maxDelay, std::max (1.0, mDelaySamplesCurrent));
		double readPos = static_cast<double> (mWritePos) - d;
		if (readPos < 0.0)
			readPos += size;
		int32 idx0 = static_cast<int32> (readPos);
		if (idx0 >= size)
			idx0 -= size;
		int32 idx1 = idx0 + 1;
		if (idx1 >= size)
			idx1 -= size;
		const double frac = readPos - std::floor (readPos);

		const double delL = bufL[idx0] + (bufL[idx1] - bufL[idx0]) * frac;
		const double delR = bufR[idx0] + (bufR[idx1] - bufR[idx0]) * frac;

		//--- write input + feedback into the delay line ----------------------
		double wL = dryL + delL * mFeedbackCurrent;
		double wR = dryR + delR * mFeedbackCurrent;
		// denormal / tiny value flush
		if (std::fabs (wL) < 1.0e-20)
			wL = 0.0;
		if (std::fabs (wR) < 1.0e-20)
			wR = 0.0;
		bufL[mWritePos] = static_cast<float> (wL);
		bufR[mWritePos] = static_cast<float> (wR);
		if (++mWritePos >= size)
			mWritePos = 0;

		//--- mix ----------------------------------------------------------------
		const double wetGain = mWetCurrent * duckGain * (1.0 - mBypassMix);
		outL[i] = static_cast<SampleType> (dryL + delL * wetGain);
		if (outR)
			outR[i] = static_cast<SampleType> (dryR + delR * wetGain);
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API KTDelayProcessor::process (ProcessData& data)
{
	handleParameterChanges (data.inputParameterChanges);

	if (data.numInputs == 0 || data.numOutputs == 0 || data.numSamples <= 0)
		return kResultOk;

	AudioBusBuffers& inBus = data.inputs[0];
	AudioBusBuffers& outBus = data.outputs[0];
	const int32 numChannels = std::min (inBus.numChannels, outBus.numChannels);
	if (numChannels <= 0)
		return kResultOk;

	if (data.symbolicSampleSize == kSample32)
		processAudio<Sample32> (inBus.channelBuffers32, outBus.channelBuffers32, numChannels,
		                        data.numSamples);
	else
		processAudio<Sample64> (inBus.channelBuffers64, outBus.channelBuffers64, numChannels,
		                        data.numSamples);

	outBus.silenceFlags = 0;
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API KTDelayProcessor::setState (IBStream* state)
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

	mDelayTimeNorm = delayTime;
	mFeedbackNorm = feedback;
	mWetLevelNorm = wet;
	mDuckingNorm = ducking;
	mBypass = bypass != 0;
	mDelaySamplesTarget = mDelayTimeNorm * kMaxDelayMs * 0.001 * mSampleRate;

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API KTDelayProcessor::getState (IBStream* state)
{
	if (!state)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);
	streamer.writeFloat (static_cast<float> (mDelayTimeNorm));
	streamer.writeFloat (static_cast<float> (mFeedbackNorm));
	streamer.writeFloat (static_cast<float> (mWetLevelNorm));
	streamer.writeFloat (static_cast<float> (mDuckingNorm));
	streamer.writeInt32 (mBypass ? 1 : 0);
	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace KT
