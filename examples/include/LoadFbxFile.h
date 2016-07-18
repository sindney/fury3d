#ifndef _LOADFBXFILE_H_
#define _LOADFBXFILE_H_

#include "Fury.h"

#include "BasicScene.h"

class LoadFbxFile : public BasicScene
{
public:

	~LoadFbxFile();

	virtual void Init(sf::Window &window);

	virtual void Update(float dt);

	virtual void FixedUpdate();

	virtual void Draw(sf::Window &window);
};

#endif // _LOADFBXFILE_H_