#ifndef _FURY_RENDER_THREAD_H_
#define _FURY_RENDER_THREAD_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "Fury/FramePacket.h"

#include "Fury/Macros.h"

namespace sf
{
	class Window;
}

namespace fury
{
	struct FramePacket;

	// Render->game mailbox entry. Currently recycles the packet slot;
	// grows result payloads (counters, editor-visible data) with the
	// editor stage.
	struct FrameResult
	{
		FrameResult() = default;
		FrameResult(FramePacket *p) : packet(p) {}
		FramePacket *packet = nullptr;
		// Light nodeKey -> this frame's shadow texture (editor debug views).
		std::unordered_map<std::uint64_t, std::shared_ptr<Texture>> shadowTextures;
	};

	// Executed on the GL thread for each submitted packet (owns the
	// whole frame body: passes, overlay jobs, GUI, present, frame mark).
	using FrameExecutor = std::function<void(FramePacket&)>;
#if PLATFORM_WINDOWS
#pragma warning(push)
#pragma warning(disable: 4251)
#endif
	// Dedicated GL-owning thread (Unity-style render thread split). The game
	// thread gathers a frame's render inputs; this thread owns the GL context
	// and executes batching, passes, ImGui GL, and present. Threading off runs
	// the identical code path inline on the main thread (debug switch).
	class FURY_API RenderThread
	{
	public:

		static RenderThread& Get();

		// Toggle resolution order: CLI flag > FURY_RENDER_THREAD env > Lua
		// Engine.run option > editor ini default > ON.
		static void SetCommandLineOverride(int value); // -1 unset, 0/1
		static void SetIniDefault(int value);          // -1 unset, 0/1
		static bool ResolveEnabled(int luaOption);     // -1 unset, 0/1

		// True while the thread is alive and owns the GL context.
		bool IsRunning() const;

		// True when called on the render thread itself.
		bool OnRenderThread() const;

		// GL may be used when the render thread is not running, or from it.
		bool MayUseGL() const;

		// Hands the GL context from the calling (main) thread to the new
		// render thread and starts its loop.
		void Start(sf::Window &window, FrameExecutor executor);

		// Same entry as Start() for the synchronous (threading-off) path:
		// runs one frame inline on the calling thread.
		void ExecuteInline(FramePacket &packet);

		// Sets the frame executor for the inline path (no thread started).
		void SetExecutor(FrameExecutor executor);

		// -- frame packets (game thread) -----------------------------------
		FramePacket *AcquirePacket();            // pool (2 slots); blocks only pathologically
		void StagePacket(FramePacket *packet);   // Execute() output, consumed by the loop tail
		FramePacket *TakeStagedPacket();         // loop tail; nullptr when nothing staged
		FramePacket *PeekStagedPacket();         // non-consuming peek (editor overlay jobs)
		void SubmitFrame(FramePacket *packet);   // blocks while the previous frame is in flight
		bool PollFrameResult(FrameResult &out);  // drains one mailbox entry (recycles the slot)
		void WaitFrameResult(FrameResult &out);  // blocking variant

		// Loop-frame counter (game thread): BeginLoopFrame once per main
		// loop iteration; gather stamps packets with CurrentFrameIndex
		// (drives the particle parity double-buffer).
		void BeginLoopFrame();
		std::uint64_t CurrentFrameIndex() const;

		// Drains pending work, joins the thread, and re-activates the GL
		// context on the calling thread.
		void Stop();

		// GL work (uploads, deletes) posted from any thread; runs on the
		// render thread. Direct-executes when the render thread is not
		// running (single-threaded mode / pre-start).
		void EnqueueJob(std::function<void()> job);

		// Blocks until the job queue (and any in-flight frame) is drained.
		void Flush();

		// Marks the packet's back buffer for readback after present; the
		// next frame's executor captures it and the result lands here.
		struct ReadbackRequest
		{
			std::shared_ptr<std::vector<unsigned char>> pixels;
			unsigned int w = 0;
			unsigned int h = 0;
		};
		// Posts a capture-after-present job and blocks for the pixels
		// (screenshot paths; debug tools only).
		bool CaptureBackBuffer(ReadbackRequest &out);

		// Standalone context-handoff validation: the render thread clears +
		// swaps `frames` times with a per-frame center-pixel readback check
		// while the main thread pumps events. Returns 0 on success.
		static int RunSmokeTest(sf::Window &window, int frames);

	private:

		void ThreadMain(sf::Window *window);

		std::thread m_Thread;

		std::thread::id m_ThreadId;

		FrameExecutor m_Executor;

		FramePacket *m_InFlight = nullptr;   // guarded by m_Mutex

		std::condition_variable m_DoneCv;

		std::vector<FrameResult> m_Results;
		size_t m_ResultHead = 0;

		FramePacket *m_StagedPacket = nullptr;   // game-thread only

		FramePacket m_PacketPool[2];

		FramePacket *m_FreePackets[2] = { &m_PacketPool[0], &m_PacketPool[1] };

		int m_FreeCount = 2;

		std::uint64_t m_LoopFrameIndex = 0;

		std::atomic<bool> m_Running{ false };

		std::mutex m_Mutex;

		std::condition_variable m_Cv;

		std::vector<std::function<void()>> m_Jobs;

		bool m_Quit = false;

		// Startup handshake: set by the render thread after setActive().
		bool m_StartDone = false;

		bool m_StartOk = false;

		sf::Window *m_Window = nullptr;
	};
#if PLATFORM_WINDOWS
#pragma warning(pop)
#endif
}

// Fires when GL is used from a thread that does not own the context while
// the render thread is running. Active in every build except Shipping.
#if !defined(FURY_BUILD_SHIPPING)
#define FURY_GL_THREAD_GUARD() ASSERT_MSG(fury::RenderThread::Get().MayUseGL(), "GL call from a thread that does not own the context")
#else
#define FURY_GL_THREAD_GUARD()
#endif

namespace fury
{
	// Runs fn on the GL thread: inline when this thread may use GL, queued
	// otherwise. When T is shared-owned (Entity), the queued call is
	// skipped if the object died first -- a re-dispatched create/upload
	// must never fire on a dead resource.
	template<class T, class F>
	inline void DispatchGL(T *obj, F &&fn)
	{
		auto &rt = RenderThread::Get();
		if (rt.MayUseGL())
		{
			fn();
			return;
		}
		std::weak_ptr<T> weak;
		if constexpr (std::is_base_of_v<std::enable_shared_from_this<T>, T>)
			weak = obj->weak_from_this();
		if (!weak.expired())
		{
			rt.EnqueueJob([weak, fn = std::forward<F>(fn)]() mutable {
				if (auto strong = weak.lock())
					fn();
			});
		}
		else
		{
			// Not shared-owned: lifetime is the caller's contract
			// (load/teardown paths Flush before destruction).
			rt.EnqueueJob(std::forward<F>(fn));
		}
	}
}

#endif // _FURY_RENDER_THREAD_H_
