//------------------------------------------------------------------------
// KT Delay - edit controller
// Copyright (c) 2026 aike. SPDX-License-Identifier: MIT
//------------------------------------------------------------------------
#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include <vector>

namespace KT {

class KTDelayEditor;

//------------------------------------------------------------------------
class KTDelayController : public Steinberg::Vst::EditController
{
public:
	KTDelayController () = default;
	~KTDelayController () SMTG_OVERRIDE = default;

	static Steinberg::FUnknown* createInstance (void* /*context*/)
	{
		return (Steinberg::Vst::IEditController*)new KTDelayController;
	}

	//--- EditController overrides ----------------------------------------------
	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setParamNormalized (Steinberg::Vst::ParamID tag,
	                                                   Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;

	//--- editor registry (UI thread) ------------------------------------------
	void addEditor (KTDelayEditor* editor);
	void removeEditor (KTDelayEditor* editor);

	//--- helper for the UI -----------------------------------------------------
	/** Formats a normalized delay time as e.g. "250 ms". */
	static void formatDelayTime (Steinberg::Vst::ParamValue normalized, char* buffer, size_t size);

private:
	std::vector<KTDelayEditor*> mEditors;
};

//------------------------------------------------------------------------
} // namespace KT
