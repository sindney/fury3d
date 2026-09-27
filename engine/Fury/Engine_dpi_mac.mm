// macOS DPI shim. Compiled only on Apple; today returns 1.0 because
// SFML 3 macOS forces highDpi=NO. The call is in place so a future
// Retina-rendering change can consume the value without API churn.

#import <AppKit/AppKit.h>

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
}
