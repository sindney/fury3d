#include "Fury/Editor/EditorPackageDialog.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "Fury/FileUtil.h"
#include "Fury/Log.h"
#include "ImGui/imgui.h"
#include "nfd.h"

#if PLATFORM_WINDOWS
	#include <io.h>
	#include <windows.h>
#else
	#include <fcntl.h>
	#include <signal.h>
	#include <sys/types.h>
	#include <sys/wait.h>
	#include <unistd.h>
#endif

namespace fury
{
	namespace Editor
	{
		namespace
		{
			const char* const kPopupTitle = "Package Scene";
			// Printed by the shell wrapper after furye-cli exits so the
			// dialog recovers the exit code even when SIGCHLD is SIG_IGN
			// (LaunchDetached sets that once Play is used) and waitpid can
			// no longer report it.
			const char* const kExitMarker = "__FURY_PACKAGE_EXIT__";

			const char* const kCompressionItems = "lz4\0none\0";
			const char* const kTextureTargetItems = "Host default\0legacy\0modern\0";

			enum class Phase
			{
				Settings,
				Running,
				Done
			};

			struct PackageDialogState
			{
				bool open = false;
				bool pendingOpen = false;
				bool everVisible = false;
				std::string scenePath;
				std::string outputFolder;
				int compression = 0;   // 0 = lz4, 1 = none
				int textureTarget = 0; // 0 = host default, 1 = legacy, 2 = modern
				bool verbose = false;

				Phase phase = Phase::Settings;
				std::string log;
				std::string readBuf; // partial trailing line from the pipe
				std::string pakPath;
				bool cancelled = false;
				bool exitCodeValid = false;
				int exitCode = -1;

#if PLATFORM_WINDOWS
				FILE* pipe = nullptr;
#else
				pid_t pid = -1;
				int pipeFd = -1;
#endif
			};

			PackageDialogState g_Package;

			std::string ShellQuote(const std::string& value)
			{
#if PLATFORM_WINDOWS
				return "\"" + value + "\"";
#else
				std::string out = "'";
				for (const char c : value)
				{
					if (c == '\'') out += "'\\''";
					else out += c;
				}
				out += "'";
				return out;
#endif
			}

			// Full command line for the spawned shell; on success
			// *pakPathOut receives <outputFolder>/<sceneStem>.pak.
			std::string BuildCommand(std::string* pakPathOut)
			{
				const std::filesystem::path sceneFs(g_Package.scenePath);
				const std::string pakPath =
					(std::filesystem::path(g_Package.outputFolder) /
					 (sceneFs.stem().generic_string() + ".pak"))
						.generic_string();
				*pakPathOut = pakPath;

				std::string cli = std::filesystem::path(FileUtil::GetExecutablePath())
									  .parent_path()
									  .generic_string() +
					"/furye-cli";
#if PLATFORM_WINDOWS
				cli += ".exe";
#endif

				std::string cmd = ShellQuote(cli) + " package " +
					ShellQuote(g_Package.scenePath) + " --compression " +
					(g_Package.compression == 1 ? "none" : "lz4") + " --output " +
					ShellQuote(pakPath);
				if (g_Package.textureTarget == 1) cmd += " --texture-target legacy";
				if (g_Package.textureTarget == 2) cmd += " --texture-target modern";
				if (g_Package.verbose) cmd += " --verbose";
#if PLATFORM_WINDOWS
				cmd += std::string(" & echo ") + kExitMarker + " %ERRORLEVEL%";
#else
				cmd += std::string("; echo ") + kExitMarker + " $?";
#endif
				return cmd;
			}

			// Split freshly read pipe bytes into lines; marker lines update
			// the recorded exit code, everything else appends to the log.
			void AppendOutput(const char* data, size_t len)
			{
				g_Package.readBuf.append(data, len);
				size_t newline = 0;
				while ((newline = g_Package.readBuf.find('\n')) != std::string::npos)
				{
					std::string line = g_Package.readBuf.substr(0, newline);
					g_Package.readBuf.erase(0, newline + 1);
					if (!line.empty() && line.back() == '\r') line.pop_back();
					if (line.rfind(kExitMarker, 0) == 0)
					{
						g_Package.exitCode =
							std::atoi(line.c_str() + std::strlen(kExitMarker));
						g_Package.exitCodeValid = true;
						continue;
					}
					g_Package.log += line;
					g_Package.log += '\n';
				}
				// Bound the log so a verbose cook cannot grow it forever.
				if (g_Package.log.size() > 512 * 1024)
					g_Package.log.erase(0, g_Package.log.size() - 256 * 1024);
			}

