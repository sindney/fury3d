#ifndef _FURY_HEIGHTMAP_H_
#define _FURY_HEIGHTMAP_H_

#include <memory>
#include <string>
#include <vector>

#include "Fury/Entity.h"

namespace fury
{
	// Heightmap asset: a 16-bit .r16 height field + JSON sidecar (resolution,
	// world size in cm, height scale), registered in the scene's
	// EntityManager and referenced by name from Terrain components. The scene
	// file stores only the path + params; heights re-decode from the .r16 on
	// load (never serialized inline). Pure CPU data - no GL needed.
	class FURY_API Heightmap : public Entity
	{
	public:

		typedef std::shared_ptr<Heightmap> Ptr;

		static Ptr Create(const std::string &name);

		Heightmap(const std::string &name);

		virtual ~Heightmap();

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		const std::string &GetFilePath() const { return m_FilePath; }
		void SetFilePath(const std::string &path) { m_FilePath = path; }

		// Reads the .r16 + sidecar from disk (scene-working-dir relative).
		// Idempotent; returns false (logged) on missing/malformed files.
		bool LoadHeights();

		bool HasHeights() const { return !m_Heights.empty(); }

		int GetResolution() const { return m_Resolution; }

		float GetWorldSizeX() const { return m_WorldSizeX; }
		float GetWorldSizeZ() const { return m_WorldSizeZ; }
		float GetHeightScale() const { return m_HeightScale; }

		// heights in cm, row-major [z * N + x]
		const std::vector<float> &GetHeights() const { return m_Heights; }

	protected:

		std::string m_FilePath;

		int m_Resolution = 0;

		float m_WorldSizeX = 0.0f;

		float m_WorldSizeZ = 0.0f;

		float m_HeightScale = 0.0f;

		std::vector<float> m_Heights;
	};
}

#endif // _FURY_HEIGHTMAP_H_
