#ifndef _BASIC_SCENE_H_
#define _BASIC_SCENE_H_

#include "Fury.h"

#include "FrameWork.h"

class BasicScene :
	public FrameWork
{
public:

	OcTreeManager::Ptr m_OcTree;

	SceneNode::Ptr m_CamNode;

	SceneNode::Ptr m_RootNode;

	int m_OldMouseX = 0, m_OldMouseY = 0;

	float m_MouseSensitivity = 0.01f;

	float m_CamSpeed = 1000;

	Vector4 m_CamPos;

	bool m_MouseDown = false;

public:

	BasicScene();

	virtual ~BasicScene();

	virtual void Init(sf::RenderWindow &window);

	virtual void HandleEvent(sf::Event event);

	virtual void PreFixedUpdate();

	virtual void FixedUpdate();

	virtual void PostFixedUpdate();

	virtual void Update(float dt);

	virtual void Draw(sf::RenderWindow &window);

};

#endif // _BASIC_SCENE_H_