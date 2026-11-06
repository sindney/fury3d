#include "Fury/SceneFingerprint.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <typeindex>

#include "Fury/AnimationClip.h"
#include "Fury/AnimationPlayer.h"
#include "Fury/AnimationState.h"
#include "Fury/BodySetup.h"
#include "Fury/BuoyancyComponent.h"
#include "Fury/Camera.h"
#include "Fury/CharacterController.h"
#include "Fury/EntityManager.h"
#include "Fury/EnumUtil.h"
#include "Fury/Heightmap.h"
#include "Fury/InstancedMeshRender.h"
#include "Fury/Joint.h"
#include "Fury/Light.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/OceanComponent.h"
#include "Fury/OceanWaves.h"
#include "Fury/ParticleModules.h"
#include "Fury/ParticleRenderer.h"
#include "Fury/ParticleSystem.h"
#include "Fury/PlayerController.h"
#include "Fury/RenderSettings.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/SkyAtmosphere.h"
#include "Fury/Terrain.h"
#include "Fury/Texture.h"
#include "Fury/Transform.h"
#include "Fury/Uniform.h"

namespace fury
{
	// ------------------------------------------------------------------
	// FpNode
	// ------------------------------------------------------------------

	FpNode FpNode::Obj()
	{
		FpNode n;
		n.kind = Kind::Object;
		return n;
	}

	FpNode FpNode::Arr()
	{
		FpNode n;
		n.kind = Kind::Array;
		return n;
	}

	FpNode FpNode::Str(const std::string &value)
	{
		FpNode n;
		n.kind = Kind::String;
		n.scalar = value;
		return n;
	}

	static std::string RawReal(double value)
	{
		if (std::isnan(value)) return "nan";
		if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.9g", value);
		return buf;
	}

	FpNode FpNode::Int(long long value)
	{
		FpNode n;
		n.kind = Kind::Integer;
		n.integer = value;
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%lld", value);
		n.scalar = "i" + std::string(buf);
		n.raw = n.scalar;
		return n;
	}

	FpNode FpNode::UInt(unsigned long long value)
	{
		FpNode n;
		n.kind = Kind::Integer;
		n.integer = static_cast<long long>(value);
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%llu", value);
		n.scalar = "i" + std::string(buf);
		n.raw = n.scalar;
		return n;
	}

	FpNode FpNode::Real(double value)
	{
		FpNode n;
		n.kind = Kind::Real;
		n.real = value;
		n.raw = RawReal(value);
		if (std::isnan(value))
			n.scalar = "fnan";
		else if (std::isinf(value))
			n.scalar = value > 0 ? "finf" : "f-ninf";
		else
			n.scalar = "f" + n.raw;
		return n;
	}

	FpNode FpNode::Bool(bool value)
	{
		FpNode n;
		n.kind = Kind::Boolean;
		n.boolean = value;
		n.scalar = value ? "true" : "false";
		n.raw = n.scalar;
		return n;
	}

	FpNode FpNode::Nil()
	{
		return FpNode();
	}

	FpNode &FpNode::Set(const std::string &key, const FpNode &value)
	{
		object[key] = value;
		return *this;
	}

	const FpNode *FpNode::Find(const std::string &key) const
	{
		if (kind != Kind::Object)
			return nullptr;
		auto it = object.find(key);
		return it == object.end() ? nullptr : &it->second;
	}

	// ------------------------------------------------------------------
	// Canonical text form
	//
	// Grammar (single line, no whitespace):
	//   node   := '{' pair '}' | '[' node * ']' | '"' escaped '"'
	//           | 'i' '-'? digits | 'f' '-'? digits | 'true' | 'false' | 'null'
	//   pair   := '"' key '"' ':' node (',' pair)*
	// Reals carry the raw decimal in the diff report only, never in the
	// canonical token, so tolerance-quantized values are what gets compared.
	// ------------------------------------------------------------------

	static void CanonicalizeInto(const FpNode &node, std::string &out)
	{
		switch (node.kind)
		{
		case FpNode::Kind::Object:
		{
			out += '{';
			bool first = true;
			for (const auto &kv : node.object)
			{
				if (!first) out += ',';
				first = false;
				out += '"';
				out += kv.first;
				out += "\":";
				CanonicalizeInto(kv.second, out);
			}
			out += '}';
			break;
		}
		case FpNode::Kind::Array:
		{
			out += '[';
			for (size_t i = 0; i < node.array.size(); ++i)
			{
				if (i > 0) out += ',';
				CanonicalizeInto(node.array[i], out);
			}
			out += ']';
			break;
		}
		case FpNode::Kind::String:
		{
			out += '"';
			for (char c : node.scalar)
			{
				if (c == '"' || c == '\\') { out += '\\'; out += c; }
				else if (c == '\n') out += "\\n";
				else out += c;
			}
			out += '"';
			break;
		}
		case FpNode::Kind::Integer:
			out += node.scalar;
			break;
		case FpNode::Kind::Real:
			out += node.scalar;
			break;
		case FpNode::Kind::Boolean:
			out += node.scalar;
			break;
		case FpNode::Kind::Null:
			out += "null";
			break;
		}
	}

	std::string SceneFingerprint::Canonicalize(const FpNode &node)
	{
		std::string out;
		CanonicalizeInto(node, out);
		return out;
	}

	namespace
	{
		class FpParser
		{
		public:
			FpParser(const std::string &text) : m_Text(text) {}

			bool Parse(FpNode &out)
			{
				if (!ParseNode(out)) return false;
				return m_Pos == m_Text.size();
			}

		private:
			const std::string &m_Text;
			size_t m_Pos = 0;

			char Peek() const { return m_Pos < m_Text.size() ? m_Text[m_Pos] : '\0'; }

			bool Consume(char c)
			{
				if (Peek() == c) { ++m_Pos; return true; }
				return false;
			}

			bool ParseNode(FpNode &out)
			{
				switch (Peek())
				{
				case '{': return ParseObject(out);
				case '[': return ParseArray(out);
				case '"': return ParseStringNode(out);
				case 'i':
				{
					const size_t start = m_Pos++;
					Consume('-');
					if (Peek() < '0' || Peek() > '9') return false;
					while (Peek() >= '0' && Peek() <= '9') ++m_Pos;
					out.kind = FpNode::Kind::Integer;
					out.scalar = m_Text.substr(start, m_Pos - start);
					out.raw = out.scalar;
					out.integer = std::strtoll(out.scalar.c_str() + 1, nullptr, 10);
					return true;
				}
				case 'f':
					if (m_Text.compare(m_Pos, 5, "false") == 0) { m_Pos += 5; out = FpNode::Bool(false); return true; }
					if (m_Text.compare(m_Pos, 4, "finf") == 0) { m_Pos += 4; out = FpNode::Real(INFINITY); return true; }
					if (m_Text.compare(m_Pos, 6, "f-ninf") == 0) { m_Pos += 6; out = FpNode::Real(-INFINITY); return true; }
					if (m_Text.compare(m_Pos, 4, "fnan") == 0) { m_Pos += 4; out = FpNode::Real(NAN); return true; }
					++m_Pos;
					{
						const char *start = m_Text.c_str() + m_Pos;
						char *end = nullptr;
						const double v = std::strtod(start, &end);
						if (end == start) return false;
						m_Pos += static_cast<size_t>(end - start);
						out = FpNode::Real(v);
						return true;
					}
				default:
					if (m_Text.compare(m_Pos, 4, "true") == 0) { m_Pos += 4; out = FpNode::Bool(true); return true; }
					if (m_Text.compare(m_Pos, 4, "null") == 0) { m_Pos += 4; out = FpNode::Nil(); return true; }
					return false;
				}
			}

