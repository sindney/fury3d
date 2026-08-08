#ifndef _FURY_JOINT_H_
#define _FURY_JOINT_H_

#include "Fury/Entity.h"
#include "Fury/Matrix4.h"

namespace fury {
class Mesh;
class SceneNode;

class FURY_API Joint final : public Entity {
public:
	friend class Shader;

	typedef std::shared_ptr<Joint> Ptr;

	static Ptr Create(const std::string& name, const std::shared_ptr<Mesh>& mesh);

	static Ptr FindFromRoot(const std::string& name, const Ptr& root);

protected:
	std::weak_ptr<Mesh> m_Mesh;

	// glTF-standard skinning: the joint's world matrix comes from the
	// scene graph (this SceneNode -- the glTF joint node -- includes
	// ancestors like the skeleton root's parent). GetFinalMatrix()
	// returns sceneNodeWorld * m_OffsetMatrix.
	std::weak_ptr<SceneNode> m_SceneNode;

	// UUID of the linked SceneNode, persisted in Mesh::Save so the
	// linkage can be re-established unambiguously after Scene::Load
	// (names can collide across instances; UUIDs can't).
	std::string m_SceneNodeUUID;

	Joint::Ptr m_FirstChild;

	Joint::Ptr m_Sibling;

	std::weak_ptr<Joint> m_Parent;

	Matrix4 m_LocalMatrix;

	Matrix4 m_OffsetMatrix;

public:
	Joint(const std::string& name, const std::shared_ptr<Mesh>& mesh);

	~Joint();

	void SetLocalMatrix(const Matrix4& matrix);

	Matrix4 GetLocalMatrix() const;

	void SetOffsetMatrix(const Matrix4& matrix);

	Matrix4 GetOffsetMatrix() const;

	Matrix4 GetFinalMatrix();

	// glTF skin: the SceneNode this joint mirrors (its world matrix is
	// JᵢW in the Final = JᵢW * ibm formula).
	std::shared_ptr<SceneNode> GetSceneNode() const;
	void SetSceneNode(const std::shared_ptr<SceneNode>& node);

	// UUID of the linked SceneNode (empty when unlinked). SetSceneNode
	// keeps this in sync; SetSceneNodeUUID is the deserialization hook.
	const std::string& GetSceneNodeUUID() const;
	void SetSceneNodeUUID(const std::string& uuid);

	Joint::Ptr GetFirstChild() const;

	void SetFirstChild(const Joint::Ptr& joint);

	Joint::Ptr GetSibling() const;

	void SetSibling(const Joint::Ptr& joint);

	Joint::Ptr GetParent() const;

	void SetParent(const Joint::Ptr& joint);

	std::shared_ptr<Mesh> GetMesh() const;
};
} // namespace fury

#endif // _FURY_JOINT_H_