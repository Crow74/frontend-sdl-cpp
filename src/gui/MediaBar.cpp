#include "MediaBar.h"

#include "PresetLibrary.h"
#include "ProjectMWrapper.h"

#include "gui/ProjectMGUI.h"

#include "notifications/PlaybackControlNotification.h"

#include "imgui.h"

#include <Poco/NotificationCenter.h>
#include <Poco/Util/Application.h>

MediaBar::MediaBar(ProjectMGUI& gui)
    : _gui(gui)
    , _projectMWrapper(Poco::Util::Application::instance().getSubsystem<ProjectMWrapper>())
    , _presetLibrary(Poco::Util::Application::instance().getSubsystem<PresetLibrary>())
{
}

void MediaBar::Draw()
{
    auto& notificationCenter = Poco::NotificationCenter::defaultCenter();
    auto& io = ImGui::GetIO();

    bool locked = projectm_get_preset_locked(_projectMWrapper.ProjectM());
    bool shuffleEnabled = Poco::Util::Application::instance().config().getBool("projectM.shuffleEnabled", true);
    auto currentPreset = _presetLibrary.CurrentPresetPath();
    bool favorite = !currentPreset.empty() && _presetLibrary.IsFavorite(currentPreset);

    constexpr float margin = 24.0f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - margin), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.75f);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::Begin("##MediaBar", nullptr, flags))
    {
        if (ImGui::Button("|< Prev"))
        {
            notificationCenter.postNotification(new PlaybackControlNotification(PlaybackControlNotification::Action::PreviousPreset));
        }

        ImGui::SameLine();
        if (ImGui::Button(locked ? "> Play" : "|| Pause"))
        {
            notificationCenter.postNotification(new PlaybackControlNotification(PlaybackControlNotification::Action::TogglePresetLocked));
        }

        ImGui::SameLine();
        if (ImGui::Button("Next >|"))
        {
            notificationCenter.postNotification(new PlaybackControlNotification(PlaybackControlNotification::Action::NextPreset));
        }

        ImGui::SameLine();
        if (ImGui::Button("Random"))
        {
            notificationCenter.postNotification(new PlaybackControlNotification(PlaybackControlNotification::Action::RandomPreset));
        }

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(12.0f, 0.0f));
        ImGui::SameLine();

        if (shuffleEnabled)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button("Shuffle"))
        {
            notificationCenter.postNotification(new PlaybackControlNotification(PlaybackControlNotification::Action::ToggleShuffle));
        }
        if (shuffleEnabled)
        {
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();

        if (favorite)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        ImGui::BeginDisabled(currentPreset.empty());
        if (ImGui::Button(favorite ? "* Favorited" : "Favorite"))
        {
            _presetLibrary.ToggleFavoriteCurrent();
        }
        ImGui::EndDisabled();
        if (favorite)
        {
            ImGui::PopStyleColor();
        }
    }
    ImGui::End();
}
