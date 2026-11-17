#ifndef _FURY_CLUSTER_RENDERER_H_
#define _FURY_CLUSTER_RENDERER_H_

#include <cstdint>
#include <memory>
#include <string>

#include "Fury/Macros.h"

namespace fury
{
	struct FramePacket;
	class Pass;

	struct ClusterLodStats
	{
		std::uint64_t frameIndex = 0;
		std::uint32_t candidateInstances = 0;
		std::uint32_t fallbackInstances = 0;
		std::uint32_t instances = 0;
		std::uint32_t visibleInstances = 0;
		std::uint32_t selected = 0;
		std::uint32_t visible = 0;
		std::uint32_t commands = 0;
		std::uint32_t batches = 0;
		std::uint32_t drawnTriangles = 0;
		std::uint32_t selectionOverflow = 0;
		std::uint32_t visibilityOverflow = 0;
		std::string unavailableReason;
	};

	class FURY_API ClusterRenderer
	{
	public:
		ClusterRenderer();
		~ClusterRenderer();

		void Prepare(FramePacket &packet, const std::shared_ptr<Pass> &pass);

		void Execute(FramePacket &packet, const std::shared_ptr<Pass> &pass);

		bool ReadStats(ClusterLodStats &out) const;

		void LogDiagnostics(std::uint64_t frameIndex, const FramePacket &packet) const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

#endif // _FURY_CLUSTER_RENDERER_H_