			bool ParseEscaped(std::string &out)
			{
				if (!Consume('"')) return false;
				out.clear();
				while (m_Pos < m_Text.size() && Peek() != '"')
				{
					char c = m_Text[m_Pos++];
					if (c == '\\' && m_Pos < m_Text.size())
					{
						char e = m_Text[m_Pos++];
						if (e == 'n') out += '\n';
						else out += e;
					}
					else out += c;
				}
				return Consume('"');
			}

			bool ParseStringNode(FpNode &out)
			{
				std::string s;
				if (!ParseEscaped(s)) return false;
				out = FpNode::Str(s);
				return true;
			}

			bool ParseObject(FpNode &out)
			{
				if (!Consume('{')) return false;
				FpNode obj = FpNode::Obj();
				if (Consume('}')) { out = obj; return true; }
				while (true)
				{
					std::string key;
					if (!ParseEscaped(key)) return false;
					if (!Consume(':')) return false;
					FpNode value;
					if (!ParseNode(value)) return false;
					obj.object[key] = value;
					if (Consume(',')) continue;
					if (Consume('}')) { out = obj; return true; }
					return false;
				}
			}

			bool ParseArray(FpNode &out)
			{
				if (!Consume('[')) return false;
				FpNode arr = FpNode::Arr();
				if (Consume(']')) { out = arr; return true; }
				while (true)
				{
					FpNode value;
					if (!ParseNode(value)) return false;
					arr.array.push_back(value);
					if (Consume(',')) continue;
					if (Consume(']')) { out = arr; return true; }
					return false;
				}
			}
		};
	}

	bool SceneFingerprint::Parse(const std::string &text, FpNode &out)
	{
		FpParser parser(text);
		return parser.Parse(out);
	}

	// ------------------------------------------------------------------
	// Diff
	// ------------------------------------------------------------------

	namespace
	{
		bool ScalarEqual(const FpNode &a, const FpNode &b)
		{
			if (a.kind != b.kind) return false;
			return a.scalar == b.scalar;
		}

		// Abs/relative float tolerance; NaN matches NaN.
		bool RealsEqual(double a, double b)
		{
			if (std::isnan(a) || std::isnan(b)) return std::isnan(a) && std::isnan(b);
			if (a == b) return true;
			const double tol = SceneFingerprint::kFloatTolerance;
			return std::fabs(a - b) <= tol * std::max(1.0, std::max(std::fabs(a), std::fabs(b)));
		}

		std::string PathJoin(const std::string &base, const std::string &key)
		{
			return base.empty() ? key : base + "/" + key;
		}

		std::string PathIndex(const std::string &base, size_t index)
		{
			return base + "[" + std::to_string(index) + "]";
		}

		void DiffInto(const FpNode &a, const FpNode &b, const std::string &path,
			FingerprintDiff &out)
		{
			if (a.kind == FpNode::Kind::Object && b.kind == FpNode::Kind::Object)
			{
				auto ia = a.object.begin();
				auto ib = b.object.begin();
				while (ia != a.object.end() || ib != b.object.end())
				{
					if (ia != a.object.end() && ib != b.object.end() && ia->first == ib->first)
					{
						DiffInto(ia->second, ib->second, PathJoin(path, ia->first), out);
						++ia; ++ib;
					}
					else if (ib == b.object.end() || (ia != a.object.end() && ia->first < ib->first))
					{
						DiffInto(ia->second, FpNode::Nil(), PathJoin(path, ia->first), out);
						++ia;
					}
					else
					{
						DiffInto(FpNode::Nil(), ib->second, PathJoin(path, ib->first), out);
						++ib;
					}
				}
				return;
			}

			if (a.kind == FpNode::Kind::Array && b.kind == FpNode::Kind::Array)
			{
				const size_t n = std::max(a.array.size(), b.array.size());
				for (size_t i = 0; i < n; ++i)
				{
					const FpNode *pa = i < a.array.size() ? &a.array[i] : nullptr;
					const FpNode *pb = i < b.array.size() ? &b.array[i] : nullptr;
					const std::string p = PathIndex(path, i);
					if (pa && pb) DiffInto(*pa, *pb, p, out);
					else if (pa) DiffInto(*pa, FpNode::Nil(), p, out);
					else DiffInto(FpNode::Nil(), *pb, p, out);
				}
				return;
			}

			if (a.kind == FpNode::Kind::Real && b.kind == FpNode::Kind::Real)
			{
				if (RealsEqual(a.real, b.real))
					return;
			}
			else if (ScalarEqual(a, b))
			{
				return;
			}

			FingerprintDiffEntry entry;
			entry.path = path.empty() ? "<root>" : path;
			entry.first = a.IsNull() ? "<missing>" : a.scalar;
			entry.second = b.IsNull() ? "<missing>" : b.scalar;
			entry.firstRaw = a.raw.empty() ? entry.first : a.raw;
			entry.secondRaw = b.raw.empty() ? entry.second : b.raw;
			out.entries.push_back(std::move(entry));
			out.equal = false;
		}
	}

	FingerprintDiff SceneFingerprint::Diff(const FpNode &first, const FpNode &second)
	{
		FingerprintDiff out;
		DiffInto(first, second, "", out);
		return out;
	}

	FingerprintDiff SceneFingerprint::DiffStrings(const std::string &first, const std::string &second)
	{
		FingerprintDiff out;
		FpNode a, b;
		const bool okA = Parse(first, a);
		const bool okB = Parse(second, b);
		if (!okA || !okB)
		{
			out.equal = false;
			FingerprintDiffEntry entry;
			entry.path = "<parse>";
			entry.first = okA ? "ok" : "malformed";
			entry.second = okB ? "ok" : "malformed";
			out.entries.push_back(std::move(entry));
			return out;
		}
		return Diff(a, b);
	}

	// ------------------------------------------------------------------
	// Visitor helpers
	// ------------------------------------------------------------------

	namespace
	{
		// Relative float tolerance buckets, shared with FpNode::Real.
		long long QuantBucket(double value)
		{
			const double tol = SceneFingerprint::kFloatTolerance;
			const double scale = std::max(1.0, std::fabs(value));
			return static_cast<long long>(std::llround(value / (scale * tol)));
		}

		FpNode Vec(const Vector4 &v)
		{
			FpNode n = FpNode::Arr();
			n.array.push_back(FpNode::Real(v.x));
			n.array.push_back(FpNode::Real(v.y));
			n.array.push_back(FpNode::Real(v.z));
			n.array.push_back(FpNode::Real(v.w));
			return n;
		}

		FpNode Col(const Color &c)
		{
			FpNode n = FpNode::Arr();
			n.array.push_back(FpNode::Real(c.r));
			n.array.push_back(FpNode::Real(c.g));
			n.array.push_back(FpNode::Real(c.b));
			n.array.push_back(FpNode::Real(c.a));
			return n;
		}

