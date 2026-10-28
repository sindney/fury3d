#ifndef _FURY_GPU_DUMP_H_
#define _FURY_GPU_DUMP_H_

#include <string>

#include "Fury/Macros.h"

namespace fury
{
	struct FramePacket;

	// Frame GPU dump for the --gpu-dump <dir> CLI: render-target readbacks
	// (textures/*.png, per array slice) plus a render-graph JSON derived from
	// the pipeline's live passes and textures (passes/edges/chain), which the
	// editor's render-graph window also consumes. Runs on the render thread
	// after the dumped frame's Present (device calls are render-thread bound).
	class FURY_API GpuDump
	{
	public:

		static bool Write(const std::string &outDir, const FramePacket &packet);
	};
}

#endif // _FURY_GPU_DUMP_H_
