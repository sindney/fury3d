#include "Fury/OceanWaves.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include <rapidjson/document.h>

#include "Fury/EnumUtil.h"
#include "Fury/EntityUtil.h"
#include "Fury/GLLoader.h"
#include "Fury/Log.h"
#include "Fury/RenderThread.h"
#include "Fury/Scene.h"
// NOTE: must come after GLLoader.h (windows.h) -- FileUtil.h #undefs the
// WinAPI LoadString macro that would otherwise rewrite FileUtil::LoadString.
#include "Fury/FileUtil.h"

namespace fury
{
	namespace
	{
		float HalfToFloat(unsigned short h)
		{
			unsigned int sign = (h >> 15) & 1;
			unsigned int exp = (h >> 10) & 0x1F;
			unsigned int mant = h & 0x3FF;
			float f;
			if (exp == 0)
				f = std::ldexp(mant / 1024.0f, -14);
			else if (exp == 31)
				f = 60000.0f; // bake clamps to +-60000; treat inf/nan as that
			else
				f = std::ldexp(1.0f + mant / 1024.0f, (int)exp - 15);
			return sign ? -f : f;
		}

		// Inputs explicit so the caller can dispatch this with CPU data captured by value.
		void UploadBandTextures(const Texture::Ptr &dispTex, const Texture::Ptr &nrmTex,
			int n, int frames,
			const std::vector<float> &disp, const std::vector<unsigned char> &nrm)
		{
			dispTex->SetWrapMode(WrapMode::REPEAT);
			glBindTexture(GL_TEXTURE_2D_ARRAY, dispTex->GetID());
			for (int f = 0; f < frames; f++)
			{
				glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, f, n, n, 1,
					GL_RGBA, GL_FLOAT, disp.data() + (size_t)f * n * n * 4);
			}
			glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
			glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

