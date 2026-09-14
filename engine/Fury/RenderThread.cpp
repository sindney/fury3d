#include "Fury/RenderThread.h"

#include <SFML/Window/Window.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <future>
#include <iostream>

#include "Fury/GLLoader.h"
#include "Fury/Log.h"
#include "Fury/Profiler.h"

namespace fury
{
	namespace
	{
		// Toggle sources. -1 = unset; resolution order in ResolveEnabled.
		int s_CliOverride = -1;

		int s_IniDefault = -1;
	}

	RenderThread& RenderThread::Get()
	{
		static RenderThread s_Instance;
		return s_Instance;
	}

	void RenderThread::SetCommandLineOverride(int value)
	{
		s_CliOverride = value;
	}

	void RenderThread::SetIniDefault(int value)
	{
		s_IniDefault = value;
	}

	bool RenderThread::ResolveEnabled(int luaOption)
	{
		if (s_CliOverride != -1)
			return s_CliOverride != 0;

		if (const char *env = std::getenv("FURY_RENDER_THREAD"))
		{
			if (env[0] == '0')
				return false;
			if (env[0] == '1')
				return true;
		}

		if (luaOption != -1)
			return luaOption != 0;

		if (s_IniDefault != -1)
			return s_IniDefault != 0;

		return true;
	}

	bool RenderThread::IsRunning() const
	{
		return m_Running.load(std::memory_order_acquire);
	}

	bool RenderThread::OnRenderThread() const
	{
		return IsRunning() && std::this_thread::get_id() == m_ThreadId;
	}

	bool RenderThread::MayUseGL() const
	{
		return !IsRunning() || OnRenderThread();
	}

	void RenderThread::Start(sf::Window &window, FrameExecutor executor)
	{
		ASSERT_MSG(!IsRunning(), "RenderThread::Start called twice");
		ASSERT_MSG(!m_Thread.joinable(), "RenderThread::Start without Stop");

		m_Executor = std::move(executor);

		// Hand the context over. SFML serializes: deactivate here, the
		// render thread activates on its side (m_StartDone gates the return
		// so the caller never races ahead of the handoff).
		window.setActive(false);

		m_Window = &window;
		m_Quit = false;
		m_StartDone = false;
		m_StartOk = false;
		m_Thread = std::thread([this]() { ThreadMain(m_Window); });

		std::unique_lock<std::mutex> lock(m_Mutex);
		m_Cv.wait(lock, [this]() { return m_StartDone; });
		if (!m_StartOk)
		{
			lock.unlock();
			m_Thread.join();
			m_Window = nullptr;
			window.setActive(true);
			ASSERT_MSG(false, "RenderThread failed to acquire the GL context");
		}
	}

	void RenderThread::Stop()
	{
		if (!m_Thread.joinable())
			return;

		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Quit = true;
		}
		m_Cv.notify_one();
		m_Thread.join();

