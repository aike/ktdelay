//------------------------------------------------------------------------
// KT Delay - image based editor (VSTGUI, code only, no uidesc)
// Copyright (c) 2026 aike. SPDX-License-Identifier: MIT
//------------------------------------------------------------------------
#pragma once

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/vstguifwd.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace KT {

class KTDelayController;

//------------------------------------------------------------------------
class KTDelayEditor : public Steinberg::Vst::VSTGUIEditor, public VSTGUI::IControlListener
{
public:
	explicit KTDelayEditor (KTDelayController* controller);
	~KTDelayEditor () SMTG_OVERRIDE;

	//--- VSTGUIEditor ---------------------------------------------------------
	bool PLUGIN_API open (void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
	void PLUGIN_API close () SMTG_OVERRIDE;
	/** Knob mouse mode: honour the host's "relative circular" setting, otherwise drag up/down. */
	VSTGUI_INT32 getKnobMode () const SMTG_OVERRIDE;

	//--- IControlListener -----------------------------------------------------
	void valueChanged (VSTGUI::CControl* control) SMTG_OVERRIDE;
	void controlBeginEdit (VSTGUI::CControl* control) SMTG_OVERRIDE;
	void controlEndEdit (VSTGUI::CControl* control) SMTG_OVERRIDE;

	/** Called by the controller when a parameter changed (host automation, preset load...). */
	void update (Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value);

	static constexpr int kWidth = 600;
	static constexpr int kHeight = 260;

private:
	void updateDelayReadout (Steinberg::Vst::ParamValue normalized);
	void showDelayReadout ();
	void hideDelayReadout ();
	void restartHideTimer ();

	KTDelayController* mController = nullptr;
	VSTGUI::CAnimKnob* mKnobs[4] = {nullptr, nullptr, nullptr, nullptr};
	VSTGUI::COnOffButton* mBypassButton = nullptr;
	VSTGUI::CTextLabel* mDelayReadout = nullptr;
	VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> mHideTimer;
	bool mEditingDelay = false;
};

//------------------------------------------------------------------------
} // namespace KT
