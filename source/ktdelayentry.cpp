//------------------------------------------------------------------------
// KT Delay - plug-in factory entry
// Copyright (c) 2026 aike. SPDX-License-Identifier: MIT
//------------------------------------------------------------------------
#include "ktdelayprocessor.h"
#include "ktdelaycontroller.h"
#include "ktdelaycids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

#define stringPluginName "KT Delay"

using namespace Steinberg::Vst;
using namespace KT;

//------------------------------------------------------------------------
//  VST Plug-in Entry
//------------------------------------------------------------------------
// vendor name / homepage / contact: change as you like
BEGIN_FACTORY_DEF ("aikelab", "https://keisuke.ai", "mailto:info@keisuke.ai")

	//---First Plug-in included in this factory-------
	// its kVstAudioEffectClass component
	DEF_CLASS2 (INLINE_UID_FROM_FUID (kKTDelayProcessorUID),
	            PClassInfo::kManyInstances, // cardinality
	            kVstAudioEffectClass,       // the component category (do not change this)
	            stringPluginName,           // plug-in name
	            Vst::kDistributable,        // component and controller may run on different machines
	            KTDelayVST3Category,        // sub category
	            FULL_VERSION_STR,           // plug-in version
	            kVstVersionString,          // VST 3 SDK version
	            KTDelayProcessor::createInstance)

	// its kVstComponentControllerClass component
	DEF_CLASS2 (INLINE_UID_FROM_FUID (kKTDelayControllerUID),
	            PClassInfo::kManyInstances,     // cardinality
	            kVstComponentControllerClass,   // the controller category (do not change this)
	            stringPluginName "Controller",  // controller name
	            0,                              // not used here
	            "",                             // not used here
	            FULL_VERSION_STR,               // plug-in version
	            kVstVersionString,              // VST 3 SDK version
	            KTDelayController::createInstance)

END_FACTORY
