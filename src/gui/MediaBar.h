#pragma once

class AudioCapture;
class PresetLibrary;
class ProjectMGUI;
class ProjectMWrapper;

/**
 * @brief Compact, always-visible (whenever the UI is shown) transport strip docked to the
 * bottom-centre of the screen: previous, play/pause, next, shuffle, favorite.
 *
 * Every button posts the same commands as the existing keyboard shortcuts (n/p/space/y) or
 * calls PresetLibrary directly for favorites, so there is exactly one code path behind each
 * action, whether triggered by key or by button.
 */
class MediaBar
{
public:
    MediaBar() = delete;

    explicit MediaBar(ProjectMGUI& gui);

    /**
     * @brief Draws the media bar. Call every frame the UI is visible.
     */
    void Draw();

private:
    ProjectMGUI& _gui;
    ProjectMWrapper& _projectMWrapper;
    PresetLibrary& _presetLibrary;
};
