#ifndef _FRAMEWORK_H_
#define _FRAMEWORK_H_

#include <memory>

#include "SFML/Window/Event.hpp"
#include "SFML/Graphics/RenderWindow.hpp"

#undef near
#undef far

using namespace std;
using namespace fury;

class FrameWork
{
public:

	typedef std::shared_ptr<FrameWork> Ptr;

	bool running = true;

	virtual void Init(sf::RenderWindow &window) = 0;

	virtual void HandleEvent(sf::Event event) = 0;

	virtual void PreFixedUpdate() = 0;

	virtual void FixedUpdate() = 0;

	virtual void PostFixedUpdate() = 0;

	virtual void Update(float dt) = 0;

	virtual void Draw(sf::RenderWindow &window) = 0;
};

#endif // _FRAMEWORK_H_