			bool SpawnSubprocess(const std::string& cmd)
			{
#if PLATFORM_WINDOWS
				g_Package.pipe = _popen(cmd.c_str(), "r");
				if (!g_Package.pipe)
				{
					FURYE << "Package: failed to spawn furye-cli (" << std::strerror(errno) << ")";
					return false;
				}
				return true;
#else
				int fds[2];
				if (pipe(fds) != 0)
				{
					FURYE << "Package: pipe failed (" << std::strerror(errno) << ")";
					return false;
				}
				const pid_t pid = fork();
				if (pid == -1)
				{
					close(fds[0]);
					close(fds[1]);
					FURYE << "Package: fork failed (" << std::strerror(errno) << ")";
					return false;
				}
				if (pid == 0)
				{
					// Child: own process group so Cancel can signal the
					// shell and furye-cli together; both outputs into the
					// pipe. Only async-signal-safe calls before exec.
					setpgid(0, 0);
					dup2(fds[1], STDOUT_FILENO);
					dup2(fds[1], STDERR_FILENO);
					close(fds[0]);
					close(fds[1]);
					execlp("sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
					_exit(127);
				}
				close(fds[1]);
				fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
				g_Package.pid = pid;
				g_Package.pipeFd = fds[0];
				return true;
#endif
			}

			void ReapSubprocess()
			{
#if PLATFORM_WINDOWS
				if (g_Package.pipe)
				{
					const int rc = _pclose(g_Package.pipe);
					g_Package.pipe = nullptr;
					if (!g_Package.exitCodeValid && rc != -1)
						g_Package.exitCode = rc;
				}
#else
				if (g_Package.pipeFd >= 0)
				{
					close(g_Package.pipeFd);
					g_Package.pipeFd = -1;
				}
				if (g_Package.pid > 0)
				{
					// The child is already dead at EOF, so this returns
					// immediately; ECHILD just means LaunchDetached's
					// SIG_IGN auto-reaped it and the marker has the code.
					int status = 0;
					while (waitpid(g_Package.pid, &status, 0) == -1 && errno == EINTR)
					{
					}
					g_Package.pid = -1;
				}
#endif
			}

			bool SubprocessRunning()
			{
#if PLATFORM_WINDOWS
				return g_Package.pipe != nullptr;
#else
				return g_Package.pipeFd >= 0;
#endif
			}

			// Nonblocking drain of the subprocess pipe; call every frame
			// while handles are open, regardless of phase, so leftover
			// output after Cancel still lands in the log and the child is
			// reaped.
			void PumpSubprocess()
			{
#if PLATFORM_WINDOWS
				if (!g_Package.pipe) return;
				HANDLE handle = (HANDLE)_get_osfhandle(_fileno(g_Package.pipe));
				DWORD avail = 0;
				if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &avail, nullptr))
				{
					// Broken pipe: the shell exited; collect its status.
					ReapSubprocess();
					return;
				}
				if (avail == 0) return;
				char buf[4096];
				const size_t want =
					avail < sizeof(buf) ? (size_t)avail : sizeof(buf);
				const size_t n = fread(buf, 1, want, g_Package.pipe);
				if (n > 0) AppendOutput(buf, n);
#else
				if (g_Package.pipeFd < 0) return;
				char buf[4096];
				for (;;)
				{
					const ssize_t n = read(g_Package.pipeFd, buf, sizeof(buf));
					if (n > 0)
					{
						AppendOutput(buf, (size_t)n);
						continue;
					}
					if (n == 0)
					{
						ReapSubprocess();
						break;
					}
					if (errno == EINTR) continue;
					if (errno == EAGAIN || errno == EWOULDBLOCK) break;
					ReapSubprocess();
					break;
				}
#endif
			}

			void KillSubprocess()
			{
#if PLATFORM_WINDOWS
				// _popen has no kill; Cancel is disabled in the UI.
#else
				if (g_Package.pid > 0 && killpg(g_Package.pid, SIGTERM) != 0)
					kill(g_Package.pid, SIGTERM);
#endif
			}

			void CloseDialog()
			{
				if (SubprocessRunning())
				{
					KillSubprocess();
					ReapSubprocess();
				}
				g_Package = PackageDialogState{};
			}

