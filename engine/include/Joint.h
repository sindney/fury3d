#ifndef _FURY_JOINT_H_
#define _FURY_JOINT_H_

#include "Entity.h"
#include "Matrix4.h"
#include "Quaternion.h"

namespace fury
{
	class Mesh;

	class FURY_API Joint final : public Entity
	{
	public:

		friend class Shader;

		typedef std::shared_ptr<Joint> Ptr;

		static Ptr Create(const std::string &name, const std::shared_ptr<Mesh> &mesh);

		static Ptr FindFromRoot(const std::string &name, const Ptr &root);

	protected:

		std::weak_ptr<Mesh> m_Mesh;

		Joint::Ptr m_FirstChild;

		Joint::Ptr m_Sibling;

		std::weak_ptr<Joint> m_Parent;

		Matrix4 m_LocalMatrix;

		Matrix4 m_CombinedMatrix;

		Matrix4 m_OffsetMatrix;

		Matrix4 m_FinalMatrix;

		std::pair<Vector4, Vector4> m_Position;

		std::pair<Quaternion, Quaternion> m_Rotation;

		std::pair<Vector4, Vector4> m_Scaling;

	public:

		Joint(const std::string &name, const std::shared_ptr<Mesh> &mesh);

		~Joint();

		void Update(const Matrix4 &matrix);

		// update local matrix by interpolated TRS value pairs.
		void Update(float dt);

		void SetRotation(Quaternion rot, bool old = false);

		void SetPosition(Vector4 pos, bool old = false);

		void SetScaling(Vector4 scl, bool old = false);

		Quaternion GetRotation(bool old = false);

		Vector4 GetPosition(bool old = false);

		Vector4 GetScaling(bool old = false);

		void SetLocalMatrix(const Matrix4 &matrix);

		Matrix4 GetLocalMatrix() const;

		void SetCombinedMatrix(const Matrix4 &matrix);

		Matrix4 GetCombinedMatrix() const;

		void SetOffsetMatrix(const Matrix4 &matrix);

		Matrix4 GetOffsetMatrix() const;

		Matrix4 GetFinalMatrix();

		Joint::Ptr GetFirstChild() const;

		void SetFirstChild(const Joint::Ptr &joint);

		Joint::Ptr GetSibling() const;

		void SetSibling(const Joint::Ptr &joint);

		Joint::Ptr GetParent() const;

		void SetParent(const Joint::Ptr &joint);

		std::shared_ptr<Mesh> GetMesh() const;
	};
}

#endif // _FURY_JOINT_H_