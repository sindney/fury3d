// dear imgui: Platform Backend for SFML 3.1
// (Custom written for fury3d — modeled after upstream imgui_impl_sdl3.cpp.)
//
// Implemented features:
//  [X] Platform: Mouse cursor shape and visibility (ImGuiBackendFlags_HasMouseCursors).
//  [X] Platform: Clipboard support.
//  [X] Platform: Keyboard arrays indexed using ImGuiKey values.

#include "imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui_impl_sfml3.h"

#include <SFML/Window/Window.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>
#include <SFML/Window/Cursor.hpp>
#include <SFML/Window/Clipboard.hpp>
#include <SFML/System/String.hpp>

#include <memory>
#include <string>

// SFML 3.1 data
struct ImGui_ImplSFML3_Data
{
    sf::Window* Window;
    bool        WindowHasFocus;
    bool        MousePressed[3];
    float       MouseWheelV;
    float       MouseWheelH;
    bool        InstalledCallbacks;
    std::string ClipboardTextData;

    std::unique_ptr<sf::Cursor> MouseCursors[ImGuiMouseCursor_COUNT];

    ImGui_ImplSFML3_Data() { memset((void*)this, 0, sizeof(*this)); }
};

// Backend data stored in io.BackendPlatformUserData. Single context-only;
// fury3d only ever creates one ImGui context.
static ImGui_ImplSFML3_Data* ImGui_ImplSFML3_GetBackendData()
{
    return ImGui::GetCurrentContext()
        ? (ImGui_ImplSFML3_Data*)ImGui::GetIO().BackendPlatformUserData
        : nullptr;
}

// ----------------------------------------------------------------------------
// Clipboard
// ----------------------------------------------------------------------------
static const char* ImGui_ImplSFML3_GetClipboardText(ImGuiContext*)
{
    ImGui_ImplSFML3_Data* bd = ImGui_ImplSFML3_GetBackendData();
    bd->ClipboardTextData = sf::Clipboard::getString().toAnsiString();
    return bd->ClipboardTextData.c_str();
}

static void ImGui_ImplSFML3_SetClipboardText(ImGuiContext*, const char* text)
{
    sf::Clipboard::setString(sf::String(text ? text : ""));
}