		m_Running.store(false, std::memory_order_release);
		if (m_Window != nullptr)
		{
			m_Window->setActive(true);
			m_Window = nullptr;
		}
		m_Executor = nullptr;
		m_InFlight = nullptr;
		// Drop per-frame payload clones (GUI draw data) before ImGui
		// context teardown.
		m_PacketPool[0].Reset();
		m_PacketPool[1].Reset();
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Results.clear();
			m_ResultHead = 0;
			m_FreeCount = 2;
			m_FreePackets[0] = &m_PacketPool[0];
			m_FreePackets[1] = &m_PacketPool[1];
			m_StagedPacket = nullptr;
		}
	}

	void RenderThread::EnqueueJob(std::function<void()> job)
	{
		// Direct-execute on the GL-owning thread (or when not running);
		// only foreign threads queue.
		if (!IsRunning() || OnRenderThread())
		{
			job();
			return;
		}
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Jobs.emplace_back(std::move(job));
		}
		m_Cv.notify_one();
	}

	void RenderThread::Flush()
	{
		if (!IsRunning())
			return;

		std::promise<void> drained;
		std::future<void> wait = drained.get_future();
		EnqueueJob([&drained]() { drained.set_value(); });
		wait.wait();

		std::unique_lock<std::mutex> lock(m_Mutex);
		m_DoneCv.wait(lock, [this]() { return m_InFlight == nullptr || m_Quit; });
	}

	void RenderThread::ThreadMain(sf::Window *window)
	{
		m_ThreadId = std::this_thread::get_id();
		FURY_SET_THREAD_NAME("render");

		bool acquired = window->setActive(true);
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_StartOk = acquired;
			m_StartDone = true;
		}
		m_Cv.notify_one();
		if (!acquired)
			return;

		// The Tracy GPU context binds to the thread that owns GL.
		FURY_GPU_CONTEXT();

		m_Running.store(true, std::memory_order_release);

		for (;;)
		{
			std::vector<std::function<void()>> jobs;
			FramePacket *packet = nullptr;
			{
				std::unique_lock<std::mutex> lock(m_Mutex);
				m_Cv.wait(lock, [this]() { return m_Quit || !m_Jobs.empty() || m_InFlight != nullptr; });
				if (m_Quit)
					break;
				jobs.swap(m_Jobs);
				packet = m_InFlight;
			}
			for (auto &job : jobs)
				job();
			if (packet != nullptr)
			{
				m_Executor(*packet);
				{
					std::lock_guard<std::mutex> lock(m_Mutex);
					m_InFlight = nullptr;
					FrameResult result(packet);
					for (size_t i = 0; i < packet->lights.size(); ++i)
						if (packet->shadowResults[i].texture)
							result.shadowTextures[packet->lights[i].nodeKey] =
								packet->shadowResults[i].texture;
					m_Results.push_back(std::move(result));
				}
				m_DoneCv.notify_all();
			}
		}

		m_Running.store(false, std::memory_order_release);
		window->setActive(false);
	}

	void RenderThread::ExecuteInline(FramePacket &packet)
	{
		ASSERT_MSG(!IsRunning(), "ExecuteInline while the render thread is running");
		ASSERT_MSG(m_Executor != nullptr, "ExecuteInline without an executor");
		m_Executor(packet);
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			FrameResult result(&packet);
			for (size_t i = 0; i < packet.lights.size(); ++i)
				if (packet.shadowResults[i].texture)
					result.shadowTextures[packet.lights[i].nodeKey] =
						packet.shadowResults[i].texture;
			m_Results.push_back(std::move(result));
		}
	}

	void RenderThread::SetExecutor(FrameExecutor executor)
	{
		ASSERT_MSG(!IsRunning(), "SetExecutor while the render thread is running");
		m_Executor = std::move(executor);
	}

	FramePacket *RenderThread::AcquirePacket()
	{
		for (;;)
		{
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				if (m_FreeCount > 0)
					return m_FreePackets[--m_FreeCount];
			}
			FrameResult fr;
			if (!PollFrameResult(fr))
				WaitFrameResult(fr);
		}
	}

	void RenderThread::StagePacket(FramePacket *packet)
	{
		// Game-thread only. A second Execute in one loop iteration drops
		// the earlier packet back into the pool.
		if (m_StagedPacket != nullptr && m_StagedPacket != packet)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_FreePackets[m_FreeCount++] = m_StagedPacket;
		}
		m_StagedPacket = packet;
	}

	FramePacket *RenderThread::PeekStagedPacket()
	{
		return m_StagedPacket;
	}

	FramePacket *RenderThread::TakeStagedPacket()
	{
		FramePacket *packet = m_StagedPacket;
		m_StagedPacket = nullptr;
		return packet;
	}

	void RenderThread::SubmitFrame(FramePacket *packet)
	{
		if (!IsRunning())
		{
			ExecuteInline(*packet);
			return;
		}
		std::unique_lock<std::mutex> lock(m_Mutex);
		m_DoneCv.wait(lock, [this]() { return m_InFlight == nullptr || m_Quit; });
		if (m_Quit)
		{
			m_FreePackets[m_FreeCount++] = packet;
			return;
		}
		m_InFlight = packet;
		lock.unlock();
		m_Cv.notify_one();
	}

	bool RenderThread::PollFrameResult(FrameResult &out)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (m_ResultHead >= m_Results.size())
			return false;
		out = m_Results[m_ResultHead++];
		if (m_ResultHead >= m_Results.size())
		{
			m_Results.clear();
			m_ResultHead = 0;
		}
		m_FreePackets[m_FreeCount++] = out.packet;
		return true;
	}

	void RenderThread::WaitFrameResult(FrameResult &out)
	{
		std::unique_lock<std::mutex> lock(m_Mutex);
		m_DoneCv.wait(lock, [this]()
		{
			return m_ResultHead < m_Results.size() || m_Quit;
		});
		if (m_ResultHead < m_Results.size())
		{
			out = m_Results[m_ResultHead++];
			if (m_ResultHead >= m_Results.size())
			{
				m_Results.clear();
				m_ResultHead = 0;
			}
			m_FreePackets[m_FreeCount++] = out.packet;
		}
	}

	void RenderThread::BeginLoopFrame()
	{
		++m_LoopFrameIndex;
	}

	std::uint64_t RenderThread::CurrentFrameIndex() const
	{
		return m_LoopFrameIndex;
	}

	bool RenderThread::CaptureBackBuffer(ReadbackRequest &out)
	{
		if (!IsRunning())
			return false;
		out.pixels = std::make_shared<std::vector<unsigned char>>();
		std::promise<bool> done;
		std::future<bool> wait = done.get_future();
		auto pixels = out.pixels;
		unsigned int *w = &out.w;
		unsigned int *h = &out.h;
		EnqueueJob([this, pixels, w, h, &done]()
		{
			if (m_Window == nullptr)
			{
				done.set_value(false);
				return;
			}
			const sf::Vector2u sz = m_Window->getSize();
			*w = sz.x;
			*h = sz.y;
			if (sz.x == 0 || sz.y == 0)
			{
				done.set_value(false);
				return;
			}
			pixels->resize(static_cast<size_t>(sz.x) * sz.y * 4);
			glReadPixels(0, 0, static_cast<GLsizei>(sz.x), static_cast<GLsizei>(sz.y),
				GL_RGBA, GL_UNSIGNED_BYTE, pixels->data());
			done.set_value(true);
		});
		return wait.get();
	}

	int RenderThread::RunSmokeTest(sf::Window &window, int frames)
	{
		// Lean path: no Engine::Initialize (the Tracy GPU context must be
		// created on the render thread, so the engine's main-thread init is
		// skipped entirely). GL function pointers are loaded by the caller.
		if (frames <= 0)
			frames = 120;

		window.setActive(false);

		std::atomic<int> presented{ 0 };
		std::atomic<int> mismatches{ 0 };

		const sf::Vector2u size = window.getSize();

		std::thread worker([&]()
		{
			FURY_SET_THREAD_NAME("render");
			if (!window.setActive(true))
			{
				std::cerr << "render-thread-smoke: setActive(true) failed on render thread\n";
				mismatches = -1;
				return;
			}
			FURY_GPU_CONTEXT();

			for (int i = 0; i < frames; ++i)
			{
				const float t = static_cast<float>(i) / static_cast<float>(frames);
				const unsigned char r = static_cast<unsigned char>(t * 255.0f);
				const unsigned char b = static_cast<unsigned char>((1.0f - t) * 255.0f);
				glClearColor(t, 0.2f, 1.0f - t, 1.0f);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

				// Read pre-swap (the back buffer is undefined after display):
				// verifies the render thread really produced the pixels.
				unsigned char px[4] = { 0, 0, 0, 0 };
				glReadPixels(static_cast<GLint>(size.x / 2), static_cast<GLint>(size.y / 2),
					1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
				if (std::abs((int)px[0] - (int)r) > 4 || std::abs((int)px[2] - (int)b) > 4
					|| px[1] != 51)
					++mismatches;

				window.display();
				FURY_GPU_COLLECT();
				FURY_FRAME;
				++presented;
			}

			window.setActive(false);
		});

		// Main thread keeps the window responsive (macOS requires the window
		// thread to pump events) until the render thread finishes.
		while (presented.load() < frames && mismatches.load() >= 0 && window.isOpen())
		{
			while (const std::optional event = window.pollEvent())
			{
				if (event->is<sf::Event::Closed>())
					window.close();
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		worker.join();
		window.setActive(true);

		if (mismatches.load() != 0)
		{
			std::cerr << "render-thread-smoke: FAILED (" << mismatches.load()
				<< " pixel mismatches or handoff error)\n";
			return 1;
		}
		std::cout << "render-thread-smoke: OK, " << presented.load()
			<< " frames presented from the render thread\n";
		return 0;
	}
}
