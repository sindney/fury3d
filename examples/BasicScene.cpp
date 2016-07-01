#include <Imgui/imgui.h>
#include <Imgui/imgui_fury.h>

#include "BasicScene.h"

BasicScene::BasicScene()
{

}

BasicScene::~BasicScene()
{
	m_OcTree->Clear();
	m_RootNode->RemoveAllChilds();
}

void BasicScene::Init(sf::Window &window)
{

}

void BasicScene::PreFixedUpdate()
{
	m_CamNode->GetComponent<Transform>()->SyncTransforms();
}

void BasicScene::FixedUpdate()
{
	auto &inputMgr = InputUtil::Instance();

	if (inputMgr->GetKeyDown(sf::Keyboard::Escape))
		running = false;

	if (inputMgr->GetKeyDown(sf::Keyboard::A))
		m_CamPos.x = -m_CamSpeed;
	else if (inputMgr->GetKeyDown(sf::Keyboard::D))
		m_CamPos.x = m_CamSpeed;
	else
		m_CamPos.x = 0;

	if (inputMgr->GetKeyDown(sf::Keyboard::W))
		m_CamPos.z = -m_CamSpeed;
	else if (inputMgr->GetKeyDown(sf::Keyboard::S))
		m_CamPos.z = m_CamSpeed;
	else
		m_CamPos.z = 0;

	auto mousePos = inputMgr->GetMousePosition();
	if (inputMgr->GetMouseDown(sf::Mouse::Middle) || inputMgr->GetMouseDown(sf::Mouse::Right))
	{
		float tx = (m_OldMouseX - mousePos.first) * m_MouseSensitivity;
		float ty = (m_OldMouseY - mousePos.second) * m_MouseSensitivity;

		Transform::Ptr camTrans = m_CamNode->GetComponent<Transform>();
		Vector4 euler = MathUtil::QuatToEulerRad(camTrans->GetPostRotation());
		euler.x += tx;
		euler.y += ty;

		static const float DEGREE_90 = 3.1416f / 2;

		if (euler.y >= DEGREE_90)
			euler.y = DEGREE_90;
		else if (euler.y <= -DEGREE_90)
			euler.y = -DEGREE_90;

		camTrans->SetPostRotation(MathUtil::EulerRadToQuat(euler));
	}
	m_OldMouseX = mousePos.first;
	m_OldMouseY = mousePos.second;
}

void BasicScene::PostFixedUpdate()
{
	m_CamNode->GetComponent<Transform>()->SetPostPosition(m_CamNode->GetWorldMatrix().Multiply(m_CamPos));
}

void BasicScene::Update(float dt)
{
	m_CamNode->GetComponent<Transform>()->SetDeltaTime(dt);
}

