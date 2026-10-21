//------------------------------------------------------------------------
// KT Delay - offline DSP sanity test (no host, no GUI)
// Copyright (c) 2026 aike. SPDX-License-Identifier: MIT
//
//   cmake -S . -B build -DKTDELAY_BUILD_TESTS=ON
//   cmake --build build --config Release --target ktdelay_dsptest
//   build\bin\Release\ktdelay_dsptest.exe
//------------------------------------------------------------------------
#include "ktdelayprocessor.h"
#include "ktdelaycids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "public.sdk/source/common/memorystream.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace KT;

static int gFailures = 0;
static int gChecks = 0;

static void check (bool ok, const char* what, double got, double expected, double tol)
{
	++gChecks;
	if (!ok)
		++gFailures;
	std::printf ("%s  %-58s got=% .5f  expected=% .5f  (tol %.4f)\n", ok ? "ok  " : "FAIL", what,
	             got, expected, tol);
}

static void expectNear (const char* what, double got, double expected, double tol)
{
	check (std::fabs (got - expected) <= tol, what, got, expected, tol);
}

//------------------------------------------------------------------------
template <typename T>
struct Plug
{
	IPtr<KTDelayProcessor> proc;
	double sr;
	int32 block;

	Plug (double sr_, int32 block_, float delayNorm, float feedback, float wet, float duck,
	      bool bypass)
	: sr (sr_), block (block_)
	{
		proc = owned (new KTDelayProcessor ());
		proc->initialize (nullptr);

		// state stream: delay, feedback, wet, ducking (floats, normalized) + bypass (int32)
		MemoryStream ms;
		{
			IBStreamer w (&ms, kLittleEndian);
			w.writeFloat (delayNorm);
			w.writeFloat (feedback);
			w.writeFloat (wet);
			w.writeFloat (duck);
			w.writeInt32 (bypass ? 1 : 0);
		}
		ms.seek (0, IBStream::kIBSeekSet, nullptr);
		proc->setState (&ms);

		ProcessSetup setup {};
		setup.processMode = kRealtime;
		setup.symbolicSampleSize = sizeof (T) == 8 ? kSample64 : kSample32;
		setup.maxSamplesPerBlock = block;
		setup.sampleRate = sr;
		proc->setupProcessing (setup);
		proc->setActive (true);
		proc->setProcessing (true);
	}

	~Plug ()
	{
		proc->setProcessing (false);
		proc->setActive (false);
		proc->terminate ();
	}

	void run (const std::vector<T>& inL, const std::vector<T>& inR, std::vector<T>& outL,
	          std::vector<T>& outR)
	{
		const size_t n = inL.size ();
		outL.assign (n, T (0));
		outR.assign (n, T (0));
		for (size_t pos = 0; pos < n; pos += block)
		{
			const int32 ns = static_cast<int32> (std::min<size_t> (block, n - pos));
			T* inBufs[2] = {const_cast<T*> (inL.data () + pos), const_cast<T*> (inR.data () + pos)};
			T* outBufs[2] = {outL.data () + pos, outR.data () + pos};

			AudioBusBuffers ib {};
			ib.numChannels = 2;
			AudioBusBuffers ob {};
			ob.numChannels = 2;
			if constexpr (sizeof (T) == 8)
			{
				ib.channelBuffers64 = reinterpret_cast<double**> (inBufs);
				ob.channelBuffers64 = reinterpret_cast<double**> (outBufs);
			}
			else
			{
				ib.channelBuffers32 = reinterpret_cast<float**> (inBufs);
				ob.channelBuffers32 = reinterpret_cast<float**> (outBufs);
			}

			ProcessData d {};
			d.processMode = kRealtime;
			d.symbolicSampleSize = sizeof (T) == 8 ? kSample64 : kSample32;
			d.numSamples = ns;
			d.numInputs = 1;
			d.numOutputs = 1;
			d.inputs = &ib;
			d.outputs = &ob;
			proc->process (d);
		}
	}
};

//------------------------------------------------------------------------
template <typename T>
static void testImpulse (const char* label)
{
	// 100 ms delay @ 48 kHz = 4800 samples, feedback 50 % (-> gain 0.49), wet 100 %
	const double sr = 48000.0;
	Plug<T> p (sr, 512, 0.25f, 0.5f, 1.0f, 0.0f, false);
	const size_t n = static_cast<size_t> (sr);
	std::vector<T> inL (n, T (0)), inR (n, T (0)), outL, outR;
	inL[0] = T (1);
	inR[0] = T (0.5);
	p.run (inL, inR, outL, outR);

	std::printf ("-- impulse response (%s)\n", label);
	expectNear ("dry passes through at sample 0 (L)", outL[0], 1.0, 1e-6);
	expectNear ("dry passes through at sample 0 (R)", outR[0], 0.5, 1e-6);
	expectNear ("1st echo at 4800 samples (L)", outL[4800], 1.0, 2e-3);
	expectNear ("1st echo at 4800 samples (R)", outR[4800], 0.5, 2e-3);
	expectNear ("2nd echo = 0.49 (feedback 50 % -> 0.98 * 0.5)", outL[9600], 0.49, 3e-3);
	expectNear ("3rd echo = 0.49^2", outL[14400], 0.49 * 0.49, 3e-3);

	double maxBetween = 0.0;
	for (size_t i = 1; i < 4799; ++i)
		maxBetween = std::max (maxBetween, std::fabs (static_cast<double> (outL[i])));
	expectNear ("silence between dry and 1st echo", maxBetween, 0.0, 1e-6);

	// stereo independence: R must not leak into L
	double maxLeak = 0.0;
	for (size_t i = 0; i < n; ++i)
		maxLeak = std::max (maxLeak, std::fabs (static_cast<double> (outL[i]) - 2.0 * outR[i]));
	expectNear ("channels are independent (L == 2*R everywhere)", maxLeak, 0.0, 1e-5);
}