// ----------------------------------------------------------------------------
// Key mapping
// ----------------------------------------------------------------------------
static ImGuiKey ImGui_ImplSFML3_KeyToImGuiKey(sf::Keyboard::Key key)
{
    using K = sf::Keyboard::Key;
    switch (key)
    {
        case K::Tab:        return ImGuiKey_Tab;
        case K::Left:       return ImGuiKey_LeftArrow;
        case K::Right:      return ImGuiKey_RightArrow;
        case K::Up:         return ImGuiKey_UpArrow;
        case K::Down:       return ImGuiKey_DownArrow;
        case K::PageUp:     return ImGuiKey_PageUp;
        case K::PageDown:   return ImGuiKey_PageDown;
        case K::Home:       return ImGuiKey_Home;
        case K::End:        return ImGuiKey_End;
        case K::Insert:     return ImGuiKey_Insert;
        case K::Delete:     return ImGuiKey_Delete;
        case K::Backspace:  return ImGuiKey_Backspace;
        case K::Space:      return ImGuiKey_Space;
        case K::Enter:      return ImGuiKey_Enter;
        case K::Escape:     return ImGuiKey_Escape;
        case K::Apostrophe: return ImGuiKey_Apostrophe;
        case K::Comma:      return ImGuiKey_Comma;
        case K::Hyphen:     return ImGuiKey_Minus;
        case K::Period:     return ImGuiKey_Period;
        case K::Slash:      return ImGuiKey_Slash;
        case K::Semicolon:  return ImGuiKey_Semicolon;
        case K::Equal:      return ImGuiKey_Equal;
        case K::LBracket:   return ImGuiKey_LeftBracket;
        case K::Backslash:  return ImGuiKey_Backslash;
        case K::RBracket:   return ImGuiKey_RightBracket;
        case K::Grave:      return ImGuiKey_GraveAccent;
        case K::LControl:   return ImGuiKey_LeftCtrl;
        case K::LShift:     return ImGuiKey_LeftShift;
        case K::LAlt:       return ImGuiKey_LeftAlt;
        case K::LSystem:    return ImGuiKey_LeftSuper;
        case K::RControl:   return ImGuiKey_RightCtrl;
        case K::RShift:     return ImGuiKey_RightShift;
        case K::RAlt:       return ImGuiKey_RightAlt;
        case K::RSystem:    return ImGuiKey_RightSuper;
        case K::Menu:       return ImGuiKey_Menu;
        case K::Num0:       return ImGuiKey_0;
        case K::Num1:       return ImGuiKey_1;
        case K::Num2:       return ImGuiKey_2;
        case K::Num3:       return ImGuiKey_3;
        case K::Num4:       return ImGuiKey_4;
        case K::Num5:       return ImGuiKey_5;
        case K::Num6:       return ImGuiKey_6;
        case K::Num7:       return ImGuiKey_7;
        case K::Num8:       return ImGuiKey_8;
        case K::Num9:       return ImGuiKey_9;
        case K::A:          return ImGuiKey_A;
        case K::B:          return ImGuiKey_B;
        case K::C:          return ImGuiKey_C;
        case K::D:          return ImGuiKey_D;
        case K::E:          return ImGuiKey_E;
        case K::F:          return ImGuiKey_F;
        case K::G:          return ImGuiKey_G;
        case K::H:          return ImGuiKey_H;
        case K::I:          return ImGuiKey_I;
        case K::J:          return ImGuiKey_J;
        case K::K:          return ImGuiKey_K;
        case K::L:          return ImGuiKey_L;
        case K::M:          return ImGuiKey_M;
        case K::N:          return ImGuiKey_N;
        case K::O:          return ImGuiKey_O;
        case K::P:          return ImGuiKey_P;
        case K::Q:          return ImGuiKey_Q;
        case K::R:          return ImGuiKey_R;
        case K::S:          return ImGuiKey_S;
        case K::T:          return ImGuiKey_T;
        case K::U:          return ImGuiKey_U;
        case K::V:          return ImGuiKey_V;
        case K::W:          return ImGuiKey_W;
        case K::X:          return ImGuiKey_X;
        case K::Y:          return ImGuiKey_Y;
        case K::Z:          return ImGuiKey_Z;
        case K::F1:         return ImGuiKey_F1;
        case K::F2:         return ImGuiKey_F2;
        case K::F3:         return ImGuiKey_F3;
        case K::F4:         return ImGuiKey_F4;
        case K::F5:         return ImGuiKey_F5;
        case K::F6:         return ImGuiKey_F6;
        case K::F7:         return ImGuiKey_F7;
        case K::F8:         return ImGuiKey_F8;
        case K::F9:         return ImGuiKey_F9;
        case K::F10:        return ImGuiKey_F10;
        case K::F11:        return ImGuiKey_F11;
        case K::F12:        return ImGuiKey_F12;
        case K::Pause:      return ImGuiKey_Pause;
        case K::Numpad0:    return ImGuiKey_Keypad0;
        case K::Numpad1:    return ImGuiKey_Keypad1;
        case K::Numpad2:    return ImGuiKey_Keypad2;
        case K::Numpad3:    return ImGuiKey_Keypad3;
        case K::Numpad4:    return ImGuiKey_Keypad4;
        case K::Numpad5:    return ImGuiKey_Keypad5;
        case K::Numpad6:    return ImGuiKey_Keypad6;
        case K::Numpad7:    return ImGuiKey_Keypad7;
        case K::Numpad8:    return ImGuiKey_Keypad8;
        case K::Numpad9:    return ImGuiKey_Keypad9;
        case K::Add:        return ImGuiKey_KeypadAdd;
        case K::Subtract:   return ImGuiKey_KeypadSubtract;
        case K::Multiply:   return ImGuiKey_KeypadMultiply;
        case K::Divide:     return ImGuiKey_KeypadDivide;
        default:            return ImGuiKey_None;
    }
}