void BasicScene::UpdateGUI(float dt)
{
	static bool showProfilerWindow = true, showGBufferWindow = false, showShadowBufferWindow = false;

	ImGui::Begin("Profiler", &showProfilerWindow, ImVec2(240, 290), 1.0f, 
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_ShowBorders | ImGuiWindowFlags_NoCollapse);

	// fps graph
	{
		static float upper_bound = 100.0f;

		float curFps = ImGui::GetIO().Framerate;
		while (curFps > upper_bound) upper_bound += 50;
		while (upper_bound - 50 > curFps) upper_bound -= 50;

		ImGui::PlotVar("FPS", curFps, 1, upper_bound, 210, 30);
	}

	ImGui::Separator();

	ImGui::Text("DrawCall: %i", RenderUtil::Instance()->GetDrawCall());
	ImGui::Text("Triangles: %i", RenderUtil::Instance()->GetTriangleCount());
	ImGui::Text("Mesh: %i", RenderUtil::Instance()->GetMeshCount());
	ImGui::Text("SkinnedMesh: %i", RenderUtil::Instance()->GetSkinnedMeshCount());
	ImGui::Text("Light: %i", RenderUtil::Instance()->GetLightCount());

	// switches
	{
		static bool draw_light_bounds = false, draw_mesh_bounds = false, draw_custom_bounds = false;

		ImGui::Separator();

		ImGui::Checkbox("Draw Light Bounds", &draw_light_bounds);
		ImGui::Checkbox("Draw Mesh Bounds", &draw_mesh_bounds);
		ImGui::Checkbox("Draw Custom Bounds", &draw_custom_bounds);

		unsigned int debugFlags = 0;
		if (draw_light_bounds) debugFlags |= PipelineDebugFlags::LIGHT_BOUNDS;
		if (draw_mesh_bounds) debugFlags |= PipelineDebugFlags::MESH_BOUNDS;
		if (draw_custom_bounds) debugFlags |= PipelineDebugFlags::CUSTOM_BOUNDS;
		m_Pipeline->SetDebugFlags(debugFlags);

		ImGui::Checkbox("Show GBuffer Window", &showGBufferWindow);
		ImGui::Checkbox("Show ShadowBuffer Window", &showShadowBufferWindow);
	}

	ImGui::End();

	if (showShadowBufferWindow)
	{
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 300, ImGui::GetIO().DisplaySize.y - 300), ImGuiSetCond_FirstUseEver);
		ImGui::Begin("Shadow Buffers", &showShadowBufferWindow, ImVec2(300, 300), 1.0f, 
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_ShowBorders | ImGuiWindowFlags_NoCollapse);

		if (auto ptr = EntityUtil::Instance()->Get<Texture>("1024*1024*0*depth24*2d"))
		{
			ImGui::Text("2DTexture Buffer: ");
			ImGui::Image((ImTextureID)ptr->GetID(), ImVec2(256, 256), ImVec2(0, 1), ImVec2(1, 0));
		}

		// cube_texture
		if (auto ptr = EntityUtil::Instance()->Get<Texture>("512*512*0*depth24*cube"))
		{
			static auto img0 = Texture::Pool.Get(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			static auto img1 = Texture::Pool.Get(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			static auto img2 = Texture::Pool.Get(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			static auto img3 = Texture::Pool.Get(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			static auto img4 = Texture::Pool.Get(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			static auto img5 = Texture::Pool.Get(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);

			static auto blitShader = Shader::Create("BlitShader", ShaderType::OTHER);

			if (blitShader->GetDirty())
			{
				const char *blit_vs =
					"in vec3 vertex_position;"
					"out vec2 out_uv;"
					"void main()"
					"{"
					"	out_uv = vertex_position.xy * 0.5 + 0.5;"
					"	gl_Position = vec4(vertex_position.xy, 0.0, 1.0);"
					"}";

				const char *blit_fs =
					"uniform samplerCube src;"
					"uniform mat4 matrix;"
					"in vec2 out_uv;"
					"out vec4 fragment_output;"
					"void main()"
					"{"
					"   vec4 dir = matrix * vec4(out_uv.x, 1.0 - out_uv.y, 1.0, 1.0);"
					"	fragment_output = texture(src, dir.xyz);"
					"}";

				blitShader->Compile(blit_vs, blit_fs, "");
			}

			std::array<Matrix4, 6> dirMatrices;
			Vector4 lightPos(0, 0, 0, 1);
			dirMatrices[0].LookAt(lightPos, lightPos + Vector4(1.0f, 0.0f, 0.0f), Vector4(0.0f, -1.0f, 0.0f));
			dirMatrices[1].LookAt(lightPos, lightPos + Vector4(-1.0f, 0.0f, 0.0f), Vector4(0.0f, -1.0f, 0.0f));
			dirMatrices[2].LookAt(lightPos, lightPos + Vector4(0.0f, 1.0f, 0.0f), Vector4(0.0f, 0.0f, 1.0f));
			dirMatrices[3].LookAt(lightPos, lightPos + Vector4(0.0f, -1.0f, 0.0f), Vector4(0.0f, 0.0f, -1.0f));
			dirMatrices[4].LookAt(lightPos, lightPos + Vector4(0.0f, 0.0f, 1.0f), Vector4(0.0f, -1.0f, 0.0f));
			dirMatrices[5].LookAt(lightPos, lightPos + Vector4(0.0f, 0.0f, -1.0f), Vector4(0.0f, -1.0f, 0.0f));

			auto render = RenderUtil::Instance();

			blitShader->Bind();
			blitShader->BindMatrix("matrix", dirMatrices[0]);
			render->Blit(ptr, img0, blitShader);

			blitShader->Bind();
			blitShader->BindMatrix("matrix", dirMatrices[1]);
			render->Blit(ptr, img1, blitShader);

			blitShader->Bind();
			blitShader->BindMatrix("matrix", dirMatrices[2]);
			render->Blit(ptr, img2, blitShader);

			blitShader->Bind();
			blitShader->BindMatrix("matrix", dirMatrices[3]);
			render->Blit(ptr, img3, blitShader);

			blitShader->Bind();
			blitShader->BindMatrix("matrix", dirMatrices[4]);
			render->Blit(ptr, img4, blitShader);

			blitShader->Bind();
			blitShader->BindMatrix("matrix", dirMatrices[5]);
			render->Blit(ptr, img5, blitShader);

			ImGui::Text("CubeTexture Buffer: ");
			ImGui::BeginGroup();

			ImGui::Image((ImTextureID)img0->GetID(), ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
			ImGui::SameLine(140);
			ImGui::Image((ImTextureID)img1->GetID(), ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));

			ImGui::Image((ImTextureID)img2->GetID(), ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
			ImGui::SameLine(140);
			ImGui::Image((ImTextureID)img3->GetID(), ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));

			ImGui::Image((ImTextureID)img4->GetID(), ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
			ImGui::SameLine(140);
			ImGui::Image((ImTextureID)img5->GetID(), ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));

			ImGui::EndGroup();
		}

		// texture_array
		if (auto ptr = EntityUtil::Instance()->Get<Texture>("1024*1024*4*depth24*2d_array"))
		{
			static auto img0 = Texture::Pool.Get(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			static auto img1 = Texture::Pool.Get(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			static auto img2 = Texture::Pool.Get(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			static auto img3 = Texture::Pool.Get(128, 128, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			static auto blitShader = Shader::Create("BlitShader", ShaderType::OTHER);

			if (blitShader->GetDirty())
			{
				const char *blit_vs =
					"in vec3 vertex_position;"
					"out vec2 out_uv;"
					"void main()"
					"{"
					"	out_uv = vertex_position.xy * 0.5 + 0.5;"
					"	gl_Position = vec4(vertex_position.xy, 0.0, 1.0);"
					"}";

				const char *blit_fs =
					"uniform sampler2DArray src;"
					"uniform float index;"
					"in vec2 out_uv;"
					"out vec4 fragment_output;"
					"void main()"
					"{"
					"	fragment_output = texture(src, vec3(out_uv, index));"
					"}";

				blitShader->Compile(blit_vs, blit_fs, "");
			}

			auto render = RenderUtil::Instance();

			blitShader->Bind();
			blitShader->BindFloat("index", 0);
			render->Blit(ptr, img0, blitShader);

			blitShader->Bind();
			blitShader->BindFloat("index", 1);
			render->Blit(ptr, img1, blitShader);

			blitShader->Bind();
			blitShader->BindFloat("index", 2);
			render->Blit(ptr, img2, blitShader);

			blitShader->Bind();
			blitShader->BindFloat("index", 3);
			render->Blit(ptr, img3, blitShader);

			ImGui::Text("ArrayTexture Buffer: ");
			ImGui::BeginGroup();

			ImGui::Image((ImTextureID)img0->GetID(), ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
			ImGui::SameLine(140);
			ImGui::Image((ImTextureID)img1->GetID(), ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));

			ImGui::Image((ImTextureID)img2->GetID(), ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
			ImGui::SameLine(140);
			ImGui::Image((ImTextureID)img3->GetID(), ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));

			ImGui::EndGroup();
		}

		ImGui::End();
	}

	if (showGBufferWindow)
	{
		ImGui::SetNextWindowPos(ImVec2(ImVec2(ImGui::GetIO().DisplaySize.x - 300, 0)), ImGuiSetCond_FirstUseEver);
		ImGui::Begin("GBuffers", &showGBufferWindow, ImVec2(300, 300), 1.0f,
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_ShowBorders | ImGuiWindowFlags_NoCollapse);

		ImVec2 imgSize(ImGui::GetIO().DisplaySize.x / 4, ImGui::GetIO().DisplaySize.y / 4);

		ImGui::Text("Depth Buffer: ");
		if (auto ptr = m_Pipeline->GetTextureByName("gbuffer_depth"))
			ImGui::Image((ImTextureID)ptr->GetID(), imgSize, ImVec2(0, 1), ImVec2(1, 0));

		ImGui::Text("Normal Buffer: ");
		if (auto ptr = m_Pipeline->GetTextureByName("gbuffer_normal"))
			ImGui::Image((ImTextureID)ptr->GetID(), imgSize, ImVec2(0, 1), ImVec2(1, 0));

		ImGui::Text("Diffuse Buffer: ");
		if (auto ptr = m_Pipeline->GetTextureByName("gbuffer_diffuse"))
			ImGui::Image((ImTextureID)ptr->GetID(), imgSize, ImVec2(0, 1), ImVec2(1, 0));

		ImGui::Text("Light Buffer: ");
		if (auto ptr = m_Pipeline->GetTextureByName("gbuffer_light"))
			ImGui::Image((ImTextureID)ptr->GetID(), imgSize, ImVec2(0, 1), ImVec2(1, 0));

		ImGui::End();
	}

	// static bool m_ShowProfilerWindow = true;
	// ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiSetCond_FirstUseEver);
	// ImGui::ShowTestWindow(&m_ShowProfilerWindow);

	// ImGui::ShowStyleEditor(&ImGui::GetStyle());
}

void BasicScene::Draw(sf::Window &window)
{

}