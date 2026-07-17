#ifndef _FURY_EDITOR_ANIMATION_WINDOW_H_
#define _FURY_EDITOR_ANIMATION_WINDOW_H_

#include "Fury/Macros.h"

#ifdef WITH_EDITOR

struct ImVec2;

namespace fury {
namespace Editor {

void RenderAnimationWindow(bool* pOpen);

// Joint-skeleton debug overlay. When enabled, draws the joint
// hierarchy of every skinned mesh on top of the editor viewport
// as a line/marker overlay. Toggled from the Animator inspector.
FURY_API bool IsJointDebugEnabled();
FURY_API void SetJointDebugEnabled(bool enabled);
FURY_API void RenderJointDebugOverlay(const ImVec2& origin, const ImVec2& size);

} // namespace Editor
} // namespace fury

#endif // WITH_EDITOR

#endif // _FURY_EDITOR_ANIMATION_WINDOW_H_
