#pragma once

#include <projectM-4/playlist.h>
#include <projectM-4/projectM.h>

#include <Poco/Logger.h>
#include <Poco/Util/AbstractConfiguration.h>
#include <Poco/Util/Subsystem.h>

#include <memory>
#include <string>
#include <vector>

class ProjectMWrapper;

/**
 * @brief One entry in the on-disk preset tree, mirroring the presets folder structure.
 */
struct PresetTreeNode
{
    std::string name;                             //!< Display name (file or folder name only).
    std::string fullPath;                          //!< Absolute filesystem path.
    bool isDirectory{false};                       //!< True if this is a folder.
    std::vector<PresetTreeNode> children;           //!< Subdirectories and files, folders first, both alphabetical.
};

/**
 * @brief A named, ordered, user-editable collection of presets.
 */
struct NamedPlaylist
{
    std::string id;                                //!< Stable identifier, not shown to the user.
    std::string name;                               //!< Display name.
    std::vector<std::string> items;                  //!< Absolute preset paths, in playback order.
};

/**
 * @brief Determines what a "next/previous preset" and auto-advance cycle through.
 */
enum class CycleScope
{
    Playlist,  //!< The active named playlist.
    Folder,    //!< Only the presets in the current preset's own folder.
    All,       //!< Every preset found under the configured preset path(s).
    Favorites  //!< The favorited presets, alphabetically.
};

/**
 * @brief Owns favorites, user-defined named playlists, the preset folder tree, and the
 * current cycle scope. Bridges all of these onto the existing libprojectM playlist
 * (the same one driving n/p/space/shuffle/duration/transition), so navigation, shuffle,
 * duration and transition settings keep working unchanged no matter what is being cycled.
 */
class PresetLibrary : public Poco::Util::Subsystem
{
public:
    const char* name() const override;

    void initialize(Poco::Util::Application& app) override;

    void uninitialize() override;

    // --- folder tree -----------------------------------------------------

    /** @brief Re-scans the configured preset path(s) from disk. */
    void Rescan();

    const std::vector<PresetTreeNode>& Tree() const;

    /** @brief The preset's folder, relative to its configured preset root, using native
     * path separators (e.g. "Numbers/Level2"). Empty if the preset sits directly in the
     * root of a configured preset path. */
    std::string RelativeFolder(const std::string& fullPath) const;

    // --- favorites --------------------------------------------------------

    bool IsFavorite(const std::string& path) const;

    void ToggleFavorite(const std::string& path);

    /** @brief Toggles the favorite state of the preset currently being displayed. */
    void ToggleFavoriteCurrent();

    /** @brief Favorite preset paths, sorted for display. */
    std::vector<std::string> FavoritesSorted() const;

    // --- named playlists ---------------------------------------------------

    const std::vector<NamedPlaylist>& Playlists() const;

    NamedPlaylist* FindPlaylist(const std::string& id);

    std::string CreatePlaylist(const std::string& name);

    void RenamePlaylist(const std::string& id, const std::string& name);

    void DeletePlaylist(const std::string& id);

    void AddToPlaylist(const std::string& id, const std::string& path);

    void RemoveFromPlaylist(const std::string& id, size_t index);

    /** @brief Moves an item from one index to another within the same playlist (for reordering). */
    void MoveInPlaylist(const std::string& id, size_t from, size_t to);

    std::string ActivePlaylistId() const;

    /** @brief Makes the given playlist active and switches the cycle scope to Playlist. */
    void SetActivePlaylist(const std::string& id);

    // --- cycle scope --------------------------------------------------------

    CycleScope Scope() const;

    void SetScope(CycleScope scope);

    // --- navigation -----------------------------------------------------

    /** @brief The full path of the preset the live playlist is currently positioned at,
     * i.e. what's actually on screen and what Next/Previous/auto-advance work from. */
    std::string CurrentPresetPath() const;

    /**
     * @brief Double-click handler for the Browse tab. Root-level presets always switch
     * scope to All. Subfolder presets keep Folder scope if that's already active
     * (re-centring on the clicked preset's own folder); otherwise scope becomes All.
     * Either way, the live playlist is rebuilt to match and positioned at `path`.
     */
    void PlayFromBrowse(const std::string& path);

    /**
     * @brief Double-click handler for the Favorites tab: switches scope to Favorites and
     * plays `path` from within it.
     */
    void PlayFromFavorites(const std::string& path);

    /**
     * @brief Double-click handler for the Playlists tab: makes the given playlist active,
     * switches scope to Playlist, and plays the item at `index`.
     */
    void PlayFromPlaylist(const std::string& playlistId, size_t index);

    /** @brief Rebuilds the live libprojectM playlist to match the current scope. Call after
     * the current preset's folder changes (for Folder scope) or on startup. */
    void ApplyScopeToLivePlaylist();

private:
    void ScanDirectory(const std::string& path, std::vector<PresetTreeNode>& outChildren);

    /** @brief Re-links favorites/playlist entries whose file moved to a different folder
     * since they were saved, matching by filename against the freshly scanned tree. */
    void ReconcilePathsAfterRescan();

    std::vector<std::string> GetPresetPaths() const;

    /** @brief Non-recursive listing of preset files directly inside a folder, alphabetical. */
    std::vector<std::string> FilesInFolder(const std::string& folderPath) const;

    /** @brief Every preset file under the configured preset path(s), alphabetical, folders depth-first. */
    std::vector<std::string> AllFiles() const;

    /** @brief Replaces the live playlist contents (in the given order) and jumps to the given
     * path if found (or index 0 otherwise). */
    void RebuildLivePlaylist(const std::vector<std::string>& items, const std::string& jumpToPath);

    void LoadFavorites();
    void SaveFavorites();

    void LoadPlaylists();
    void SavePlaylists();

    void OnConfigurationPropertyChanged(const Poco::Util::AbstractConfiguration::KeyValue& property);

    void OnConfigurationPropertyRemoved(const std::string& key);

    std::string DataFilePath(const std::string& suffix) const;

    static bool IsPresetFile(const std::string& fileName);

    ProjectMWrapper* _projectMWrapper{nullptr};
    Poco::AutoPtr<Poco::Util::AbstractConfiguration> _userConfig;
    Poco::AutoPtr<Poco::Util::AbstractConfiguration> _projectMConfigView;

    std::vector<PresetTreeNode> _tree;
    std::vector<std::string> _favorites;
    std::vector<NamedPlaylist> _playlists;

    Poco::Logger& _logger{Poco::Logger::get("PresetLibrary")};
};