			void DrawSettingsView()
			{
				ImGui::TextUnformatted("Scene:");
				ImGui::SameLine();
				ImGui::TextDisabled("%s", g_Package.scenePath.c_str());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", g_Package.scenePath.c_str());

				ImGui::Combo("Compression", &g_Package.compression, kCompressionItems);
				ImGui::Combo("Texture target", &g_Package.textureTarget, kTextureTargetItems);

				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("Output folder");
				ImGui::SameLine();
				char folderBuf[1024];
				std::snprintf(folderBuf, sizeof(folderBuf), "%s", g_Package.outputFolder.c_str());
				ImGui::PushItemWidth(300.0f);
				ImGui::InputText("##outfolder", folderBuf, sizeof(folderBuf),
					ImGuiInputTextFlags_ReadOnly);
				ImGui::PopItemWidth();
				ImGui::SameLine();
				if (ImGui::Button("Browse..."))
				{
					nfdu8char_t* picked = nullptr;
					if (NFD_PickFolderU8(&picked, nullptr) == NFD_OKAY && picked)
					{
						g_Package.outputFolder = picked;
						NFD_FreePathU8(picked);
					}
				}

				ImGui::Checkbox("Verbose", &g_Package.verbose);

				ImGui::Separator();
				if (ImGui::Button("Package", ImVec2(120.0f, 0.0f)))
				{
					const std::string cmd = BuildCommand(&g_Package.pakPath);
					g_Package.log.clear();
					g_Package.readBuf.clear();
					g_Package.cancelled = false;
					g_Package.exitCodeValid = false;
					g_Package.exitCode = -1;
					g_Package.phase =
						SpawnSubprocess(cmd) ? Phase::Running : Phase::Done;
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
					CloseDialog();
			}

			void DrawLogChild()
			{
				ImGui::BeginChild("##pkglog", ImVec2(560.0f, 260.0f),
					ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
				ImGui::TextUnformatted(g_Package.log.c_str());
				// Pin to the bottom only while already there, so scrolling
				// up freezes the view.
				if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
					ImGui::SetScrollHereY(1.0f);
				ImGui::EndChild();
			}

			void DrawStatusLine()
			{
				ImVec4 color(0.9f, 0.4f, 0.4f, 1.0f);
				std::string text;
				if (g_Package.cancelled)
				{
					color = ImVec4(0.95f, 0.8f, 0.3f, 1.0f);
					text = "Cancelled by user";
				}
				else if (g_Package.exitCode == 0)
				{
					color = ImVec4(0.4f, 0.85f, 0.4f, 1.0f);
					text = "Package complete: " + g_Package.pakPath;
				}
				else
				{
					text = "Package failed (exit " + std::to_string(g_Package.exitCode) + ")";
				}
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				// Wrap long pak paths instead of stretching the modal.
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextUnformatted(text.c_str());
				ImGui::PopTextWrapPos();
				ImGui::PopStyleColor();
			}
		}

		namespace EditorPackageDialog
		{
			void Open(const std::string& scenePath)
			{
				if (g_Package.open) return;
				const std::filesystem::path p(scenePath);
				g_Package = PackageDialogState{};
				g_Package.scenePath = scenePath;
				g_Package.outputFolder = p.parent_path().generic_string();
				g_Package.pendingOpen = true;
			}

			bool IsOpen()
			{
				return g_Package.open;
			}

			void Draw()
			{
				if (g_Package.pendingOpen)
				{
					g_Package.pendingOpen = false;
					g_Package.open = true;
					ImGui::OpenPopup(kPopupTitle);
				}
				if (!g_Package.open) return;

				PumpSubprocess();
				if (g_Package.phase == Phase::Running && !SubprocessRunning())
					g_Package.phase = Phase::Done;

				// Fixed size: log lines and the final pak path must not grow
				// the modal (no AlwaysAutoResize).
				ImGui::SetNextWindowSize(ImVec2(660.0f, 480.0f), ImGuiCond_Always);
				if (!ImGui::BeginPopupModal(kPopupTitle, nullptr, 0))
				{
					// Not rendering: either the first frame after OpenPopup
					// (everVisible still false) or the user dismissed the
					// modal with Esc / click-outside.
					if (!g_Package.everVisible) return;
					if (g_Package.phase == Phase::Running)
					{
						// Treat like Cancel: kill the subprocess and stay open
						// in the Done state so the outcome is visible.
						KillSubprocess();
						g_Package.cancelled = true;
						g_Package.phase = Phase::Done;
						ImGui::OpenPopup(kPopupTitle);
					}
					else
					{
						CloseDialog();
					}
					return;
				}
				g_Package.everVisible = true;

				if (g_Package.phase == Phase::Settings)
				{
					DrawSettingsView();
				}
				else
				{
					DrawLogChild();

					if (g_Package.phase == Phase::Running)
					{
	#if PLATFORM_WINDOWS
						ImGui::BeginDisabled();
						ImGui::Button("Cancel", ImVec2(120.0f, 0.0f));
						ImGui::EndDisabled();
						if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
							ImGui::SetTooltip("Cancel is not available on Windows (_popen cannot kill the child)");
	#else
						if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
						{
							KillSubprocess();
							g_Package.cancelled = true;
							g_Package.phase = Phase::Done;
						}
	#endif
					}
					else
					{
						DrawStatusLine();
						if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
							CloseDialog();
					}
				}

				ImGui::EndPopup();
			}
		}
	}
}