		// q and -q are the same rotation; canonicalize the sign so both
		// compare equal.
		FpNode Quat(const Quaternion &q)
		{
			const float absmax = std::max(std::max(std::fabs(q.x), std::fabs(q.y)),
				std::max(std::fabs(q.z), std::fabs(q.w)));
			const float eps = 1e-12f;
			float sign = 1.0f;
			if (std::fabs(q.w) >= absmax - eps) sign = q.w < 0 ? -1.0f : 1.0f;
			else if (std::fabs(q.x) >= absmax - eps) sign = q.x < 0 ? -1.0f : 1.0f;
			else if (std::fabs(q.y) >= absmax - eps) sign = q.y < 0 ? -1.0f : 1.0f;
			else sign = q.z < 0 ? -1.0f : 1.0f;
			FpNode n = FpNode::Arr();
			n.array.push_back(FpNode::Real(q.x * sign));
			n.array.push_back(FpNode::Real(q.y * sign));
			n.array.push_back(FpNode::Real(q.z * sign));
			n.array.push_back(FpNode::Real(q.w * sign));
			return n;
		}

		FpNode Mat(const Matrix4 &m)
		{
			FpNode n = FpNode::Arr();
			for (int i = 0; i < 16; ++i)
				n.array.push_back(FpNode::Real(m.Raw[i]));
			return n;
		}

		uint64_t FnvaMix(uint64_t h, long long v)
		{
			unsigned long long u = static_cast<unsigned long long>(v);
			for (int i = 0; i < 8; ++i)
			{
				h ^= (u >> (i * 8)) & 0xff;
				h *= 1099511628211ULL;
			}
			return h;
		}

		uint64_t HashFloats(const std::vector<float> &data)
		{
			uint64_t h = 1469598103934665603ULL;
			for (float f : data)
				h = FnvaMix(h, QuantBucket(f));
			return h;
		}

		uint64_t HashUInts(const std::vector<unsigned int> &data)
		{
			uint64_t h = 1469598103934665603ULL;
			for (unsigned int v : data)
				h = FnvaMix(h, static_cast<long long>(v));
			return h;
		}

		std::string HashHex(uint64_t h)
		{
			char buf[24];
			std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
			return buf;
		}

		// Generic uniform read: downcast to the concrete typedef and copy
		// the data out as numbers. Integers stay integers so exactness is
		// preserved.
		FpNode UniformData(const UniformBase::Ptr &u)
		{
			FpNode data = FpNode::Arr();
			if (!u) return data;
			auto tryFloat = [&data](auto typed) -> bool
			{
				if (!typed) return false;
				for (unsigned int i = 0; i < typed->GetSize(); ++i)
					data.array.push_back(FpNode::Real(typed->GetDataAt(i)));
				return true;
			};
			if (tryFloat(std::dynamic_pointer_cast<Uniform1f>(u))) return data;
			if (tryFloat(std::dynamic_pointer_cast<Uniform2f>(u))) return data;
			if (tryFloat(std::dynamic_pointer_cast<Uniform3f>(u))) return data;
			if (tryFloat(std::dynamic_pointer_cast<Uniform4f>(u))) return data;
			if (tryFloat(std::dynamic_pointer_cast<UniformMatrix4fv>(u))) return data;
			auto tryInt = [&data](auto typed) -> bool
			{
				if (!typed) return false;
				for (unsigned int i = 0; i < typed->GetSize(); ++i)
					data.array.push_back(FpNode::Int(typed->GetDataAt(i)));
				return true;
			};
			if (tryInt(std::dynamic_pointer_cast<Uniform1i>(u))) return data;
			if (tryInt(std::dynamic_pointer_cast<Uniform2i>(u))) return data;
			if (tryInt(std::dynamic_pointer_cast<Uniform3i>(u))) return data;
			if (tryInt(std::dynamic_pointer_cast<Uniform4i>(u))) return data;
			if (tryInt(std::dynamic_pointer_cast<Uniform1ui>(u))) return data;
			if (tryInt(std::dynamic_pointer_cast<Uniform2ui>(u))) return data;
			if (tryInt(std::dynamic_pointer_cast<Uniform3ui>(u))) return data;
			if (tryInt(std::dynamic_pointer_cast<Uniform4ui>(u))) return data;
			return data;
		}

		std::string UniformTypeName(const UniformBase::Ptr &u)
		{
			if (!u) return "";
			static const std::vector<std::pair<std::type_index, std::string>> kTypes =
			{
				{ typeid(Uniform1f), "Uniform1f" },
				{ typeid(Uniform2f), "Uniform2f" },
				{ typeid(Uniform3f), "Uniform3f" },
				{ typeid(Uniform4f), "Uniform4f" },
				{ typeid(UniformMatrix4fv), "UniformMatrix4fv" },
				{ typeid(Uniform1i), "Uniform1i" },
				{ typeid(Uniform2i), "Uniform2i" },
				{ typeid(Uniform3i), "Uniform3i" },
				{ typeid(Uniform4i), "Uniform4i" },
				{ typeid(Uniform1ui), "Uniform1ui" },
				{ typeid(Uniform2ui), "Uniform2ui" },
				{ typeid(Uniform3ui), "Uniform3ui" },
				{ typeid(Uniform4ui), "Uniform4ui" },
			};
			const std::type_index ti = u->GetTypeIndex();
			for (const auto &kv : kTypes)
				if (kv.first == ti) return kv.second;
			return "unknown";
		}

		// Stable identity for a path-keyed (or name-keyed) asset.
		std::string AssetKey(const Entity &e)
		{
			const std::string path = e.GetPath();
			if (!path.empty()) return path;
			return "\x01name:" + e.GetName();
		}

		void SetEntityId(FpNode &n, const Entity &e)
		{
			n.Set("name", e.GetName());
			n.Set("uuid", e.GetUUID());
		}

		FpNode FingerprintTexture(const Texture::Ptr &t)
		{
			FpNode n = FpNode::Obj();
			SetEntityId(n, *t);
			n.Set("key", AssetKey(*t));
			const std::string path = t->GetFilePath();
			if (path.empty())
			{
				n.Set("format", EnumUtil::TextureFormatToString(t->GetFormat()));
				n.Set("type", EnumUtil::TextureTypeToString(t->GetType()));
				n.Set("width", t->GetWidth());
				n.Set("height", t->GetHeight());
				n.Set("depth", t->GetDepth());
			}
			else
			{
				n.Set("path", path);
				n.Set("srgb", t->IsSRGB());
			}
			n.Set("borderColor", Col(t->GetBorderColor()));
			n.Set("mipmap", t->GetMipmap());
			n.Set("filter", EnumUtil::FilterModeToString(t->GetFilterMode()));
			n.Set("wrap", EnumUtil::WrapModeToString(t->GetWrapMode()));
			return n;
		}

		FpNode FingerprintShader(const Shader::Ptr &s)
		{
			FpNode n = FpNode::Obj();
			SetEntityId(n, *s);
			n.Set("type", EnumUtil::ShaderTypeToString(s->GetType()));
			std::vector<ShaderTexture> enums;
			EnumUtil::GetShaderTextures(s->GetTextureFlags(), enums);
			FpNode texs = FpNode::Arr();
			for (auto e : enums)
				texs.array.push_back(FpNode::Str(EnumUtil::ShaderTextureToString(e)));
			n.Set("textures", texs);
			FpNode defs = FpNode::Arr();
			for (const auto &d : s->GetDefines())
				defs.array.push_back(FpNode::Str(d));
			n.Set("defines", defs);
			n.Set("path", s->GetFilePath());
			if (s->GetUseGeomShader())
				n.Set("geom", true);
			return n;
		}

