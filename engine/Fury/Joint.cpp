#include <stack>

#include "Fury/Log.h"
#include "Fury/Mesh.h"
#include "Fury/Joint.h"
#include "Fury/SceneNode.h"

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

	Joint::~Joint()
	{
		m_Mesh.reset();
		m_Parent.reset();
		m_FirstChild = nullptr;
		m_Sibling = nullptr;
		//FURYD << "Joint " << m_Name << " destoried!";
	}

	void Joint::SetLocalMatrix(const Matrix4 &matrix)
	{
		m_LocalMatrix = matrix;
	}

	Matrix4 Joint::GetLocalMatrix() const
	{
		return m_LocalMatrix;
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
		// glTF-standard skinning: Final = JᵢW * ibm, where JᵢW is the
		// joint's scene-graph world matrix (computed by Recompose,
		// including ancestors like the skeleton root's parent) and ibm
		// is the glb inverseBindMatrix (stored as m_OffsetMatrix). The
		// skin shader pairs this with an identity model matrix so the
		// result lands in world space directly.
		auto node = m_SceneNode.lock();
		if (node)
			return node->GetWorldMatrix() * m_OffsetMatrix;
		return m_OffsetMatrix;
	}

	std::shared_ptr<SceneNode> Joint::GetSceneNode() const
	{
		return m_SceneNode.lock();
	}

	void Joint::SetSceneNode(const std::shared_ptr<SceneNode> &node)
	{
		m_SceneNode = node;
		m_SceneNodeUUID = node ? node->GetUUID() : std::string();
	}

	const std::string &Joint::GetSceneNodeUUID() const
	{
		return m_SceneNodeUUID;
	}

	void Joint::SetSceneNodeUUID(const std::string &uuid)
	{
		m_SceneNodeUUID = uuid;
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
		return m_Parent.lock();
	}

	void Joint::SetParent(const Joint::Ptr &joint)
	{
		m_Parent = joint;
	}

	std::shared_ptr<Mesh> Joint::GetMesh() const
	{
		return m_Mesh.lock();
	}
}