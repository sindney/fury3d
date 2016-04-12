#include <stack>

#include "Mesh.h"
#include "Joint.h"

namespace fury
{
	std::shared_ptr<Joint> Joint::Create(const std::string &name, const std::shared_ptr<Mesh> &mesh)
	{
		return std::make_shared<Joint>(name, mesh);
	}

	std::shared_ptr<Joint> Joint::FindFromRoot(const std::string &name, const std::shared_ptr<Joint> &root)
	{
		size_t nameHash = std::hash<std::string>()(name);
		if (root->m_HashCode == nameHash)
			return root;

		Joint::Ptr joint0 = root, joint1 = nullptr;
		std::stack<Joint::Ptr> jointStack;
		jointStack.push(joint0);

		while (!jointStack.empty())
		{
			joint0 = jointStack.top();
			jointStack.pop();
			joint1 = joint0->m_FirstChild;
			while (joint1 != nullptr)
			{
				if (joint1->m_HashCode == nameHash)
					return joint1;
				if (joint1->m_FirstChild != nullptr)
					jointStack.push(joint1);
				joint1 = joint1->m_Sibling;
			}
		}

		return nullptr;
	}
	
	Joint::Joint(const std::string &name, const std::shared_ptr<Mesh> &mesh) : 
		Entity(name), m_Mesh(mesh)
	{
		m_TypeIndex = typeid(Joint);
	}

	// TODO: test
	Joint::~Joint()
	{
		m_Mesh = nullptr;
		m_FirstChild = nullptr;
		m_Sibling = nullptr;
		m_Parent = nullptr;
	}

	void Joint::Update(const Matrix4 &matrix)
	{
		m_CombinedMatrix = matrix * m_LocalMatrix;
		m_FinalMatrix = m_CombinedMatrix * m_OffsetMatrix;
		if (m_Sibling != nullptr)
			m_Sibling->Update(matrix);
		if (m_FirstChild != nullptr)
			m_FirstChild->Update(m_CombinedMatrix);
	}

	void Joint::Update(float dt)
	{
		m_LocalMatrix.Translate(m_Position.first + (m_Position.second - m_Position.first) * dt); 
		m_LocalMatrix.AppendRotation(m_Rotation.first.Slerp(m_Rotation.second, dt));
		m_LocalMatrix.AppendScale(m_Scaling.first + (m_Scaling.second - m_Scaling.first) * dt);
	}

	void Joint::SetRotation(Quaternion rot, bool old)
	{
		(old ? m_Rotation.first : m_Rotation.second) = rot; 
	}

	void Joint::SetPosition(Vector4 pos, bool old)
	{
		(old ? m_Position.first : m_Position.second) = pos;
	}

	void Joint::SetScaling(Vector4 scl, bool old)
	{
		(old ? m_Scaling.first : m_Scaling.second) = scl;
	}

	Quaternion Joint::GetRotation(bool old)
	{
		return old ? m_Rotation.first : m_Rotation.second;
	}

	Vector4 Joint::GetPosition(bool old)
	{
		return old ? m_Position.first : m_Position.second;
	}

	Vector4 Joint::GetScaling(bool old)
	{
		return old ? m_Scaling.first : m_Scaling.second;
	}

	void Joint::SetLocalMatrix(const Matrix4 &matrix)
	{
		m_LocalMatrix = matrix;
	}

	Matrix4 Joint::GetLocalMatrix() const
	{
		return m_LocalMatrix;
	}

	void Joint::SetCombinedMatrix(const Matrix4 &matrix)
	{
		m_CombinedMatrix = matrix;
	}

	Matrix4 Joint::GetCombinedMatrix() const
	{
		return m_CombinedMatrix;
	}

	void Joint::SetOffsetMatrix(const Matrix4 &matrix)
	{
		m_OffsetMatrix = matrix;
	}

	Matrix4 Joint::GetOffsetMatrix() const
	{
		return m_OffsetMatrix;
	}

	Matrix4 Joint::GetFinalMatrix()
	{
		return m_FinalMatrix;
	}

	Joint::Ptr Joint::GetFirstChild() const
	{
		return m_FirstChild;
	}

	void Joint::SetFirstChild(const Joint::Ptr &joint)
	{
		m_FirstChild = joint;
	}

	Joint::Ptr Joint::GetSibling() const
	{
		return m_Sibling;
	}

	void Joint::SetSibling(const Joint::Ptr &joint)
	{
		m_Sibling = joint;
	}

	Joint::Ptr Joint::GetParent() const
	{
		return m_Parent;
	}

	void Joint::SetParent(const Joint::Ptr &joint)
	{
		m_Parent = joint;
	}

	std::shared_ptr<Mesh> Joint::GetMesh() const
	{
		return m_Mesh;
	}
}