//------------------------------------------------------------------------
static void testWetLevel ()
{
	const double sr = 44100.0;
	// 200 ms, no feedback, wet 25 %
	Plug<float> p (sr, 256, 0.5f, 0.0f, 0.25f, 0.0f, false);
	const size_t n = static_cast<size_t> (sr);
	std::vector<float> inL (n, 0.f), inR (n, 0.f), outL, outR;
	inL[0] = inR[0] = 1.f;
	p.run (inL, inR, outL, outR);
	std::printf ("-- wet level\n");
	expectNear ("echo level == wet level (0.25) @ 8820 samples", outL[8820], 0.25, 2e-3);
	expectNear ("no 2nd echo without feedback", outL[17640], 0.0, 1e-4);
}

//------------------------------------------------------------------------
static void testBypass ()
{
	const double sr = 48000.0;
	Plug<float> p (sr, 512, 0.25f, 0.5f, 1.0f, 0.0f, true);
	const size_t n = static_cast<size_t> (sr);
	std::vector<float> inL (n, 0.f), inR (n, 0.f), outL, outR;
	inL[0] = inR[0] = 1.f;
	p.run (inL, inR, outL, outR);
	std::printf ("-- bypass\n");
	expectNear ("bypass: dry still passes", outL[0], 1.0, 1e-6);
	double maxWet = 0.0;
	for (size_t i = 1; i < n; ++i)
		maxWet = std::max (maxWet, std::fabs (static_cast<double> (outL[i])));
	expectNear ("bypass: no wet signal at all", maxWet, 0.0, 1e-6);
}

//------------------------------------------------------------------------
static double duckingOutputLevel (float duck, float dryLevel)
{
	// 10 ms delay, no feedback, wet 100 %: steady state output = dry + wet * duckGain
	const double sr = 48000.0;
	Plug<float> p (sr, 512, 0.025f, 0.0f, 1.0f, duck, false);
	const size_t n = static_cast<size_t> (sr * 0.5);
	std::vector<float> inL (n, dryLevel), inR (n, dryLevel), outL, outR;
	p.run (inL, inR, outL, outR);
	return outL[n - 1];
}

static void testDucking ()
{
	std::printf ("-- ducking (dry = 0.5 -> -6 dBFS, above the full-ducking point)\n");
	expectNear ("ducking   0 %: out = dry + wet = 1.0", duckingOutputLevel (0.0f, 0.5f), 1.0, 0.01);
	expectNear ("ducking  50 %: wet halved -> 0.75", duckingOutputLevel (0.5f, 0.5f), 0.75, 0.01);
	expectNear ("ducking 100 %: wet fully removed -> 0.5", duckingOutputLevel (1.0f, 0.5f), 0.5, 0.01);
	std::printf ("-- ducking with quiet dry (-46 dBFS: below the ducking range)\n");
	expectNear ("ducking 100 % but quiet dry: no reduction (2 * 0.005)",
	            duckingOutputLevel (1.0f, 0.005f), 0.01, 0.0005);

	// release: when the dry signal stops the wet signal comes back
	std::printf ("-- ducking release\n");
	const double sr = 48000.0;
	Plug<float> p (sr, 512, 1.0f, 0.0f, 1.0f, 1.0f, false); // 400 ms delay
	const size_t n = static_cast<size_t> (sr);
	std::vector<float> inL (n, 0.f), inR (n, 0.f), outL, outR;
	for (size_t i = 0; i < n / 2; ++i)
		inL[i] = inR[i] = 0.5f; // loud for the first 500 ms
	p.run (inL, inR, outL, outR);
	// t = 450 ms: dry loud, echo of the loud part is present -> fully ducked -> 0.5
	expectNear ("t=450ms: dry loud, echo ducked away", outL[21600], 0.5, 0.01);
	// t = 850 ms: dry silent for 350 ms, echo still running -> ducking released (partly)
	check (outL[40800] > 0.1, "t=850ms: echo audible again after dry stopped (> 0.1)", outL[40800],
	       0.1, 0.0);
	// t = 899 ms (just before the echo ends): even more released
	check (outL[43100] > outL[40800], "t=899ms: ducking keeps releasing", outL[43100], outL[40800],
	       0.0);
}

//------------------------------------------------------------------------
static void testDelayGlide ()
{
	// change the delay time while running: output must stay bounded (no blow-up)
	const double sr = 48000.0;
	Plug<float> p (sr, 512, 0.25f, 0.9f, 1.0f, 0.0f, false);
	const size_t n = static_cast<size_t> (sr);
	std::vector<float> inL (n), inR (n), outL, outR;
	for (size_t i = 0; i < n; ++i)
		inL[i] = inR[i] = 0.5f * static_cast<float> (std::sin (2.0 * 3.14159265 * 220.0 * i / sr));
	p.run (inL, inR, outL, outR);
	double peak = 0.0;
	for (float v : outL)
		peak = std::max (peak, std::fabs (static_cast<double> (v)));
	std::printf ("-- stability\n");
	check (peak < 6.0 && std::isfinite (peak), "feedback 90 %, 1 s of sine: output bounded", peak,
	       6.0, 0.0);
}

//------------------------------------------------------------------------
int main ()
{
	testImpulse<float> ("32 bit");
	testImpulse<double> ("64 bit");
	testWetLevel ();
	testBypass ();
	testDucking ();
	testDelayGlide ();

	std::printf ("\n%d checks, %d failed\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
