#ifndef _FURY_METAL_WINDOW_BRIDGE_H_
#define _FURY_METAL_WINDOW_BRIDGE_H_

namespace sf
{
	class WindowBase;
}

namespace fury
{
	// ObjC++ bridge (MetalWindowBridge.mm): SFML's native handle is an
	// NSWindow; Metal presents through a CAMetalLayer set on its content
	// view. The returned pointer is a retained CAMetalLayer*.
	void *MetalAttachLayer(sf::WindowBase *window);
}

#endif // _FURY_METAL_WINDOW_BRIDGE_H_
