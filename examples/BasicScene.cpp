#include "BasicScene.h"

BasicScene::BasicScene()
{

}

BasicScene::~BasicScene()
{

}

void BasicScene::Init(sf::RenderWindow &window)
{
	
}

const float DEGREE_90 = 3.1416f / 2;

void BasicScene::HandleEvent(sf::Event event)
{
	switch (event.type)
	{
	case sf::Event::Closed:
		running = false;
		break;
	case sf::Event::KeyPressed:
		switch (event.key.code)
		{
		case sf::Keyboard::A:
			m_CamPos.x = -m_CamSpeed;
			break;
		case sf::Keyboard::D:
			m_CamPos.x = m_CamSpeed;
			break;
		case sf::Keyboard::W:
			m_CamPos.z = -m_CamSpeed;
			break;
		case sf::Keyboard::S:
			m_CamPos.z = m_CamSpeed;
			break;
		default:
			break;
		}
		break;
	case sf::Event::KeyReleased:
		switch (event.key.code)
		{
		case sf::Keyboard::A:
			m_CamPos.x = 0;
			break;
		case sf::Keyboard::D:
			m_CamPos.x = 0;
			break;
		case sf::Keyboard::W:
			m_CamPos.z = 0;
			break;
		case sf::Keyboard::S:
			m_CamPos.z = 0;
			break;
		}
		break;
	case sf::Event::MouseButtonPressed:
		m_MouseDown = true;
		break;
	case sf::Event::MouseButtonReleased:
		m_MouseDown = false;
		break;
	case sf::Event::MouseLeft:
		m_MouseDown = false;
		break;
	case sf::Event::MouseMoved:
		float tx, ty;
		if (m_MouseDown)
		{
			tx = (m_OldMouseX - event.mouseMove.x) * m_MouseSensitivity;
			ty = (m_OldMouseY - event.mouseMove.y) * m_MouseSensitivity;

			Transform::Ptr camTrans = m_CamNode->GetComponent<Transform>();
			Vector4 euler = Angle::QuatToEulerRad(camTrans->GetPostRotation());
			euler.x += tx;
			euler.y += ty;

			if (euler.y >= DEGREE_90)
				euler.y = DEGREE_90;
			else if (euler.y <= -DEGREE_90)
				euler.y = -DEGREE_90;

			camTrans->SetPostRotation(Angle::EulerRadToQuat(euler));
		}
		m_OldMouseX = event.mouseMove.x;
		m_OldMouseY = event.mouseMove.y;

		break;
	default:
		break;
	}
}

void BasicScene::PreFixedUpdate()
{
	m_CamNode->GetComponent<Transform>()->SyncTransforms();
}

void BasicScene::FixedUpdate()
{

}

void BasicScene::PostFixedUpdate()
{
	m_CamNode->GetComponent<Transform>()->SetPostPosition(m_CamNode->GetWorldMatrix().Multiply(m_CamPos));
}

void BasicScene::Update(float dt)
{
	m_CamNode->GetComponent<Transform>()->SetDeltaTime(dt);
}

void BasicScene::Draw(sf::RenderWindow &window)
{

}