		FpNode FingerprintMaterial(const Material::Ptr &m)
		{
			FpNode n = FpNode::Obj();
			SetEntityId(n, *m);
			n.Set("key", AssetKey(*m));
			n.Set("opaque", m->GetOpaque());
			n.Set("alpha_mode", EnumUtil::AlphaModeToString(m->GetAlphaMode()));
			n.Set("alpha_cutoff", m->GetAlphaCutoff());
			n.Set("two_sided", m->GetTwoSided());
			n.Set("wind_enabled", m->GetWindEnabled());
			n.Set("pre_z", m->GetPreZ());
			n.Set("texture_flags", static_cast<unsigned int>(m->GetTextureFlags()));
			FpNode shaders = FpNode::Arr();
			for (unsigned int i = 0; ; ++i)
			{
				auto s = m->GetShaderForPass(i);
				if (!s) break;
				shaders.array.push_back(FingerprintShader(s));
			}
			n.Set("shaders", shaders);
			// Unordered maps canonicalized by slot key.
			FpNode texs = FpNode::Arr();
			{
				std::map<std::string, Texture::Ptr> sorted(m->GetTextures().begin(), m->GetTextures().end());
				for (const auto &kv : sorted)
				{
					FpNode e = FpNode::Obj();
					e.Set("key", kv.first);
					e.Set("texture", kv.second ? AssetKey(*kv.second) : std::string());
					texs.array.push_back(std::move(e));
				}
			}
			n.Set("textures", texs);
			FpNode unis = FpNode::Arr();
			{
				std::map<std::string, UniformBase::Ptr> sorted(m->GetUniforms().begin(), m->GetUniforms().end());
				for (const auto &kv : sorted)
				{
					if (kv.first == Material::MATERIAL_ID) continue;
					FpNode e = FpNode::Obj();
					e.Set("key", kv.first);
					e.Set("type", UniformTypeName(kv.second));
					e.Set("data", UniformData(kv.second));
					unis.array.push_back(std::move(e));
				}
			}
			n.Set("uniforms", unis);
			return n;
		}

		FpNode FingerprintMeshLod(unsigned int index, const Mesh::Ptr &m)
		{
			FpNode n = FpNode::Obj();
			n.Set("lod", index);
			n.Set("threshold", m->GetLodThreshold(index));
			if (index < m->GetLodBillboardFlags().size())
				n.Set("billboard", m->GetLodBillboardFlags()[index]);
			FpNode verts = FpNode::Obj();
			verts.Set("positions_count", static_cast<unsigned int>(m->Positions.Data.size() / 3));
			verts.Set("positions_hash", HashHex(HashFloats(m->Positions.Data)));
			if (!m->Normals.Data.empty())
			{
				verts.Set("normals_count", static_cast<unsigned int>(m->Normals.Data.size() / 3));
				verts.Set("normals_hash", HashHex(HashFloats(m->Normals.Data)));
			}
			if (!m->Tangents.Data.empty())
			{
				verts.Set("tangents_count", static_cast<unsigned int>(m->Tangents.Data.size() / 4));
				verts.Set("tangents_hash", HashHex(HashFloats(m->Tangents.Data)));
			}
			if (!m->UVs.Data.empty())
			{
				verts.Set("uvs_count", static_cast<unsigned int>(m->UVs.Data.size() / 2));
				verts.Set("uvs_hash", HashHex(HashFloats(m->UVs.Data)));
			}
			if (!m->Colors.Data.empty())
			{
				verts.Set("colors_count", static_cast<unsigned int>(m->Colors.Data.size() / 4));
				verts.Set("colors_hash", HashHex(HashFloats(m->Colors.Data)));
			}
			if (!m->IDs.Data.empty())
			{
				verts.Set("bone_ids_count", static_cast<unsigned int>(m->IDs.Data.size() / 4));
				verts.Set("bone_ids_hash", HashHex(HashUInts(m->IDs.Data)));
			}
			if (!m->Weights.Data.empty())
			{
				verts.Set("bone_weights_count", static_cast<unsigned int>(m->Weights.Data.size() / 4));
				verts.Set("bone_weights_hash", HashHex(HashFloats(m->Weights.Data)));
			}
			n.Set("vertices", verts);
			FpNode subs = FpNode::Arr();
			for (unsigned int i = 0; i < m->GetSubMeshCount(); ++i)
			{
				auto sub = m->GetSubMeshAt(i);
				FpNode e = FpNode::Obj();
				if (sub)
				{
					e.Set("indices_count", static_cast<unsigned int>(sub->Indices.Data.size()));
					e.Set("indices_hash", HashHex(HashUInts(sub->Indices.Data)));
				}
				subs.array.push_back(std::move(e));
			}
			n.Set("submeshes", subs);
			FpNode topIdx = FpNode::Obj();
			topIdx.Set("count", static_cast<unsigned int>(m->Indices.Data.size()));
			topIdx.Set("hash", HashHex(HashUInts(m->Indices.Data)));
			n.Set("indices", topIdx);
			if (m->GetJointCount() > 0)
			{
				FpNode joints = FpNode::Arr();
				for (unsigned int i = 0; i < m->GetJointCount(); ++i)
				{
					auto j = m->GetJointAt(i);
					if (!j) continue;
					FpNode e = FpNode::Obj();
					e.Set("name", j->GetName());
					e.Set("local_matrix", Mat(j->GetLocalMatrix()));
					e.Set("offset_matrix", Mat(j->GetOffsetMatrix()));
					e.Set("scene_node_uuid", j->GetSceneNodeUUID());
					auto parent = j->GetParent();
					e.Set("parent", parent ? parent->GetName() : std::string());
					joints.array.push_back(std::move(e));
				}
				n.Set("joints", joints);
				n.Set("root_joint", m->GetRootJoint() ? m->GetRootJoint()->GetName() : std::string());
			}
			if (auto bm = m->GetBillboardMaterial())
				n.Set("billboard_material", bm->GetName());
			// LOD chain in semantic order.
			const unsigned int lodCount = m->GetLodCount();
			if (lodCount > 1)
			{
				FpNode lods = FpNode::Arr();
				for (unsigned int i = 1; i < lodCount; ++i)
					lods.array.push_back(FingerprintMeshLod(i, m->GetLodMesh(i)));
				n.Set("lods", lods);
			}
			return n;
		}

		FpNode FingerprintMesh(const Mesh::Ptr &m)
		{
			FpNode n = FingerprintMeshLod(0, m);
			SetEntityId(n, *m);
			n.Set("key", AssetKey(*m));
			n.Set("cast_shadows", m->GetCastShadows());
			n.Set("skinned", m->IsSkinnedMesh());
			return n;
		}

		FpNode FingerprintGradient(const Gradient &g)
		{
			FpNode keys = FpNode::Arr();
			for (const auto &k : g.keys)
			{
				FpNode e = FpNode::Obj();
				e.Set("time", k.time);
				e.Set("color", Col(k.color));
				keys.array.push_back(std::move(e));
			}
			return keys;
		}

		FpNode FingerprintCurve(const AnimationCurve &c)
		{
			FpNode keys = FpNode::Arr();
			for (const auto &k : c.keys)
			{
				FpNode e = FpNode::Obj();
				e.Set("time", k.time);
				e.Set("value", k.value);
				keys.array.push_back(std::move(e));
			}
			return keys;
		}

