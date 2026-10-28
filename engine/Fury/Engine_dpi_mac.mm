// macOS DPI shim. Compiled only on Apple; today returns 1.0 because
// SFML 3 macOS forces highDpi=NO. The call is in place so a future
// Retina-rendering change can consume the value without API churn.

#import <AppKit/AppKit.h>

#include <algorithm>

namespace fury
{
	float furyGetMacOSBackingScale()
	{
		@autoreleasepool {
			NSScreen *screen = [NSScreen mainScreen];
			if (screen == nil) return 1.0f;
			return (float)[screen backingScaleFactor];
		}
	}

	// FPS-look cursor: disassociate the cursor from the mouse (it stops
	// moving - no recenter warps, no edge escape) and hide it; deltas are
	// polled separately via furyMacOSPollMouseDelta. SFML's own grab is
	// unusable here (center-locked event positions).
	void furyMacOSSyncCursor(bool grabbed, bool hidden)
	{
		static bool s_Grabbed = false, s_Hidden = false;
		@autoreleasepool {
			if (grabbed != s_Grabbed)
			{
				CGAssociateMouseAndMouseCursorPosition(!grabbed);
				if (grabbed)
				{
					// Drop the delta backlog accumulated while ungrabbed.
					int32_t dumpX = 0, dumpY = 0;
					CGGetLastMouseDelta(&dumpX, &dumpY);
				}
				s_Grabbed = grabbed;
			}
			if (hidden != s_Hidden)
			{
				if (hidden) CGDisplayHideCursor(kCGDirectMainDisplay);
				else CGDisplayShowCursor(kCGDirectMainDisplay);
				s_Hidden = hidden;
			}
		}
	}

	// Raw HID mouse delta since the previous call (Quake-style macOS look
	// input; unaffected by cursor position, warps, or event coalescing).
	void furyMacOSPollMouseDelta(float &dx, float &dy)
	{
		int32_t dX = 0, dY = 0;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
		CGGetLastMouseDelta(&dX, &dY);
#pragma clang diagnostic pop
		dx = (float)dX;
		dy = (float)dY;
	}

	namespace
	{
		// Trackpad gesture state. SFML reports only the line-based wheel
		// delta and drops the magnify gesture, so the precise scroll and the
		// pinch are captured with a local NSEvent monitor.
		float s_TrackpadScrollX = 0.0f;
		float s_TrackpadScrollY = 0.0f;
		float s_TrackpadMagnify = 0.0f;
		float s_TrackpadZoomScroll = 0.0f;
		bool s_TrackpadMonitorInstalled = false;
	}

	// Installs the trackpad gesture monitor. Safe to call more than once.
	void furyMacOSInstallTrackpadMonitor()
	{
		if (s_TrackpadMonitorInstalled)
			return;
		s_TrackpadMonitorInstalled = true;

		[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel
			handler:^NSEvent *(NSEvent *event) {
				if ([event hasPreciseScrollingDeltas])
				{
					// A command-modified scroll zooms instead of panning.
					if (([event modifierFlags] & NSEventModifierFlagCommand) != 0)
						s_TrackpadZoomScroll += (float)[event scrollingDeltaY];
					else
					{
						s_TrackpadScrollX += (float)[event scrollingDeltaX];
						s_TrackpadScrollY += (float)[event scrollingDeltaY];
					}
				}
				return event;
			}];

		[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskMagnify
			handler:^NSEvent *(NSEvent *event) {
				s_TrackpadMagnify += (float)[event magnification];
				return event;
			}];
	}

	// Drains the gestures accumulated since the last call. magnify is in
	// wheel units: the zoom step a wheel consumer expects.
	void furyMacOSPollTrackpadGesture(float &scrollX, float &scrollY, float &magnify)
	{
		// Clamped, so a gesture that piled up while nothing consumed it does
		// not turn into a jump.
		scrollX = std::min(std::max(s_TrackpadScrollX, -400.0f), 400.0f);
		scrollY = std::min(std::max(s_TrackpadScrollY, -400.0f), 400.0f);
		// The pinch ratio and the command-modified scroll are scaled into
		// one wheel delta: a notch of a mouse wheel is 1.0.
		const float zoom = s_TrackpadMagnify * 10.0f + s_TrackpadZoomScroll * 0.1f;
		magnify = std::min(std::max(zoom, -2.0f), 2.0f);
		s_TrackpadScrollX = 0.0f;
		s_TrackpadScrollY = 0.0f;
		s_TrackpadMagnify = 0.0f;
		s_TrackpadZoomScroll = 0.0f;
	}
}