static void ImGui_ImplSFML3_UpdateKeyModifiers(bool ctrl, bool shift, bool alt, bool super)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl,  ctrl);
    io.AddKeyEvent(ImGuiMod_Shift, shift);
    io.AddKeyEvent(ImGuiMod_Alt,   alt);
    io.AddKeyEvent(ImGuiMod_Super, super);
}

// ----------------------------------------------------------------------------
// Event handling
// ----------------------------------------------------------------------------
void ImGui_ImplSFML3_ProcessEvent(const sf::Event& event)
{
    ImGui_ImplSFML3_Data* bd = ImGui_ImplSFML3_GetBackendData();
    IM_ASSERT(bd != nullptr && "Did you call ImGui_ImplSFML3_Init()?");
    ImGuiIO& io = ImGui::GetIO();

    if (event.is<sf::Event::FocusGained>())
    {
        bd->WindowHasFocus = true;
        io.AddFocusEvent(true);
        return;
    }
    if (event.is<sf::Event::FocusLost>())
    {
        bd->WindowHasFocus = false;
        io.AddFocusEvent(false);
        return;
    }

    if (const auto* moved = event.getIf<sf::Event::MouseMoved>())
    {
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMousePosEvent((float)moved->position.x, (float)moved->position.y);
        return;
    }

    if (const auto* btn = event.getIf<sf::Event::MouseButtonPressed>())
    {
        int idx = -1;
        switch (btn->button)
        {
            case sf::Mouse::Button::Left:   idx = 0; break;
            case sf::Mouse::Button::Right:  idx = 1; break;
            case sf::Mouse::Button::Middle: idx = 2; break;
            case sf::Mouse::Button::Extra1: idx = 3; break;
            case sf::Mouse::Button::Extra2: idx = 4; break;
        }
        if (idx >= 0)
        {
            io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
            io.AddMouseButtonEvent(idx, true);
        }
        return;
    }

    if (const auto* btn = event.getIf<sf::Event::MouseButtonReleased>())
    {
        int idx = -1;
        switch (btn->button)
        {
            case sf::Mouse::Button::Left:   idx = 0; break;
            case sf::Mouse::Button::Right:  idx = 1; break;
            case sf::Mouse::Button::Middle: idx = 2; break;
            case sf::Mouse::Button::Extra1: idx = 3; break;
            case sf::Mouse::Button::Extra2: idx = 4; break;
        }
        if (idx >= 0)
        {
            io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
            io.AddMouseButtonEvent(idx, false);
        }
        return;
    }

    if (const auto* wheel = event.getIf<sf::Event::MouseWheelScrolled>())
    {
        if (wheel->wheel == sf::Mouse::Wheel::Vertical)
            io.AddMouseWheelEvent(0.0f, wheel->delta);
        else
            io.AddMouseWheelEvent(wheel->delta, 0.0f);
        return;
    }

    if (const auto* key = event.getIf<sf::Event::KeyPressed>())
    {
        ImGui_ImplSFML3_UpdateKeyModifiers(key->control, key->shift, key->alt, key->system);
        ImGuiKey ik = ImGui_ImplSFML3_KeyToImGuiKey(key->code);
        if (ik != ImGuiKey_None)
            io.AddKeyEvent(ik, true);
        return;
    }

    if (const auto* key = event.getIf<sf::Event::KeyReleased>())
    {
        ImGui_ImplSFML3_UpdateKeyModifiers(key->control, key->shift, key->alt, key->system);
        ImGuiKey ik = ImGui_ImplSFML3_KeyToImGuiKey(key->code);
        if (ik != ImGuiKey_None)
            io.AddKeyEvent(ik, false);
        return;
    }

    if (const auto* text = event.getIf<sf::Event::TextEntered>())
    {
        if (text->unicode >= 32 && text->unicode != 127)
            io.AddInputCharacter(text->unicode);
        return;
    }
}

