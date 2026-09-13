#pragma once

#include "PresetLibrary.h"

#include "gui/FileChooser.h"

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

    /** @brief Confirmation shown when the file picked in the Playlist import dialog turns out
     * to be a Favorites export instead - offers to import its presets as a new playlist. */
    void DrawConvertFavoritesToPlaylistPopup();

    /** @brief Handles a completed _importChooser pick, dispatching on Context() ("favorites" or
     * "playlist"). No-op if the user cancelled (SelectedFiles() empty). */
    void HandleImportChooserResult();

    /** @brief Handles a completed _exportChooser pick, dispatching on Context(). No-op if the
     * user cancelled. */
    void HandleExportChooserResult();

    /** @brief Path to the small file remembering _importChooser's/_exportChooser's last
     * directories across app restarts. */
    std::string DirectoryMemoryFilePath() const;

    /** @brief Points both choosers at their last-remembered directory (falls back to the
     * user's home directory if none was saved yet). Called once, from the constructor. */
    void LoadRememberedDirectories();

    /** @brief Persists both choosers' current directories. Called after either chooser closes,
     * whether the user picked a file or cancelled - like a native dialog, where it navigated to
     * is remembered either way. */
    void SaveRememberedDirectories() const;

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

    std::string _pendingFavoritesAsPlaylistFile; //!< Set when the Playlist import dialog picked a Favorites-shaped file, awaiting the convert-to-playlist confirmation.
    bool _openConvertFavoritesPopup{false}; //!< Same stable-ID-stack reasoning as _openAddToPlaylistPopup.

    FileChooser _importChooser{FileChooser::Mode::File}; //!< Picks a .json file to import (favorites or a playlist, see Context()).
    FileChooser _exportChooser{FileChooser::Mode::SaveFile}; //!< Picks a destination .json file to export to (favorites or a playlist, see Context()).
};
