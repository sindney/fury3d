#include "Fury/Editor/EditorConfirmDialog.h"

#include <functional>
#include <optional>
#include <queue>
#include <string>

#include "ImGui/imgui.h"

namespace fury
{
	namespace Editor
	{
		namespace
		{
			struct ConfirmRequest
			{
				std::string title;
				std::string message;
				std::function<void(bool)> onResult;
			};

			// Pending-request queue. RequestConfirmDialog pushes onto
			// this; RenderConfirmDialog pops from it when no dialog is
			// currently open. Mirrors the g_SaveAsModalOpen flag-then-
			// OpenPopup pattern but generalized to a queue so multiple
			// in-flight confirms serialize.
			std::queue<ConfirmRequest> g_PendingConfirms;

			// The currently-open confirm dialog (if any). Cleared when
			// the user clicks Yes/No (callback invoked inline) or when
			// the popup is dismissed via Esc / click-outside (callback
			// invoked with false on the next frame).
			std::optional<ConfirmRequest> g_ConfirmCurrent;
		}

		void RequestConfirmDialog(const std::string& title,
			const std::string& message,
			std::function<void(bool)> onResult)
		{
			g_PendingConfirms.push({ title, message, std::move(onResult) });
		}

		void RenderConfirmDialog()
		{
			// If no dialog is currently open and the queue has a pending
			// request, pop it and open the popup. OpenPopup must run
			// BEFORE BeginPopupModal in the same ID-stack position so
			// the popup opens on this or the next frame.
			if (!g_ConfirmCurrent && !g_PendingConfirms.empty())
			{
				g_ConfirmCurrent = std::move(g_PendingConfirms.front());
				g_PendingConfirms.pop();
				ImGui::OpenPopup(g_ConfirmCurrent->title.c_str());
			}

			if (!g_ConfirmCurrent)
				return;

			// The popup ID is the request's title. Two simultaneous
			// requests with the same title cannot happen because the
			// queue serializes them (we only pop when g_ConfirmCurrent
			// is empty).
			const char* popup_id = g_ConfirmCurrent->title.c_str();

			if (ImGui::BeginPopupModal(popup_id, nullptr,
				ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::TextUnformatted(g_ConfirmCurrent->message.c_str());
				ImGui::Separator();

				if (ImGui::Button("Yes"))
				{
					auto cb = std::move(g_ConfirmCurrent->onResult);
					ImGui::CloseCurrentPopup();
					g_ConfirmCurrent.reset();
					if (cb) try { cb(true); } catch (...) {}
				}
				ImGui::SameLine();
				if (ImGui::Button("No"))
				{
					auto cb = std::move(g_ConfirmCurrent->onResult);
					ImGui::CloseCurrentPopup();
					g_ConfirmCurrent.reset();
					if (cb) try { cb(false); } catch (...) {}
				}

				ImGui::EndPopup();
			}
			else
			{
				// The popup is not rendering this frame. Since
				// g_ConfirmCurrent is still set, the popup was closed
				// without clicking Yes/No (Esc / click-outside). Treat
				// this as a No result and clear the current request so
				// the next pending request can open on the next frame.
				auto cb = std::move(g_ConfirmCurrent->onResult);
				g_ConfirmCurrent.reset();
				if (cb) try { cb(false); } catch (...) {}
			}
		}
	}
}