// ----------------------------------------------------------------------------
// Mouse cursor
// ----------------------------------------------------------------------------
static void ImGui_ImplSFML3_UpdateMouseCursor()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
        return;

    ImGui_ImplSFML3_Data* bd = ImGui_ImplSFML3_GetBackendData();
    if (bd->Window == nullptr)
        return;

    ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
    if (cursor == ImGuiMouseCursor_None || io.MouseDrawCursor)
    {
        bd->Window->setMouseCursorVisible(false);
        return;
    }

    if (sf::Cursor* cur = bd->MouseCursors[cursor].get())
    {
        bd->Window->setMouseCursor(*cur);
        bd->Window->setMouseCursorVisible(true);
    }
    else
    {
        bd->Window->setMouseCursorVisible(true);
    }
}

static void ImGui_ImplSFML3_LoadCursors()
{
    ImGui_ImplSFML3_Data* bd = ImGui_ImplSFML3_GetBackendData();

    auto load = [](sf::Cursor::Type t) -> std::unique_ptr<sf::Cursor> {
        // SFML 3.1 returns std::optional<sf::Cursor> from createFromSystem.
        if (auto cur = sf::Cursor::createFromSystem(t))
            return std::make_unique<sf::Cursor>(std::move(*cur));
        return nullptr;
    };

    bd->MouseCursors[ImGuiMouseCursor_Arrow]      = load(sf::Cursor::Type::Arrow);
    bd->MouseCursors[ImGuiMouseCursor_TextInput]  = load(sf::Cursor::Type::Text);
    bd->MouseCursors[ImGuiMouseCursor_ResizeAll]  = load(sf::Cursor::Type::SizeAll);
    bd->MouseCursors[ImGuiMouseCursor_ResizeNS]   = load(sf::Cursor::Type::SizeVertical);
    bd->MouseCursors[ImGuiMouseCursor_ResizeEW]   = load(sf::Cursor::Type::SizeHorizontal);
    bd->MouseCursors[ImGuiMouseCursor_ResizeNESW] = load(sf::Cursor::Type::SizeBottomLeftTopRight);
    bd->MouseCursors[ImGuiMouseCursor_ResizeNWSE] = load(sf::Cursor::Type::SizeTopLeftBottomRight);
    bd->MouseCursors[ImGuiMouseCursor_Hand]       = load(sf::Cursor::Type::Hand);
    bd->MouseCursors[ImGuiMouseCursor_NotAllowed] = load(sf::Cursor::Type::NotAllowed);
}

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------
bool ImGui_ImplSFML3_Init(sf::Window* window)
{
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");

    ImGui_ImplSFML3_Data* bd = IM_NEW(ImGui_ImplSFML3_Data)();
    io.BackendPlatformUserData = (void*)bd;
    io.BackendPlatformName     = "imgui_impl_sfml3";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

    bd->Window         = window;
    bd->WindowHasFocus = true;

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Platform_SetClipboardTextFn = ImGui_ImplSFML3_SetClipboardText;
    platform_io.Platform_GetClipboardTextFn = ImGui_ImplSFML3_GetClipboardText;
    platform_io.Platform_ClipboardUserData  = nullptr;

    ImGui_ImplSFML3_LoadCursors();

    return true;
}

void ImGui_ImplSFML3_Shutdown()
{
    ImGui_ImplSFML3_Data* bd = ImGui_ImplSFML3_GetBackendData();
    if (!bd) return;

    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName     = nullptr;
    io.BackendPlatformUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos);

    IM_DELETE(bd);
}

void ImGui_ImplSFML3_NewFrame(sf::Window* window, float deltaTime)
{
    ImGui_ImplSFML3_Data* bd = ImGui_ImplSFML3_GetBackendData();
    IM_ASSERT(bd != nullptr && "Did you call ImGui_ImplSFML3_Init()?");
    bd->Window = window;

    ImGuiIO& io = ImGui::GetIO();

    sf::Vector2u winSize = window->getSize();
    io.DisplaySize             = ImVec2((float)winSize.x, (float)winSize.y);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime               = deltaTime > 0.0f ? deltaTime : (1.0f / 60.0f);

    ImGui_ImplSFML3_UpdateMouseCursor();
}

#endif // #ifndef IMGUI_DISABLE