		FpNode FingerprintParticleSystem(const ParticleSystem::Ptr &p)
		{
			FpNode n = FpNode::Obj();
			SetEntityId(n, *p);
			n.Set("key", AssetKey(*p));
			n.Set("maxParticles", p->GetMaxParticles());
			n.Set("lifetime", p->GetLifetime());
			n.Set("startSize", p->GetStartSize());
			const auto &em = p->GetEmission();
			FpNode emission = FpNode::Obj();
			emission.Set("rateOverTime", em.rateOverTime);
			FpNode bursts = FpNode::Arr();
			for (const auto &b : em.bursts)
			{
				FpNode e = FpNode::Obj();
				e.Set("time", b.time);
				e.Set("count", b.count);
				e.Set("probability", b.probability);
				bursts.array.push_back(std::move(e));
			}
			emission.Set("bursts", bursts);
			n.Set("emission", emission);
			const auto &sh = p->GetShape();
			FpNode shape = FpNode::Obj();
			shape.Set("type", static_cast<unsigned int>(sh.type));
			shape.Set("scale", Vec(sh.scale));
			shape.Set("radius", sh.radius);
			shape.Set("angle", sh.angle);
			n.Set("shape", shape);
			const auto &vel = p->GetVelocity();
			FpNode velocity = FpNode::Obj();
			velocity.Set("linear", Vec(vel.linear));
			velocity.Set("speed", vel.speed);
			velocity.Set("inheritFromParent", vel.inheritFromParent);
			n.Set("velocity", velocity);
			FpNode color = FpNode::Obj();
			color.Set("keys", FingerprintGradient(p->GetColorOverLifetime().color));
			n.Set("colorOverLifetime", color);
			FpNode size = FpNode::Obj();
			size.Set("keys", FingerprintCurve(p->GetSizeOverLifetime().size));
			n.Set("sizeOverLifetime", size);
			FpNode rotation = FpNode::Obj();
			rotation.Set("angularVelocity", p->GetRotationOverLifetime().angularVelocity);
			n.Set("rotationOverLifetime", rotation);
			const auto &rm = p->GetRenderer();
			FpNode renderer = FpNode::Obj();
			renderer.Set("materialName", rm.materialName);
			renderer.Set("blendMode", static_cast<unsigned int>(rm.blendMode));
			renderer.Set("receiveShadows", rm.receiveShadows);
			n.Set("renderer", renderer);
			return n;
		}

		FpNode FingerprintClip(const AnimationClip::Ptr &c)
		{
			FpNode n = FpNode::Obj();
			SetEntityId(n, *c);
			n.Set("key", AssetKey(*c));
			n.Set("ticksPerSecond", c->GetTicksPerSecond());
			n.Set("speed", c->GetSpeed());
			n.Set("loop", c->GetLoop());
			n.Set("duration", c->GetDuration());
			FpNode channels = FpNode::Arr();
			for (int i = 0; i < c->GetChannelCount(); ++i)
			{
				auto ch = c->GetChannelAt(i);
				if (!ch) continue;
				FpNode e = FpNode::Obj();
				e.Set("name", ch->name);
				auto kfs = [](const std::vector<KeyFrame> &frames)
				{
					FpNode arr = FpNode::Arr();
					for (const auto &kf : frames)
					{
						FpNode k = FpNode::Arr();
						k.array.push_back(FpNode::UInt(kf.tick));
						k.array.push_back(FpNode::Real(kf.x));
						k.array.push_back(FpNode::Real(kf.y));
						k.array.push_back(FpNode::Real(kf.z));
						arr.array.push_back(std::move(k));
					}
					return arr;
				};
				e.Set("positions", kfs(ch->positions));
				e.Set("rotations", kfs(ch->rotations));
				e.Set("scalings", kfs(ch->scalings));
				channels.array.push_back(std::move(e));
			}
			n.Set("channels", channels);
			return n;
		}

		template<class T>
		std::vector<std::shared_ptr<T>> SortedAssets(const std::shared_ptr<EntityManager> &em)
		{
			std::vector<std::shared_ptr<T>> out;
			if (!em) return out;
			em->template ForEach<T>([&out](const std::shared_ptr<T> &p) -> bool
			{
				out.push_back(p);
				return true;
			});
			std::sort(out.begin(), out.end(), [](const std::shared_ptr<T> &a, const std::shared_ptr<T> &b)
			{
				return AssetKey(*a) < AssetKey(*b);
			});
			return out;
		}
	}

	// ------------------------------------------------------------------
	// Components
	// ------------------------------------------------------------------

	namespace
	{
		// Reverse of SceneNode::ComponentRegistry: type_index -> persisted
		// type name. Built lazily by invoking each registered factory once.
		const std::map<std::type_index, std::string> &RegistryNames()
		{
			static const std::map<std::type_index, std::string> names = []()
			{
				std::map<std::type_index, std::string> out;
				for (const auto &kv : SceneNode::ComponentRegistry)
				{
					auto c = kv.second();
					if (c) out[c->GetTypeIndex()] = kv.first;
				}
				return out;
			}();
			return names;
		}

		std::string ComponentTypeName(const Component &c)
		{
			std::string name = SceneNode::GetComponentTypeName(c.GetTypeIndex());
			if (!name.empty()) return name;
			return c.GetTypeIndex().name();
		}

		FpNode FingerprintLight(const Light::Ptr &c)
		{
			FpNode n = FpNode::Obj();
			n.Set("light_type", EnumUtil::LightTypeToString(c->GetType()));
			n.Set("color", Col(c->GetColor()));
			n.Set("intensity", c->GetIntensity());
			n.Set("inner_angle", c->GetInnerAngle());
			n.Set("outter_angle", c->GetOutterAngle());
			n.Set("falloff", c->GetFalloff());
			n.Set("radius", c->GetRadius());
			n.Set("cast_shadows", c->GetCastShadows());
			return n;
		}

		FpNode FingerprintCamera(const Camera::Ptr &c)
		{
			FpNode n = FpNode::Obj();
			n.Set("perspective", c->IsPerspective());
			n.Set("left", c->GetLeft());
			n.Set("right", c->GetRight());
			n.Set("bottom", c->GetBottom());
			n.Set("top", c->GetTop());
			n.Set("near", c->GetNear());
			n.Set("far", c->GetFar());
			n.Set("shadow_far", c->GetShadowFar());
			n.Set("csm_split_blend", c->GetCsmSplitBlend());
			const BoxBounds bounds = c->GetShadowBounds(false);
			n.Set("shadow_min", Vec(bounds.GetMin()));
			n.Set("shadow_max", Vec(bounds.GetMax()));
			return n;
		}

		FpNode FingerprintMeshRender(const MeshRender::Ptr &c)
		{
			FpNode n = FpNode::Obj();
			if (auto mesh = c->GetMesh())
				n.Set("mesh", mesh->GetName());
			n.Set("cast_shadows", c->GetCastShadows());
			FpNode mats = FpNode::Arr();
			for (unsigned int i = 0; i < c->GetMaterialCount(); ++i)
			{
				auto m = c->GetMaterial(i);
				mats.array.push_back(FpNode::Str(m ? m->GetName() : std::string()));
			}
			n.Set("materials", mats);
			return n;
		}

