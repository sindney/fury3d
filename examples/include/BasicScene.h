#ifndef _BASIC_SCENE_H_
#define _BASIC_SCENE_H_

#include "Fury/Fury.h"

#include "FrameWork.h"

class BasicScene :
	public FrameWork
{
public:

	OcTree::Ptr m_OcTree;

	SceneNode::Ptr m_CamNode;

	Scene::Ptr m_Scene;

	Pipeline::Ptr m_Pipeline;

	int m_OldMouseX = 0, m_OldMouseY = 0;

	float m_MouseSensitivity = 0.01f;

	float m_CamSpeed = 1000;

	Vector4 m_CamPos;

public:

	BasicScene();

	virtual ~BasicScene();

	virtual void Init(sf::Window &window);

	virtual void FixedUpdate();

	virtual void Update(float dt);

	virtual void UpdateGUI(float dt);

	virtual void Draw(sf::Window &window);

};

#endif // _BASIC_SCENE_H_