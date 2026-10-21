// Metal window bridge. ObjC++ (AppKit + QuartzCore); everything else in the
// Metal backend speaks metal-cpp C++ only.

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "Fury/RHI/Metal/MetalWindowBridge.h"

#include <SFML/Window/WindowBase.hpp>

namespace fury
{
	void *MetalAttachLayer(sf::WindowBase *window)
	{
		if (window == nullptr)
			return nullptr;

		@autoreleasepool {
			NSWindow *nsWindow = (__bridge NSWindow *)window->getNativeHandle();
			if (nsWindow == nil)
				return nullptr;

			// A dedicated subview hosts the Metal layer. Replacing the GL
			// context's own view-layer directly has AppKit reset it (the
			// layer dies at the next context activation); a subview is
			// untouched. The GL context stays dormant on the Metal path.
			NSView *content = nsWindow.contentView;
			NSView *metalView = [[NSView alloc] initWithFrame:content.bounds];
			metalView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
			[content addSubview:metalView];
			[metalView release];

			CAMetalLayer *layer = [CAMetalLayer layer];
			layer.contentsScale = nsWindow.backingScaleFactor;
			[metalView setWantsLayer:YES];
			[metalView setLayer:layer];

			// CFBridgingRetain (CFRetain under MRC, ARC retain under ARC):
			// the device owns this reference and releases it at destruction.
			return (void *)CFBridgingRetain(layer);
		}
	}
}