		FpNode FingerprintInstancedMeshRender(const InstancedMeshRender::Ptr &c)
		{
			FpNode n = FpNode::Obj();
			if (auto mesh = c->GetMesh())
				n.Set("mesh", mesh->GetName());
			n.Set("cast_shadows", c->GetCastShadows());
			n.Set("hierarchical", c->GetHierarchical());
			n.Set("cull_distance", c->GetCullDistance());
			FpNode mats = FpNode::Arr();
			for (unsigned int i = 0; i < c->GetMaterialCount(); ++i)
			{
				auto m = c->GetMaterial(i);
				mats.array.push_back(FpNode::Str(m ? m->GetName() : std::string()));
			}
			n.Set("materials", mats);
			FpNode inst = FpNode::Arr();
			for (unsigned int i = 0; i < c->GetInstanceCount(); ++i)
			{
				const auto &in = c->GetInstance(i);
				FpNode e = FpNode::Obj();
				e.Set("position", Vec(in.Position));
				e.Set("rotation", Quat(in.Rotation));
				e.Set("scale", Vec(in.Scale));
				inst.array.push_back(std::move(e));
			}
			n.Set("instances", inst);
			return n;
		}

		FpNode FingerprintAnimator(const Animator::Ptr &c)
		{
			FpNode n = FpNode::Obj();
			n.Set("animate_physics", c->GetAnimatePhysics());
			n.Set("default_wrap", EnumUtil::AnimWrapModeToString(c->GetDefaultWrapMode()));
			FpNode clips = FpNode::Arr();
			for (unsigned int i = 0; i < c->GetStateCount(); ++i)
			{
				auto state = c->GetStateAt(i);
				if (state && !state->GetName().empty())
					clips.array.push_back(FpNode::Str(state->GetName()));
			}
			n.Set("clips", clips);
			return n;
		}

		FpNode FingerprintParticleRenderer(const ParticleRenderer::Ptr &c)
		{
			FpNode n = FpNode::Obj();
			n.Set("name", c->GetName());
			n.Set("system", c->GetSystemName());
			auto mat = c->GetMaterial();
			n.Set("material", mat ? mat->GetName() : std::string());
			n.Set("blendMode", static_cast<unsigned int>(c->GetBlendMode()));
			return n;
		}

		FpNode FingerprintBodySetup(const BodySetup::Ptr &c)
		{
			FpNode n = FpNode::Obj();
			const char *shape = "mesh";
			switch (c->GetShapeType())
			{
			case BodySetup::ShapeType::Box: shape = "box"; break;
			case BodySetup::ShapeType::Sphere: shape = "sphere"; break;
			case BodySetup::ShapeType::HeightField: shape = "heightfield"; break;
			case BodySetup::ShapeType::Mesh: default: break;
			}
			n.Set("shape_type", shape);
			n.Set("motion_type", c->GetMotionType() == BodySetup::MotionType::Dynamic ? "dynamic" : "static");
			n.Set("collision_mesh", c->GetCollisionMeshName());
			n.Set("half_extents", Vec(c->GetHalfExtents()));
			n.Set("radius", c->GetRadius());
			n.Set("mass", c->GetMass());
			n.Set("friction", c->GetFriction());
			n.Set("restitution", c->GetRestitution());
			return n;
		}

		FpNode FingerprintPlayerFields(const std::shared_ptr<PlayerController> &c, FpNode n)
		{
			n.Set("camera_node", c->GetCameraNodeName());
			n.Set("enabled", c->IsEnabled());
			return n;
		}

		FpNode FingerprintFreeFlyController(const std::shared_ptr<FreeFlyController> &c)
		{
			FpNode n = FingerprintPlayerFields(c, FpNode::Obj());
			n.Set("move_speed", c->GetMoveSpeed());
			n.Set("mouse_sensitivity", c->GetMouseSensitivity());
			return n;
		}

		FpNode FingerprintCharacterController(const std::shared_ptr<CharacterController> &c)
		{
			FpNode n = FingerprintPlayerFields(c, FpNode::Obj());
			n.Set("height", c->GetHeight());
			n.Set("radius", c->GetRadius());
			n.Set("walk_speed", c->GetWalkSpeed());
			n.Set("run_speed", c->GetRunSpeed());
			n.Set("jump_speed", c->GetJumpSpeed());
			n.Set("camera_distance", c->GetCameraDistance());
			n.Set("camera_height", c->GetCameraHeight());
			n.Set("first_person", c->GetFirstPerson());
			n.Set("mouse_sensitivity", c->GetMouseSensitivity());
			n.Set("model_yaw_offset", c->GetModelYawOffset());
			n.Set("idle_clip", c->GetIdleClip());
			n.Set("walk_clip", c->GetWalkClip());
			n.Set("run_clip", c->GetRunClip());
			n.Set("jump_clip", c->GetJumpClip());
			return n;
		}

		FpNode FingerprintSkyAtmosphere(const SkyAtmosphere::Ptr &c)
		{
			FpNode n = FpNode::Obj();
			n.Set("enabled", c->GetEnabled());
			n.Set("bottom_radius", c->GetBottomRadiusKm());
			n.Set("top_radius", c->GetTopRadiusKm());
			n.Set("rayleigh_scat", Vec(c->GetRayleighScattering()));
			n.Set("rayleigh_exp_scale", c->GetRayleighExpScale());
			n.Set("mie_scat", c->GetMieScattering());
			n.Set("mie_ext", c->GetMieExtinction());
			n.Set("mie_exp_scale", c->GetMieExpScale());
			n.Set("mie_g", c->GetMieG());
			n.Set("ozone_ext", Vec(c->GetOzoneExtinction()));
			n.Set("ozone_center", c->GetOzoneCenterKm());
			n.Set("ozone_width", c->GetOzoneWidthKm());
			n.Set("ground_albedo", Vec(c->GetGroundAlbedo()));
			n.Set("sun_angular_radius", c->GetSunAngularRadius());
			n.Set("sun_disc_intensity", c->GetSunDiscIntensity());
			n.Set("sun_intensity", c->GetSunIntensityParam());
			n.Set("ap_range_km", c->GetApRangeKm());
			n.Set("moon_enabled", c->GetMoonEnabled());
			n.Set("moon_angular_radius", c->GetMoonAngularRadius());
			n.Set("moon_intensity", c->GetMoonIntensity());
			n.Set("moon_texture", c->GetMoonTexturePath());
			n.Set("clouds_enabled", c->GetCloudsEnabled());
			n.Set("cloud_coverage", c->GetCloudCoverage());
			n.Set("cloud_alt_km", c->GetCloudAltitudeKm());
			n.Set("cloud_thick_km", c->GetCloudThicknessKm());
			n.Set("cloud_scale", c->GetCloudScale());
			n.Set("cloud_density", c->GetCloudDensity());
			n.Set("cloud_wind_speed", c->GetCloudWindSpeed());
			n.Set("cloud_fade_km", c->GetCloudFadeKm());
			n.Set("cloud_noise_texture", c->GetCloudNoisePath());
			n.Set("cloud_type_bias", c->GetCloudTypeBias());
			n.Set("cloud_detail_scale", c->GetCloudDetailScale());
			n.Set("cloud_erosion", c->GetCloudErosion());
			n.Set("cloud_powder", c->GetCloudPowder());
			n.Set("cloud_hg_g", c->GetCloudHgG());
			n.Set("cloud_hg_g_fwd", c->GetCloudHgGFwd());
			n.Set("cloud_hg_blend", c->GetCloudHgBlend());
			n.Set("cloud_ambient_scale", c->GetCloudAmbientScale());
			n.Set("cloud_quality", c->GetCloudQuality());
			n.Set("cloud_debug_mode", c->GetCloudDebugMode());
			n.Set("cloud_weather_bias", c->GetCloudWeatherBias());
			n.Set("cloud_weather_type_contrast", c->GetCloudWeatherTypeContrast());
			n.Set("cirrus_enabled", c->GetCirrusEnabled());
			n.Set("cirrus_coverage", c->GetCirrusCoverage());
			n.Set("cirrus_alt_km", c->GetCirrusAltKm());
			n.Set("cirrus_scale", c->GetCirrusScale());
			n.Set("cirrus_density", c->GetCirrusDensity());
			n.Set("time_hours", c->GetTimeHours());
			n.Set("day_length_minutes", c->GetDayLengthMinutes());
			n.Set("auto_advance", c->GetAutoAdvance());
			n.Set("sun_from_tod", c->GetSunFromTod());
			n.Set("sun_light_name", c->GetSunLightName());
			return n;
		}

