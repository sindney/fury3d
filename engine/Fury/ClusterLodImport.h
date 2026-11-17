#ifndef _FURY_CLUSTERLODIMPORT_H_
#define _FURY_CLUSTERLODIMPORT_H_

#include <memory>
#include <string>

#include "Fury/Macros.h"

namespace fury
{
	class DdcStore;
	class Mesh;

	FURY_API bool IsClusterLodEligibleMesh(const Mesh &mesh);

	FURY_API bool PrepareClusterLodForImport(const std::shared_ptr<Mesh> &mesh,
		DdcStore &ddc, bool &cache_hit, std::string &error);
}

#endif // _FURY_CLUSTERLODIMPORT_H_
