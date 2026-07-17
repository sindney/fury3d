#ifdef WITH_EDITOR

#include "Fury/AnimationClip.h"
#include "Fury/AnimationPlayer.h"
#include "Fury/AnimationState.h"
#include "Fury/Camera.h"
#include "Fury/Editor/Editor.h"
#include "Fury/Editor/EditorAssetPicker.h"
#include "Fury/EntityManager.h"
#include "Fury/Joint.h"
#include "Fury/Log.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/Pipeline.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Transform.h"
#include "ImGui/imgui.h"
#include "ImSequencer.h"
#include "ImCurveEdit.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace fury {
namespace Editor {

// Joint-skeleton debug overlay. Toggled via the Animator inspector;
// when enabled, draws a line from each joint to its parent plus a
// small marker at each joint's world position. Helps diagnose
// mangled-skinning issues by showing where the runtime TRS is sending
// the joints versus the bind pose.
bool g_ShowJoints = false;

// ImSequencer track model wrapping an AnimationClip. One track per
// channel; each track spans the channel's full keyframe tick range.
class ClipSequence : public ImSequencer::SequenceInterface
{
public:
	std::shared_ptr<AnimationClip> clip;
	int frameMin = 0;
	int frameMax = 1;
	std::vector<std::pair<int, int>> ranges; // per-channel [firstTick, lastTick]

	ClipSequence() = default;

	void SetClip(const std::shared_ptr<AnimationClip> &c)
	{
		clip = c;
		ranges.clear();
		frameMin = 0;
		frameMax = 1;
		if (!clip) return;

		int count = clip->GetChannelCount();
		ranges.resize(count, {0, 0});
		int gmax = 0;
		for (int i = 0; i < count; ++i)
		{
			auto ch = clip->GetChannelAt(i);
			int lo = -1, hi = -1;
			auto scan = [&](const std::vector<KeyFrame> &f)
			{
				if (f.empty()) return;
				int a = static_cast<int>(f.front().tick);
				int b = static_cast<int>(f.back().tick);
				lo = (lo < 0 || a < lo) ? a : lo;
				hi = (hi < 0 || b > hi) ? b : hi;
			};
			if (ch) { scan(ch->positions); scan(ch->rotations); scan(ch->scalings); }
			if (lo < 0) lo = 0;
			if (hi < 0) hi = 0;
			ranges[i] = {lo, hi};
			if (hi > gmax) gmax = hi;
		}
		frameMax = gmax > 0 ? gmax : 1;
	}

	int GetFrameMin() const override { return frameMin; }
	int GetFrameMax() const override { return frameMax; }
	int GetItemCount() const override { return clip ? clip->GetChannelCount() : 0; }

	void Get(int index, int** start, int** end, int* type, unsigned int* color) override
	{
		if (index < 0 || index >= static_cast<int>(ranges.size())) return;
		static int s_start, s_end;
		s_start = ranges[index].first;
		s_end = ranges[index].second;
		if (start) *start = &s_start;
		if (end) *end = &s_end;
		if (type) *type = 0;
		if (color) *color = 0xFF9090E0;
	}

	const char* GetItemLabel(int index) const override
	{
		if (!clip || index < 0 || index >= clip->GetChannelCount()) return "";
		auto ch = clip->GetChannelAt(index);
		return ch ? ch->name.c_str() : "";
	}

	const char* GetItemTypeName(int) const override { return "channel"; }
	int GetItemTypeCount() const override { return 1; }
};

// ImCurveEdit delegate showing one keyframe bucket (positions/rotations/
// scalings) of a channel as three curves (X/Y/Z). Points are ImVec2(tick, value).
class ChannelCurveDelegate : public ImCurveEdit::Delegate
{
public:
	std::shared_ptr<AnimationChannel> channel;
	int bucket = 0; // 0=positions 1=rotations 2=scalings
	std::vector<std::vector<ImVec2>> points; // [component][frame]
	ImVec2 m_min = ImVec2(0, -1);
	ImVec2 m_max = ImVec2(1, 1);
	bool dirty = false;