		FpNode FingerprintTerrain(const Terrain::Ptr &c)
		{
			FpNode n = FpNode::Obj();
			n.Set("heightmap", c->GetHeightmapName());
			n.Set("splatmap", c->GetSplatmapPath());
			n.Set("chunk_count", c->GetChunkCount());
			n.Set("lod_count", c->GetLodCount());
			FpNode layers = FpNode::Arr();
			for (int i = 0; i < 4; ++i)
			{
				const Terrain::Layer &layer = c->GetLayer(i);
				FpNode e = FpNode::Obj();
				e.Set("name", layer.Name);
				e.Set("texture", layer.TexturePath);
				e.Set("tiling", layer.TilingCm);
				layers.array.push_back(std::move(e));
			}
			n.Set("layers", layers);
			return n;
		}

		FpNode FingerprintOceanComponent(const OceanComponent::Ptr &c)
		{
			FpNode n = FpNode::Obj();
			n.Set("name", c->GetName());
			n.Set("mode", static_cast<unsigned int>(c->GetMode()));
			n.Set("waveSource", static_cast<unsigned int>(c->GetWaveSource()));
			n.Set("waveAsset", c->GetWaveAssetPath());
			n.Set("seed", c->GetSeed());
			n.Set("windSpeed", c->GetWindSpeed());
			n.Set("windDirectionDeg", c->GetWindDirectionDeg());
			n.Set("fetchCm", c->GetFetchCm());
			n.Set("choppiness", c->GetChoppiness());
			n.Set("swellResolution", c->GetSwellResolution());
			n.Set("rippleResolution", c->GetRippleResolution());
			n.Set("swellTileCm", c->GetSwellTileCm());
			n.Set("rippleTileCm", c->GetRippleTileCm());
			n.Set("frameCount", c->GetFrameCount());
			n.Set("loopSeconds", c->GetLoopSeconds());
			n.Set("waterLevel", c->GetWaterLevel());
			n.Set("finiteSizeCm", c->GetFiniteSizeCm());
			n.Set("finiteResolution", c->GetFiniteResolution());
			n.Set("ringCellSizeCm", c->GetRingCellSizeCm());
			n.Set("ringCells", c->GetRingCells());
			n.Set("ringCount", c->GetRingCount());
			n.Set("skirtRadiusCm", c->GetSkirtRadiusCm());
			n.Set("absorbColor", Col(c->GetAbsorbColor()));
			n.Set("scatterColor", Col(c->GetScatterColor()));
			n.Set("roughness", c->GetRoughness());
			n.Set("normalStrength", c->GetNormalStrength());
			n.Set("foamAmount", c->GetFoamAmount());
			n.Set("shoreFoamDepthCm", c->GetShoreFoamDepthCm());
			n.Set("ssr", c->GetSsrEnabled());
			n.Set("debugView", c->GetDebugView());
			return n;
		}

		FpNode FingerprintBuoyancyComponent(const BuoyancyComponent::Ptr &c)
		{
			FpNode n = FpNode::Obj();
			n.Set("name", c->GetName());
			n.Set("waterDensity", c->GetWaterDensity());
			n.Set("linearDrag", c->GetLinearDrag());
			n.Set("angularDrag", c->GetAngularDrag());
			n.Set("rightingStrength", c->GetRightingStrength());
			n.Set("oceanNode", c->GetOceanNodeName());
			FpNode points = FpNode::Arr();
			for (unsigned int i = 0; i < c->GetFloatPointCount(); ++i)
			{
				const auto &fp = c->GetFloatPoint(i);
				FpNode e = FpNode::Obj();
				e.Set("offset", Vec(fp.Offset));
				e.Set("radius", fp.Radius);
				points.array.push_back(std::move(e));
			}
			n.Set("floatPoints", points);
			return n;
		}

		FpNode FingerprintComponentFields(const Component::Ptr &comp, const std::type_index &ti)
		{
			if (ti == typeid(MeshRender))
				return FingerprintMeshRender(std::static_pointer_cast<MeshRender>(comp));
			if (ti == typeid(InstancedMeshRender))
				return FingerprintInstancedMeshRender(std::static_pointer_cast<InstancedMeshRender>(comp));
			if (ti == typeid(Light))
				return FingerprintLight(std::static_pointer_cast<Light>(comp));
			if (ti == typeid(Camera))
				return FingerprintCamera(std::static_pointer_cast<Camera>(comp));
			if (ti == typeid(Transform))
				return FpNode::Obj(); // Transform persists nothing.
			if (ti == typeid(Animator))
				return FingerprintAnimator(std::static_pointer_cast<Animator>(comp));
			if (ti == typeid(ParticleRenderer))
				return FingerprintParticleRenderer(std::static_pointer_cast<ParticleRenderer>(comp));
			if (ti == typeid(BodySetup))
				return FingerprintBodySetup(std::static_pointer_cast<BodySetup>(comp));
			if (ti == typeid(FreeFlyController))
				return FingerprintFreeFlyController(std::static_pointer_cast<FreeFlyController>(comp));
			if (ti == typeid(CharacterController))
				return FingerprintCharacterController(std::static_pointer_cast<CharacterController>(comp));
			if (ti == typeid(SkyAtmosphere))
				return FingerprintSkyAtmosphere(std::static_pointer_cast<SkyAtmosphere>(comp));
			if (ti == typeid(Terrain))
				return FingerprintTerrain(std::static_pointer_cast<Terrain>(comp));
			if (ti == typeid(OceanComponent))
				return FingerprintOceanComponent(std::static_pointer_cast<OceanComponent>(comp));
			if (ti == typeid(BuoyancyComponent))
				return FingerprintBuoyancyComponent(std::static_pointer_cast<BuoyancyComponent>(comp));
			return FpNode::Obj();
		}

		FpNode FingerprintComponent(const Component::Ptr &comp)
		{
			FpNode n = FpNode::Obj();
			n.Set("type", ComponentTypeName(*comp));
			const auto &names = RegistryNames();
			const std::type_index ti = comp->GetTypeIndex();
			if (names.find(ti) == names.end())
			{
				// Attached outside the registry; nothing persists.
				n.Set("unregistered", true);
				return n;
			}
			n.Set("fields", FingerprintComponentFields(comp, ti));
			return n;
		}

