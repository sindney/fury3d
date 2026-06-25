#ifndef _FURY_EDITOR_THEMES_H_
#define _FURY_EDITOR_THEMES_H_

#include <cstddef>

namespace fury
{
	namespace Editor
	{
		// Theme indices match the kThemes[] table in EditorThemes.cpp.
		// Persistent storage uses the integer index, so reordering the
		// table breaks existing imgui.ini files. Append, don't reorder.
		enum class ETheme : int
		{
			Dark = 0,
			ForestGreen,
			Amethyst,
			Sapphire,
			AmberYellow,
			Dracula,
			CatppuccinMocha,
			GruvboxHard,
			CrimsonVesuvius,
			RoseQuartz,
			Cyberpunk,
			PaperAndInk,
			Count
		};

		struct ThemeEntry
		{
			const char* display_name;
			void (*apply)();
		};

		extern const ThemeEntry kThemes[];
		extern const std::size_t kThemesCount;

		void ApplyTheme(ETheme theme);

		// Re-applies the persisted theme (used after Settings handler reads
		// imgui.ini on startup).
		void ApplyPersistedTheme();

		// Active theme index, kept in sync with the Settings handler so the
		// Settings combo and ApplyPersistedTheme observe the same value.
		int GetCurrentThemeIndex();
		void SetCurrentThemeIndex(int idx);
	}
}

#endif // _FURY_EDITOR_THEMES_H_
