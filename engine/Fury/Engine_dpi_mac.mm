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
}