		FpNode FingerprintNode(const SceneNode::Ptr &node)
		{
			FpNode n = FpNode::Obj();
			n.Set("name", node->GetName());
			n.Set("uuid", node->GetUUID());
			n.Set("pos", Vec(node->GetLocalPosition()));
			n.Set("scl", Vec(node->GetLocalScale()));
			n.Set("rot", Quat(node->GetLocalRoattion()));
			// Components in canonical type-name order (the underlying map is
			// unordered); order inside a component's fields is semantic.
			FpNode comps = FpNode::Arr();
			{
				std::map<std::string, std::type_index> byName;
				for (const auto &kv : RegistryNames())
					byName.emplace(kv.second, kv.first);
				for (const auto &kv : byName)
				{
					auto comp = node->GetComponent(kv.second);
					if (comp)
						comps.array.push_back(FingerprintComponent(comp));
				}
			}
			n.Set("components", comps);
			FpNode children = FpNode::Arr();
			for (unsigned int i = 0; i < node->GetChildCount(); ++i)
			{
				auto child = node->GetChildAt(i);
				if (!child || child->IsEditorOnly())
					continue; // editor tooling state, never persisted
				children.array.push_back(FingerprintNode(child));
			}
			n.Set("children", children);
			return n;
		}

		FpNode FingerprintRenderSettings(const std::shared_ptr<RenderSettings> &rs)
		{
			FpNode n = FpNode::Obj();
			if (!rs) return n;
			n.Set("pipeline", rs->GetPipelinePath());
			n.Set("hdr", rs->IsHDR());
			n.Set("clustered_lighting", rs->IsClusteredLighting());
			n.Set("cascaded_shadow_map", rs->IsCascadedShadowMap());
			n.Set("vegetation_prez", rs->IsVegetationPreZ());
			n.Set("csm_map_size", rs->GetCsmMapSize());
			n.Set("shadow_far", rs->GetShadowFar());
			n.Set("csm_split_blend", rs->GetCsmSplitBlend());
			n.Set("wind_params", Vec(rs->GetWindParams()));
			FpNode chain = FpNode::Arr();
			for (const auto &e : rs->GetChain())
			{
				FpNode entry = FpNode::Obj();
				entry.Set("effect", e.effectName);
				entry.Set("enabled", e.enabled);
				if (!e.uniformOverrides.empty())
				{
					std::map<std::string, UniformBase::Ptr> sorted(e.uniformOverrides.begin(), e.uniformOverrides.end());
					FpNode unis = FpNode::Arr();
					for (const auto &kv : sorted)
					{
						FpNode u = FpNode::Obj();
						u.Set("name", kv.first);
						u.Set("type", UniformTypeName(kv.second));
						u.Set("value", UniformData(kv.second));
						unis.array.push_back(std::move(u));
					}
					entry.Set("uniforms", unis);
				}
				chain.array.push_back(std::move(entry));
			}
			n.Set("chain", chain);
			return n;
		}
	}

	FpNode SceneFingerprint::Build(const std::shared_ptr<Scene> &scene)
	{
		FpNode root = FpNode::Obj();
		if (!scene)
		{
			root.Set("scene", FpNode::Nil());
			return root;
		}
		FpNode sceneNode = FpNode::Obj();
		sceneNode.Set("name", scene->GetName());
		sceneNode.Set("uuid", scene->GetUUID());
		root.Set("scene", sceneNode);

		FpNode physics = FpNode::Obj();
		physics.Set("has_settings", scene->HasPhysicsSettings());
		physics.Set("gravity", Vec(scene->GetPhysicsGravity()));
		root.Set("physics", physics);

		root.Set("renderSettings", FingerprintRenderSettings(scene->GetRenderSettings()));
		root.Set("nodes", FingerprintNode(scene->GetRootNode()));

		FpNode resources = FpNode::Obj();
		auto em = scene->GetEntityManager();
		{
			FpNode arr = FpNode::Arr();
			for (const auto &t : SortedAssets<Texture>(em))
				arr.array.push_back(FingerprintTexture(t));
			resources.Set("textures", arr);
		}
		{
			FpNode arr = FpNode::Arr();
			for (const auto &m : SortedAssets<Material>(em))
				arr.array.push_back(FingerprintMaterial(m));
			resources.Set("materials", arr);
		}
		{
			FpNode arr = FpNode::Arr();
			for (const auto &m : SortedAssets<Mesh>(em))
				arr.array.push_back(FingerprintMesh(m));
			resources.Set("meshes", arr);
		}
		{
			FpNode arr = FpNode::Arr();
			for (const auto &c : SortedAssets<AnimationClip>(em))
				arr.array.push_back(FingerprintClip(c));
			resources.Set("animationClips", arr);
		}
		{
			FpNode arr = FpNode::Arr();
			for (const auto &p : SortedAssets<ParticleSystem>(em))
				arr.array.push_back(FingerprintParticleSystem(p));
			resources.Set("particleSystems", arr);
		}
		{
			FpNode arr = FpNode::Arr();
			for (const auto &h : SortedAssets<Heightmap>(em))
			{
				FpNode n = FpNode::Obj();
				SetEntityId(n, *h);
				n.Set("key", AssetKey(*h));
				n.Set("file_path", h->GetFilePath());
				n.Set("resolution", h->GetResolution());
				n.Set("worldSizeX", h->GetWorldSizeX());
				n.Set("worldSizeZ", h->GetWorldSizeZ());
				n.Set("heightScale", h->GetHeightScale());
				arr.array.push_back(std::move(n));
			}
			resources.Set("heightmaps", arr);
		}
		{
			FpNode arr = FpNode::Arr();
			for (const auto &w : SortedAssets<OceanWaves>(em))
			{
				FpNode n = FpNode::Obj();
				n.Set("name", w->GetName());
				n.Set("key", AssetKey(*w));
				n.Set("file_path", w->GetFilePath());
				arr.array.push_back(std::move(n));
			}
			resources.Set("oceanWaves", arr);
		}
		root.Set("resources", resources);
		return root;
	}

	SceneFingerprintCounts SceneFingerprint::Counts(const std::shared_ptr<Scene> &scene)
	{
		SceneFingerprintCounts out;
		if (!scene) return out;
		std::function<void(const SceneNode::Ptr &)> walk =
			[&](const SceneNode::Ptr &node)
		{
			if (!node) return;
			++out.nodes;
			if (node->GetComponent<ParticleRenderer>()) ++out.particle_renderers;
			if (auto render = node->GetComponent<MeshRender>())
			{
				if (auto mesh = render->GetMesh())
				{
					for (unsigned int s = 0; s < mesh->GetSubMeshCount(); ++s)
					{
						++out.submeshes;
						auto sub = mesh->GetSubMeshAt(s);
						if (sub) out.triangles += static_cast<long long>(sub->Indices.Data.size() / 3);
					}
					out.vertices += static_cast<long long>(mesh->Positions.Data.size() / 3);
				}
			}
			for (unsigned int i = 0; i < node->GetChildCount(); ++i)
				walk(node->GetChildAt(i));
		};
		walk(scene->GetRootNode());
		auto em = scene->GetEntityManager();
		em->ForEach<Mesh>([&](const Mesh::Ptr &m) -> bool
		{
			if (m->IsSkinnedMesh()) ++out.meshes_skinned;
			else ++out.meshes_static;
			out.joints += m->GetJointCount();
			return true;
		});
		em->ForEach<Material>([&](const Material::Ptr &) -> bool { ++out.materials; return true; });
		em->ForEach<Texture>([&](const Texture::Ptr &) -> bool { ++out.textures; return true; });
		em->ForEach<AnimationClip>([&](const AnimationClip::Ptr &) -> bool { ++out.animations; return true; });
		em->ForEach<ParticleSystem>([&](const ParticleSystem::Ptr &) -> bool { ++out.particle_systems; return true; });
		out.meshes_total = out.meshes_static + out.meshes_skinned;
		return out;
	}
}
