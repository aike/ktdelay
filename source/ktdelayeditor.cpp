//------------------------------------------------------------------------
// KT Delay - image based editor (VSTGUI, code only, no uidesc)
// Copyright (c) 2026 aike. SPDX-License-Identifier: MIT
//
// Layout (must match tools/gen_images.py):
//   window        600 x 260
//   knobs         80 x 80, left edges at x = 55 / 195 / 335 / 475, y = 100
//   knob strip    knob.png : 101 frames of 80 x 80 stacked vertically
//   bypass        bypass.png : 2 frames of 96 x 34 (top = off, bottom = on) at (480, 22)
//   delay readout text label shown above the Delay Time knob while it is turned
//------------------------------------------------------------------------
#include "ktdelayeditor.h"
#include "ktdelaycontroller.h"
#include "ktdelaycids.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/ctextlabel.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VSTGUI;

namespace KT {

namespace {
constexpr CCoord kKnobSize = 80.;
constexpr CCoord kKnobY = 100.;
constexpr CCoord kKnobX[4] = {55., 195., 335., 475.};
constexpr uint16_t kKnobFrames = 101;

constexpr CCoord kBypassX = 480.;
constexpr CCoord kBypassY = 22.;
constexpr CCoord kBypassW = 96.;
constexpr CCoord kBypassH = 34.;

constexpr CCoord kReadoutW = 72.;
constexpr CCoord kReadoutH = 20.;
constexpr uint32_t kReadoutHideDelayMs = 900;

const ParamID kKnobParamIds[4] = {kDelayTimeId, kFeedbackId, kWetLevelId, kDuckingId};
} // anonymous

//------------------------------------------------------------------------
KTDelayEditor::KTDelayEditor (KTDelayController* controller)
: VSTGUIEditor (controller), mController (controller)
{
	ViewRect viewRect (0, 0, kWidth, kHeight);
	setRect (viewRect);
	if (mController)
		mController->addEditor (this);
}

//------------------------------------------------------------------------
KTDelayEditor::~KTDelayEditor ()
{
	if (mController)
		mController->removeEditor (this);
}

//------------------------------------------------------------------------
bool PLUGIN_API KTDelayEditor::open (void* parent, const PlatformType& platformType)
{
	if (frame)
		return false;

	CRect frameSize (0, 0, kWidth, kHeight);
	frame = new CFrame (frameSize, this);
	frame->setTransparency (false);

	//--- background --------------------------------------------------------
	auto background = makeOwned<CBitmap> ("background.png");
	frame->setBackground (background);

	//--- knobs ---------------------------------------------------------------
	CMultiFrameBitmapDescription knobDesc;
	knobDesc.frameSize = CPoint (kKnobSize, kKnobSize);
	knobDesc.numFrames = kKnobFrames;
	knobDesc.framesPerRow = 1;
	auto knobBitmap = makeOwned<CMultiFrameBitmap> ("knob.png", knobDesc);

	for (int i = 0; i < 4; ++i)
	{
		CRect r (kKnobX[i], kKnobY, kKnobX[i] + kKnobSize, kKnobY + kKnobSize);
		auto* knob = new CAnimKnob (r, this, static_cast<int32_t> (kKnobParamIds[i]), knobBitmap);
		knob->setMin (0.f);
		knob->setMax (1.f);
		knob->setZoomFactor (8.f); // fine adjustment while holding shift
		knob->setWheelInc (0.01f);
		if (mController)
			knob->setValueNormalized (
			    static_cast<float> (mController->getParamNormalized (kKnobParamIds[i])));
		frame->addView (knob);
		mKnobs[i] = knob;
	}

	//--- bypass button --------------------------------------------------------
	auto bypassBitmap = makeOwned<CBitmap> ("bypass.png");
	CRect br (kBypassX, kBypassY, kBypassX + kBypassW, kBypassY + kBypassH);
	mBypassButton = new COnOffButton (br, this, static_cast<int32_t> (kBypassId), bypassBitmap);
	if (mController)
		mBypassButton->setValue (static_cast<float> (mController->getParamNormalized (kBypassId)));
	frame->addView (mBypassButton);

	//--- delay time readout (hidden until the knob is turned) -----------------
	const CCoord cx = kKnobX[0] + kKnobSize / 2.;
	CRect rr (cx - kReadoutW / 2., kKnobY - 24., cx + kReadoutW / 2., kKnobY - 24. + kReadoutH);
	mDelayReadout = new CTextLabel (rr, "", nullptr, CParamDisplay::kRoundRectStyle);
	mDelayReadout->setRoundRectRadius (5.);
	mDelayReadout->setBackColor (CColor (18, 19, 23, 235));
	mDelayReadout->setFrameColor (CColor (242, 163, 60, 255));
	mDelayReadout->setFrameWidth (1.);
	mDelayReadout->setFontColor (CColor (242, 163, 60, 255));
	mDelayReadout->setHoriAlign (kCenterText);
	auto readoutFont = makeOwned<CFontDesc> ("Segoe UI", 12, kBoldFace);
	mDelayReadout->setFont (readoutFont);
	mDelayReadout->setMouseEnabled (false);
	mDelayReadout->setVisible (false);
	if (mController)
		updateDelayReadout (mController->getParamNormalized (kDelayTimeId));
	frame->addView (mDelayReadout);

	frame->open (parent, platformType);
	return true;
}

//------------------------------------------------------------------------
void PLUGIN_API KTDelayEditor::close ()
{
	mHideTimer = nullptr;
	mEditingDelay = false;
	for (auto& knob : mKnobs)
		knob = nullptr;
	mBypassButton = nullptr;
	mDelayReadout = nullptr;

	if (frame)
	{
		frame->close (); // closes and releases the frame
		frame = nullptr;
	}
}

//------------------------------------------------------------------------
VSTGUI_INT32 KTDelayEditor::getKnobMode () const
{
	// VSTGUI falls back to the "circular" mode (value follows the cursor angle) when the
	// host does not tell us its preference. Linear drag (up = increase) is what most
	// users expect, so use that unless the host explicitly asks for relative circular.
	const VSTGUI_INT32 hostMode = VSTGUIEditor::getKnobMode ();
	if (hostMode == VSTGUI::kRelativCircularMode)
		return hostMode;
	return VSTGUI::kLinearMode;
}

//------------------------------------------------------------------------
void KTDelayEditor::valueChanged (CControl* control)
{
	if (!control || !mController)
		return;

	const ParamID tag = static_cast<ParamID> (control->getTag ());
	const ParamValue value = control->getValueNormalized ();

	mController->setParamNormalized (tag, value);
	mController->performEdit (tag, value);

	if (tag == kDelayTimeId)
	{
		updateDelayReadout (value);
		showDelayReadout ();
	}
}

//------------------------------------------------------------------------
void KTDelayEditor::controlBeginEdit (CControl* control)
{
	if (!control)
		return;
	const ParamID tag = static_cast<ParamID> (control->getTag ());
	beginEdit (static_cast<int32> (tag));

	if (tag == kDelayTimeId)
	{
		mEditingDelay = true;
		mHideTimer = nullptr;
		updateDelayReadout (control->getValueNormalized ());
		showDelayReadout ();
	}
}

//------------------------------------------------------------------------
void KTDelayEditor::controlEndEdit (CControl* control)
{
	if (!control)
		return;
	const ParamID tag = static_cast<ParamID> (control->getTag ());
	endEdit (static_cast<int32> (tag));

	if (tag == kDelayTimeId)
	{
		mEditingDelay = false;
		restartHideTimer ();
	}
}

//------------------------------------------------------------------------
void KTDelayEditor::update (ParamID tag, ParamValue value)
{
	if (!frame)
		return;

	switch (tag)
	{
		case kDelayTimeId:
		case kFeedbackId:
		case kWetLevelId:
		case kDuckingId:
		{
			if (auto* knob = mKnobs[tag])
			{
				if (knob->isEditing ())
					break; // user is turning it: the control is the source of truth
				knob->setValueNormalized (static_cast<float> (value));
				knob->invalid ();
			}
			if (tag == kDelayTimeId)
			{
				updateDelayReadout (value);
				if (!mEditingDelay)
				{
					// briefly show the value when it changes from the host side
					showDelayReadout ();
					restartHideTimer ();
				}
			}
			break;
		}
		case kBypassId:
			if (mBypassButton)
			{
				mBypassButton->setValue (value > 0.5 ? mBypassButton->getMax ()
				                                     : mBypassButton->getMin ());
				mBypassButton->invalid ();
			}
			break;
		default: break;
	}
}

//------------------------------------------------------------------------
void KTDelayEditor::updateDelayReadout (ParamValue normalized)
{
	if (!mDelayReadout)
		return;
	char text[32];
	KTDelayController::formatDelayTime (normalized, text, sizeof (text));
	mDelayReadout->setText (UTF8String (text));
	mDelayReadout->invalid ();
}

//------------------------------------------------------------------------
void KTDelayEditor::showDelayReadout ()
{
	if (mDelayReadout && !mDelayReadout->isVisible ())
	{
		mDelayReadout->setVisible (true);
		mDelayReadout->invalid ();
	}
}

//------------------------------------------------------------------------
void KTDelayEditor::hideDelayReadout ()
{
	if (mDelayReadout && mDelayReadout->isVisible ())
	{
		mDelayReadout->setVisible (false);
		if (frame)
			frame->invalidRect (mDelayReadout->getViewSize ());
	}
}

//------------------------------------------------------------------------
void KTDelayEditor::restartHideTimer ()
{
	mHideTimer = makeOwned<CVSTGUITimer> (
	    [this] (CVSTGUITimer* timer) {
		    timer->stop ();
		    if (!mEditingDelay)
			    hideDelayReadout ();
	    },
	    kReadoutHideDelayMs, true);
}

//------------------------------------------------------------------------
} // namespace KT