	void Load(const std::shared_ptr<AnimationChannel> &ch, int b)
	{
		channel = ch;
		bucket = b;
		points.assign(3, {});
		if (!ch) return;
		auto fill = [&](const std::vector<KeyFrame> &f)
		{
			for (int c = 0; c < 3; ++c)
			{
				points[c].clear();
				points[c].reserve(f.size());
				for (const auto &kf : f)
				{
					float v = (c == 0) ? kf.x : (c == 1) ? kf.y : kf.z;
					points[c].push_back(ImVec2(static_cast<float>(kf.tick), v));
				}
			}
		};
		if (b == 0) fill(ch->positions);
		else if (b == 1) fill(ch->rotations);
		else fill(ch->scalings);

		RecomputeBounds();
	}

	void RecomputeBounds()
	{
		float tmin = FLT_MAX, tmax = -FLT_MAX, vmin = FLT_MAX, vmax = -FLT_MAX;
		for (const auto &cv : points)
			for (const auto &p : cv)
			{
				if (p.x < tmin) tmin = p.x;
				if (p.x > tmax) tmax = p.x;
				if (p.y < vmin) vmin = p.y;
				if (p.y > vmax) vmax = p.y;
			}
		if (tmax <= tmin) { tmin = 0; tmax = 1; }
		if (vmax <= vmin) { vmin = -1; vmax = 1; }
		m_min = ImVec2(tmin, vmin);
		m_max = ImVec2(tmax, vmax);
	}

	size_t GetCurveCount() override { return 3; }
	bool IsVisible(size_t) override { return true; }
	ImCurveEdit::CurveType GetCurveType(size_t) const override { return ImCurveEdit::CurveLinear; }
	ImVec2& GetMin() override { return m_min; }
	ImVec2& GetMax() override { return m_max; }
	size_t GetPointCount(size_t curveIndex) override
	{
		return curveIndex < points.size() ? points[curveIndex].size() : 0;
	}
	uint32_t GetCurveColor(size_t curveIndex) override
	{
		static const uint32_t cols[3] = { 0xFFFF6060, 0xFF60FF60, 0xFF6060FF };
		return cols[curveIndex % 3];
	}
	ImVec2* GetPoints(size_t curveIndex) override
	{
		return curveIndex < points.size() ? points[curveIndex].data() : nullptr;
	}
	int EditPoint(size_t curveIndex, int pointIndex, ImVec2 value) override
	{
		if (!channel || curveIndex >= points.size()) return pointIndex;
		if (pointIndex < 0 || pointIndex >= static_cast<int>(points[curveIndex].size())) return pointIndex;
		points[curveIndex][pointIndex] = value;
		WriteBack(static_cast<int>(curveIndex), pointIndex, value);
		dirty = true;
		return pointIndex;
	}
	void AddPoint(size_t, ImVec2) override {}

