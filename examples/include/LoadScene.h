#ifndef _LOADSCENE_H_
#define _LOADSCENE_H_

#include "Fury/Fury.h"

#include "BasicScene.h"

class LoadScene : public BasicScene
{
public:

	~LoadScene();

	virtual void Init(sf::Window &window);

	virtual void Update(float dt);

	virtual void FixedUpdate();

	virtual void Draw(sf::Window &window);
};

#endif // _LOADSCENE_H_