			nrmTex->SetWrapMode(WrapMode::REPEAT);
			glBindTexture(GL_TEXTURE_2D_ARRAY, nrmTex->GetID());
			for (int f = 0; f < frames; f++)
			{
				glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, f, n, n, 1,
					GL_RGBA, GL_UNSIGNED_BYTE, nrm.data() + (size_t)f * n * n * 4);
			}
			glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
			glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		}

		bool ReadFileBytes(const std::string &path, std::vector<unsigned char> &out)
		{
			std::ifstream stream(path, std::ios::binary);
			if (!stream.good())
				return false;
			stream.seekg(0, std::ios::end);
			auto size = stream.tellg();
			stream.seekg(0, std::ios::beg);
			out.resize((size_t)size);
			stream.read(reinterpret_cast<char*>(out.data()), size);
			return stream.good() || stream.gcount() == size;
		}
	}

	OceanWaves::Ptr OceanWaves::Create(const std::string &name)
	{
		return std::make_shared<OceanWaves>(name);
	}

	OceanWaves::Ptr OceanWaves::Resolve(const std::string &path)
	{
		if (path.empty() || Scene::Active == nullptr)
			return nullptr;
		auto em = Scene::Active->GetEntityManager();
		if (!em)
			return nullptr;
		if (auto existing = em->Get<OceanWaves>(path))
		{
			existing->LoadWaves();
			return existing->IsValid() ? existing : nullptr;
		}
		auto waves = OceanWaves::Create(path);
		waves->SetPath(path);
		if (!waves->LoadWaves())
			return nullptr;
		if (!em->Add(waves))
			return em->Get<OceanWaves>(path);
		return waves;
	}

	OceanWaves::OceanWaves(const std::string &name)
		: Entity(name)
	{
		m_TypeIndex = typeid(OceanWaves);
	}

	OceanWaves::~OceanWaves()
	{
	}

	void OceanWaves::SetPath(const std::string &path)
	{
		m_FilePath = path;
		m_Name = PathBasename(path);
	}

	bool OceanWaves::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "OceanWaves: json node is not an object!";
			return false;
		}
		if (!Entity::Load(wrapper, false))
			return false;

		LoadMemberValue(wrapper, "file_path", m_FilePath);
		if (!m_FilePath.empty())
			m_Name = PathBasename(m_FilePath);

		LoadWaves();
		return true;
	}

	void OceanWaves::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		Entity::Save(wrapper, false);

		SaveKey(wrapper, "file_path"); SaveValue(wrapper, m_FilePath);

		if (object)
			EndObject(wrapper);
	}

	bool OceanWaves::LoadWaves()
	{
		if (m_FilePath.empty())
			return false;
		if (IsValid())
			return true;

		std::string jsonText;
		if (!FileUtil::LoadString(Scene::ResolveAsset(m_FilePath), jsonText))
		{
			FURYW << "OceanWaves: sidecar not found: " << m_FilePath;
			return false;
		}

		rapidjson::Document doc;
		doc.Parse(jsonText.c_str());
		if (doc.HasParseError() || !doc.IsObject() ||
			!doc.HasMember("frames") || !doc.HasMember("loopSeconds") ||
			!doc.HasMember("bands"))
		{
			FURYW << "OceanWaves: malformed sidecar: " << m_FilePath;
			return false;
		}

		m_Frames = doc["frames"].GetInt();
		m_LoopSeconds = doc["loopSeconds"].GetFloat();
		if (m_Frames < 2 || m_LoopSeconds <= 0.0f)
		{
			FURYW << "OceanWaves: bad loop info in " << m_FilePath;
			return false;
		}

		// band payloads live beside the sidecar
		std::string dir;
		auto slash = m_FilePath.find_last_of('/');
		if (slash != std::string::npos)
			dir = m_FilePath.substr(0, slash + 1);

		const rapidjson::Value &bands = doc["bands"];
		m_Bands.clear();
		for (auto it = bands.MemberBegin(); it != bands.MemberEnd(); ++it)
		{
			const rapidjson::Value &src = it->value;
			Band band;
			band.Resolution = src["resolution"].GetInt();
			band.TileCm = src["tileCm"].GetFloat();
			if (src.HasMember("maxFoam"))
				band.MaxFoam = src["maxFoam"].GetFloat();
			std::string dispPath = dir + src["dispFile"].GetString();
			std::string nrmPath = dir + src["nrmFile"].GetString();

			int n = band.Resolution;
			size_t count = (size_t)m_Frames * n * n * 4;

			std::vector<unsigned char> dispRaw, nrmRaw;
			if (!ReadFileBytes(Scene::ResolveAsset(dispPath), dispRaw) ||
				dispRaw.size() != count * 2)
			{
				FURYW << "OceanWaves: bad displacement payload " << dispPath;
				m_Bands.clear();
				return false;
			}
			if (!ReadFileBytes(Scene::ResolveAsset(nrmPath), nrmRaw) ||
				nrmRaw.size() != count)
			{
				FURYW << "OceanWaves: bad normal payload " << nrmPath;
				m_Bands.clear();
				return false;
			}

			band.Disp.resize(count);
			const unsigned short *halfs = reinterpret_cast<const unsigned short*>(dispRaw.data());
			for (size_t i = 0; i < count; i++)
				band.Disp[i] = HalfToFloat(halfs[i]);

			// headless CLI: no GL function pointers loaded, skip the GL path
			if (gl::HasGLContext())
			{
				band.DispTexture = Texture::Create("ocean_disp");
				band.DispTexture->CreateEmpty(n, n, m_Frames, TextureFormat::RGBA16F,
					TextureType::TEXTURE_2D_ARRAY, true);
				band.NrmTexture = Texture::Create("ocean_nrm");
				band.NrmTexture->CreateEmpty(n, n, m_Frames, TextureFormat::RGBA8,
					TextureType::TEXTURE_2D_ARRAY, true);

				// single-threaded / calling thread owns GL: upload inline
				if (RenderThread::Get().MayUseGL())
				{
					UploadBandTextures(band.DispTexture, band.NrmTexture,
						n, m_Frames, band.Disp, nrmRaw);
				}
				// render-thread mode, game thread: queue - FIFO after CreateEmpty's own dispatch
				else
				{
					std::vector<float> dispCopy = band.Disp;
					std::vector<unsigned char> nrmCopy = nrmRaw;
					Texture::Ptr dispTex = band.DispTexture;
					Texture::Ptr nrmTex = band.NrmTexture;
					RenderThread::Get().EnqueueJob(
						[dispTex, nrmTex, n, frames = m_Frames,
						 dispCopy = std::move(dispCopy),
						 nrmCopy = std::move(nrmCopy)]() mutable
					{
						UploadBandTextures(dispTex, nrmTex, n, frames, dispCopy, nrmCopy);
					});
				}
			}

			m_Bands.push_back(std::move(band));
		}

		if (m_Bands.empty())
		{
			FURYW << "OceanWaves: no bands in " << m_FilePath;
			return false;
		}

		// invariant: bands sorted by tile ascending, so role lookup never
		// depends on the sidecar's member order (2-band legacy assets are
		// [ripple, swell]; 3-band assets are [chop, ripple, swell])
		std::sort(m_Bands.begin(), m_Bands.end(),
			[](const Band &a, const Band &b) { return a.TileCm < b.TileCm; });

		FURYD << "OceanWaves: loaded " << m_FilePath << " [" << m_Bands.size()
			<< " bands, " << m_Frames << " frames x " << m_LoopSeconds << "s]";
		return true;
	}

	void OceanWaves::SetLoopInfo(int frames, float loopSeconds)
	{
		m_Frames = frames;
		m_LoopSeconds = loopSeconds;
	}

	void OceanWaves::SetBand(int index, Band &&band)
	{
		if (index < 0)
			return;
		if (index >= (int)m_Bands.size())
			m_Bands.resize(index + 1);
		m_Bands[index] = std::move(band);
	}

	float OceanWaves::GetDispValue(int band, int frame, int i, int j, int channel) const
	{
		if (band < 0 || band >= (int)m_Bands.size())
			return 0.0f;
		const Band &b = m_Bands[band];
		int n = b.Resolution;
		if (frame < 0 || frame >= m_Frames || i < 0 || i >= n || j < 0 || j >= n ||
			channel < 0 || channel > 3)
			return 0.0f;
		return b.Disp[((size_t)frame * n * n + (i * n + j)) * 4 + channel];
	}
}
