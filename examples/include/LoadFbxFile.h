#ifndef _LOADFBXFILE_H_
#define _LOADFBXFILE_H_

#include "Fury.h"

#include "BasicScene.h"

class LoadFbxFile : public BasicScene
{
protected:

	Pipeline::Ptr m_Pipeline;

	AnimationPlayer::Ptr m_AnimPlayer;

public:

	virtual void Init(sf::RenderWindow &window);

	virtual void Update(float dt);

	virtual void FixedUpdate();

	virtual void Draw(sf::RenderWindow &window);
};

#endif // _LOADFBXFILE_H_