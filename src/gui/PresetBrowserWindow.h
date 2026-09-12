#pragma once

#include "PresetLibrary.h"

#include <functional>
#include <string>

class ProjectMGUI;

/**
 * @brief Window with three tabs: Browse (the on-disk preset folder tree), Favorites, and
 * Playlists. Every action here (loading a preset, favoriting, editing a playlist) goes
 * through PresetLibrary, the same subsystem the media bar and the "b" key use.
 */
class PresetBrowserWindow
{
public:
    PresetBrowserWindow() = delete;

    explicit PresetBrowserWindow(ProjectMGUI& gui);

    void Show();

    void Draw();

private:
    void DrawScopeSelector();

    void DrawBrowseTab();

    void DrawFavoritesTab();

    void DrawPlaylistsTab();

    void DrawTreeNode(const PresetTreeNode& node);

    void DrawFilteredList();

    /** @brief One row for a single preset: single click selects, double click plays (via
     * `onPlay`, since Browse vs. Favorites resolve double-click to different cycle-scope
     * behaviour), star favorites, ">>" adds to a playlist. */
    void DrawFileRow(const std::string& label, const std::string& fullPath, const std::function<void(const std::string&)>& onPlay);

    void DrawAddToPlaylistPopup();

    ProjectMGUI& _gui;
    PresetLibrary& _presetLibrary;

    bool _visible{false};

    char _filterBuffer[256]{};
    std::string _filter;

    std::string _addToPlaylistTarget;
    bool _openAddToPlaylistPopup{false}; //!< Popups must be opened from a stable ID-stack location,
                                          //!< so tree/list rows just raise this flag instead of opening directly.

    std::string _selectedPlaylistId;
    char _renameBuffer[256]{};

    std::string _selectedPath; //!< Last single- or double-clicked preset (click feedback only).
};