	void WriteBack(int component, int pointIndex, ImVec2 value)
	{
		std::vector<KeyFrame>* bucket = nullptr;
		if (channel)
		{
			if (bucket == nullptr && this->bucket == 0) bucket = &channel->positions;
			else if (this->bucket == 1) bucket = &channel->rotations;
			else bucket = &channel->scalings;
		}
		if (!bucket || pointIndex < 0 || pointIndex >= static_cast<int>(bucket->size())) return;
		auto &kf = (*bucket)[pointIndex];
		kf.tick = static_cast<unsigned int>(std::max(0.0f, value.x));
		if (component == 0) kf.x = value.y;
		else if (component == 1) kf.y = value.y;
		else kf.z = value.y;
	}
};

static const char* kBucketNames[] = {"Positions", "Rotations", "Scalings"};
static const int kBucketCount = 3;

void RenderAnimationWindow(bool* pOpen)
{
	ImGui::SetNextWindowSize(ImVec2(820, 520), ImGuiCond_Appearing);
	ImGui::SetNextWindowSizeConstraints(ImVec2(420, 300), ImVec2(FLT_MAX, FLT_MAX));
	if (!ImGui::Begin("Animation", pOpen))
	{
		ImGui::End();
		return;
	}

	// Gather clips from the active scene (shared enumeration with the
	// asset-picker modal — Editor::CollectAnimationClips).
	auto clips = Editor::CollectAnimationClips();

	if (clips.empty())
	{
		ImGui::TextDisabled("No AnimationClip registered. Import a glTF/FBX scene with animations.");
		ImGui::End();
		return;
	}

	static int s_SelectedClip = 0;
	static int s_SelectedTrack = -1;
	static int s_CurrentFrame = 0;
	static int s_FirstFrame = 0;
	static bool s_Expanded = true;
	static int s_Bucket = 0;
	static ClipSequence s_Sequence;
	static ChannelCurveDelegate s_Curve;
	static std::shared_ptr<Animator> s_ActiveAnim;

	if (s_SelectedClip < 0 || s_SelectedClip >= static_cast<int>(clips.size()))
		s_SelectedClip = 0;

	// Resolve the active Animator. Three sources, in order:
	// 1. `s_ActiveAnim` (user picked from the dropdown below)
	// 2. The currently selected node's Animator (treeview pick)
	// 3. (none) — no fallback; the user must pick explicitly.
	std::shared_ptr<Animator> anim;
	std::shared_ptr<AnimationState> animState;
	std::shared_ptr<SceneNode> animOwner;
	if (s_ActiveAnim && s_ActiveAnim->GetOwner())
	{
		anim = s_ActiveAnim;
		animOwner = anim->GetOwner();
	}
	if (!anim)
	{
		SceneNode* sel = Editor::GetSelectedSceneNode();
		if (sel)
		{
			anim = sel->GetComponent<Animator>();
			if (anim) animOwner = sel->shared_from_this();
		}
	}
	// Mirror the dropdown into the treeview selection — lets the user
	// switch the Animator target by clicking a node in the Scene Inspector.
	if (animOwner && s_ActiveAnim != anim)
	{
		s_ActiveAnim = anim;
		Editor::SetSelectedSceneNode(animOwner.get());
	}

	auto currentClip = clips[s_SelectedClip];
	if (anim)
		animState = anim->GetState(currentClip->GetName());

	// Animator dropdown — Animators are not registered in the EntityManager
	// (they're attached as components to SceneNodes), so we walk the active
	// scene's node tree and collect every node that owns an Animator.
	std::vector<std::shared_ptr<Animator>> animators;
	if (Scene::Active)
	{
		auto root = Scene::Active->GetRootNode();
		std::function<void(const std::shared_ptr<SceneNode> &)> walk =
			[&](const std::shared_ptr<SceneNode> &n) {
			if (!n) return;
			if (auto a = n->GetComponent<Animator>()) animators.push_back(a);
			for (unsigned int i = 0; i < n->GetChildCount(); ++i) walk(n->GetChildAt(i));
		};
		walk(root);
	}
	std::string currentName = animOwner ? animOwner->GetName() : "(none)";
	if (animators.empty()) {
		ImGui::TextDisabled("Animator: (none in scene)");
	} else {
		ImGui::Text("Animator:");
		ImGui::SameLine();
		if (ImGui::BeginCombo("##animcombo", currentName.c_str())) {
			for (const auto &a : animators) {
				auto owner = a->GetOwner();
				std::string n = owner ? owner->GetName() : "(orphan)";
				bool isSel = (a == anim);
				if (ImGui::Selectable(n.c_str(), isSel)) {
					s_ActiveAnim = a;
					if (owner) Editor::SetSelectedSceneNode(owner.get());
				}
			}
			ImGui::EndCombo();
		}
	}

	// Sidebar: clip list.
	ImGui::BeginChild("clips", ImVec2(200, 0), true);
	ImGui::TextDisabled("Clips (%d)", static_cast<int>(clips.size()));
	ImGui::Separator();
	for (int i = 0; i < static_cast<int>(clips.size()); ++i)
	{
		bool sel_flag = (i == s_SelectedClip);
		if (ImGui::Selectable(clips[i]->GetName().c_str(), sel_flag))
		{
			s_SelectedClip = i;
			s_SelectedTrack = -1;
			currentClip = clips[i];
			if (anim) animState = anim->GetState(currentClip->GetName());
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("timeline", ImVec2(0, 0), true);
	ImGui::Text("Clip: %s  (duration %.3fs, %d tps)",
		currentClip->GetName().c_str(),
		currentClip->GetDuration(),
		currentClip->GetTicksPerSecond());

	if (anim)
	{
		ImGui::Text("Animator: %s  state: %s",
			anim->IsPlaying(currentClip->GetName()) ? "playing" : "stopped",
			animState ? animState->GetName().c_str() : "(not bound)");
		if (ImGui::Button("Play"))
		{
			anim->Play(currentClip->GetName());
		}
		ImGui::SameLine();
		if (ImGui::Button("Stop"))
		{
			anim->Stop(currentClip->GetName());
		}
		ImGui::SameLine();
		if (ImGui::Button("Rewind"))
		{
			anim->Rewind(currentClip->GetName());
			if (animState) s_CurrentFrame = 0;
		}
	}
	else
	{
		ImGui::TextDisabled("Select a node with an Animator to play/scrub.");
	}

	ImGui::Separator();

	// Keep the sequence bound to the current clip.
	if (s_Sequence.clip.get() != currentClip.get())
		s_Sequence.SetClip(currentClip);

	// Mirror the playhead from the Animator's state time when playing.
	// (Read-only: the Animator's own OnUpdate tick is already posing the
	// mesh, so we must NOT re-pose here — that would double-tick.)
	const bool playing = anim && animState && anim->IsPlaying(currentClip->GetName());
	if (playing)
		s_CurrentFrame = static_cast<int>(animState->GetTickTime());

	// Capture the frame before ImSequencer so we can tell whether the
	// USER dragged the playhead (vs. the auto-play mirror above).
	int frameBeforeSequencer = s_CurrentFrame;

	int selectedEntry = s_SelectedTrack;
	int firstFrame = s_FirstFrame;
	ImSequencer::Sequencer(&s_Sequence, &s_CurrentFrame, &s_Expanded,
		&selectedEntry, &firstFrame,
		ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_CHANGE_FRAME);
	s_SelectedTrack = selectedEntry;
	s_FirstFrame = firstFrame;

	// Scrub ONLY on a user-initiated playhead drag (ImSequencer changed
	// the frame while not auto-playing). When playing, the Animator's
	// own tick handles posing — re-posing here would double-advance.
	bool userDragged = (!playing) && (s_CurrentFrame != frameBeforeSequencer);
	if (anim && animState && userDragged)
	{
		bool wasEnabled = animState->IsEnabled();
		animState->SetEnabled(true);
		animState->SetTime(static_cast<float>(s_CurrentFrame) / currentClip->GetTicksPerSecond());
		anim->AdvanceTime(0.0f);
		anim->Display(1.0f);
		animState->SetEnabled(wasEnabled);
	}

	ImGui::Separator();

	// Curve editor for the selected track.
	if (s_SelectedTrack >= 0 && s_SelectedTrack < currentClip->GetChannelCount())
	{
		ImGui::Combo("Bucket", &s_Bucket, kBucketNames, kBucketCount);
		auto ch = currentClip->GetChannelAt(s_SelectedTrack);
		if (ch)
		{
			if (s_Curve.channel.get() != ch.get() || s_Curve.bucket != s_Bucket)
				s_Curve.Load(ch, s_Bucket);

			ImVec2 editSize(ImGui::GetContentRegionAvail().x, 180);
			ImCurveEdit::Edit(s_Curve, editSize, 0x12345678);
			if (s_Curve.dirty)
			{
				s_Curve.dirty = false;
				s_Curve.RecomputeBounds();
				Editor::MarkSceneDirty();
				currentClip->CalculateDuration();
				s_Sequence.SetClip(currentClip);
			}
		}
	}
	else
	{
		ImGui::TextDisabled("Select a track in the timeline to edit its keyframe curves.");
	}

	ImGui::EndChild();

	ImGui::End();
}

bool IsJointDebugEnabled() { return g_ShowJoints; }
void SetJointDebugEnabled(bool enabled) { g_ShowJoints = enabled; }

// Project a world-space point through the current camera. Returns
// false if the point is behind the camera or off-screen.
static bool ProjectToScreen(const Vector4 &world, const Matrix4 &viewProj,
							 const ImVec2 &origin, const ImVec2 &size,
							 ImVec2 &outScreen, float &outDepth)
{
	if (size.x <= 0.0f || size.y <= 0.0f) return false;
	Vector4 clip = viewProj.Multiply(world);
	if (clip.w == 0.0f) return false;
	// Behind the camera: clip.w < 0.
	if (clip.w < 0.0f) return false;
	float invW = 1.0f / clip.w;
	float ndcX = clip.x * invW;
	float ndcY = clip.y * invW;
	float ndcZ = clip.z * invW;
	// NDC → screen.
	outScreen.x = origin.x + (ndcX * 0.5f + 0.5f) * size.x;
	outScreen.y = origin.y + (1.0f - (ndcY * 0.5f + 0.5f)) * size.y;
	outDepth = ndcZ;
	return true;
}

// Draw the joint hierarchy of every skinned mesh in the active scene
// as a line/marker overlay on top of the viewport. Walks each mesh's
// root joint; for every joint, draws a line to its parent's world
// position and a small dot at the joint's own world position.
void RenderJointDebugOverlay(const ImVec2 &origin, const ImVec2 &size)
{
	if (!g_ShowJoints) return;
	if (!Scene::Active) return;
	auto em = Scene::Active->GetEntityManager();
	if (!em) return;
	std::shared_ptr<SceneNode> camNode;
	if (Pipeline::Active) camNode = Pipeline::Active->GetCurrentCamera();
	if (!camNode) return;
	auto cam = camNode->GetComponent<Camera>();
	if (!cam) return;
	auto camWorld = camNode->GetWorldMatrix();
	Matrix4 view = camWorld.Inverse();
	Matrix4 proj = cam->GetProjectionMatrix();
	Matrix4 viewProj = proj * view;

	// Draw into the Viewport window's own draw list (same layer as the
	// TRS gizmo) so the overlay composites over the viewport image but
	// stays BEHIND docked editor panels. The foreground draw list would
	// render on top of every window, including the panels.
	ImDrawList *drawList = ImGui::GetWindowDrawList();
	int walkCount = 0;
	em->ForEach<Mesh>([&](const std::shared_ptr<Mesh> &mesh) -> bool {
		if (!mesh) return true;
		auto root = mesh->GetRootJoint();
		if (!root) return true;

		// Build a name -> world-position lookup so we can draw each
		// joint-to-parent line regardless of first-child/sibling link
		// correctness. Iterate every joint by index (the mesh's flat
		// list) instead of walking the linked tree. Joint world
		// positions come from each joint's linked SceneNode (wired at
		// import and re-linked on scene load) — the scene graph already
		// includes ancestors like the skeleton root's parent.
		std::unordered_map<std::string, Vector4> worldByName;
		const unsigned int jCount = mesh->GetJointCount();
		for (unsigned int i = 0; i < jCount; ++i) {
			auto j = mesh->GetJointAt(i);
			if (!j) continue;
			auto jn = j->GetSceneNode();
			Vector4 jworld;
			if (jn) {
				const Matrix4 &w = jn->GetWorldMatrix();
				jworld = Vector4(w.Raw[12], w.Raw[13], w.Raw[14], 1.0f);
			}
			worldByName[j->GetName()] = jworld;
		}
		for (unsigned int i = 0; i < jCount; ++i) {
			auto j = mesh->GetJointAt(i);
			if (!j) continue;
			++walkCount;
			Vector4 selfWorld = worldByName[j->GetName()];
			// Parent: look up via GetParent(). If absent, fall back
			// to mesh origin so we still draw a dot at the root.
			Vector4 parentWorld(0, 0, 0, 1);
			auto parent = j->GetParent();
			if (parent) {
				auto it = worldByName.find(parent->GetName());
				if (it != worldByName.end()) parentWorld = it->second;
			}
			ImVec2 pSelf, pParent;
			float dSelf = 0.0f, dParent = 0.0f;
			bool okSelf = ProjectToScreen(selfWorld, viewProj, origin, size, pSelf, dSelf);
			bool okParent = ProjectToScreen(parentWorld, viewProj, origin, size, pParent, dParent);

			ImU32 colDot = IM_COL32(255, 80, 80, 255);
			if (okSelf && okParent) {
				float depthBlend = std::clamp(1.0f - 0.5f * (dSelf + dParent) * 0.5f, 0.3f, 1.0f);
				ImU32 bone = IM_COL32(static_cast<int>(255 * depthBlend),
									  static_cast<int>(200 * depthBlend),
									  static_cast<int>(60 * depthBlend), 200);
				drawList->AddLine(pParent, pSelf, bone, 2.0f);
			}
			if (okSelf) {
				drawList->AddCircleFilled(pSelf, 4.0f, colDot, 12);
				if (walkCount <= 6) {
					drawList->AddText(ImVec2(pSelf.x + 6, pSelf.y - 6),
									  IM_COL32(255, 255, 255, 220), j->GetName().c_str());
				}
			}
		}
		return true;
	});
}

} // namespace Editor
} // namespace fury

#endif // WITH_EDITOR
