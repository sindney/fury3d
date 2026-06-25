#include "Fury/Editor/EditorThemes.h"

#include "ImGui/imgui.h"

namespace fury
{
	namespace Editor
	{
		namespace
		{
			// The 12 theme bodies are copied verbatim from
			// /Users/sindney/Documents/git/furyengine/imgui_styles/themes-by-TheAncientOwl.md
			// into EditorThemes_inline.cpp.inc.
			#include "Fury/Editor/EditorThemes_inline.cpp.inc"
		}

		const ThemeEntry kThemes[] = {
			{"Dark",             &SetupImGuiDarkStyle},
			{"Forest Green",     &SetupForestGreenStyle},
			{"Amethyst",         &SetupImGuiAmethystStyle},
			{"Sapphire",         &SetupImGuiSapphireStyle},
			{"Amber Yellow",     &SetupImGuiAmberYellowStyle},
			{"Dracula",          &SetupImGuiDraculaStyle},
			{"Catppuccin Mocha", &SetupImGuiCatppuccinMochaStyle},
			{"Gruvbox Hard",     &SetupImGuiGruvboxHardStyle},
			{"Crimson Vesuvius", &SetupImGuiCrimsonVesuviusStyle},
			{"Rose Quartz",      &SetupImGuiRoseQuartzStyle},
			{"Cyberpunk",        &SetupImGuiCyberpunkStyle},
			{"Paper And Ink",    &SetupImGuiPaperAndInkStyle},
		};

		const std::size_t kThemesCount = sizeof(kThemes) / sizeof(kThemes[0]);

		static int s_CurrentThemeIndex = 0;

		void ApplyTheme(ETheme theme)
		{
			int idx = static_cast<int>(theme);
			if (idx < 0 || idx >= (int)kThemesCount) idx = 0;
			s_CurrentThemeIndex = idx;
			kThemes[idx].apply();
		}

		void ApplyPersistedTheme()
		{
			ApplyTheme(static_cast<ETheme>(s_CurrentThemeIndex));
		}

		int GetCurrentThemeIndex() { return s_CurrentThemeIndex; }

		void SetCurrentThemeIndex(int idx)
		{
			if (idx < 0 || idx >= (int)kThemesCount) idx = 0;
			s_CurrentThemeIndex = idx;
		}
	}
}
