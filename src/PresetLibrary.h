#pragma once

#include <projectM-4/playlist.h>
#include <projectM-4/projectM.h>

#include <Poco/JSON/Object.h>
#include <Poco/Logger.h>
#include <Poco/Util/AbstractConfiguration.h>
#include <Poco/Util/Subsystem.h>

#include <memory>
#include <string>
#include <unordered_map>
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

    /** @brief Writes the current favorites to an arbitrary file, in the same format as the
     * internal favorites file. */
    void ExportFavorites(const std::string& filePath) const;

    /** @brief Reads favorites from an arbitrary file (as written by ExportFavorites) and merges
     * them into the current favorites, skipping duplicates. Paths that no longer exist locally
     * are re-linked by filename against the current preset tree, same as ReconcilePathsAfterRescan
     * does for moved presets. Posts a toast summarizing the result. */
    void ImportFavorites(const std::string& filePath);

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

    /** @brief Writes the given playlist to an arbitrary file (name + items, no internal id). */
    void ExportPlaylist(const std::string& id, const std::string& filePath) const;

    /** @brief Reads a playlist from an arbitrary file (as written by ExportPlaylist) and adds it
     * as a new playlist with a freshly generated id, named after the imported file itself
     * (its filename minus ".json", not whatever name was stored inside it at export time -
     * matches what the user sees on disk). If a playlist with that name already exists, the
     * new one is suffixed ("(Imported)", "(Imported 2)", ...). Item paths that no longer exist
     * locally are re-linked by filename, same as ImportFavorites. Posts a toast summarizing the
     * result.
     * @return The id of the newly created playlist, or an empty string if the file couldn't be
     * read. */
    std::string ImportPlaylist(const std::string& filePath);

    /** @brief What kind of file ImportPlaylist()/ImportFavorites() would find at a given path,
     * without importing anything yet - lets the UI catch e.g. a Favorites export being picked
     * in the Playlist import dialog and offer ImportFavoritesAsPlaylist() instead. */
    enum class ImportFileKind
    {
        Playlist,  //!< Has an "items" array - what ExportPlaylist()/ImportPlaylist() use.
        Favorites, //!< Has a "favorites" array - what ExportFavorites()/ImportFavorites() use.
        Invalid    //!< Neither; not readable, not valid JSON, or an unrecognized shape.
    };

    ImportFileKind DetectImportFileKind(const std::string& filePath) const;

    /** @brief Imports a Favorites-shaped file's presets as a new playlist (same naming/
     * collision/path-resolution/toast behavior as ImportPlaylist) instead of merging them into
     * Favorites - for when the user confirms converting a favorites export they picked in the
     * Playlist import dialog by mistake.
     * @return The id of the newly created playlist, or an empty string if the file couldn't be
     * read. */
    std::string ImportFavoritesAsPlaylist(const std::string& filePath);

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

    /** @brief Builds a lowercase-filename -> full-path index of every preset file in the
     * current tree. Used both by ReconcilePathsAfterRescan and by favorites/playlist import to
     * re-link paths that don't exist verbatim on this machine. */
    std::unordered_map<std::string, std::string> BuildFileNameIndex() const;

    /** @brief If `path` doesn't exist as-is, looks it up by filename in `byFileName` and
     * returns the match; otherwise returns `path` unchanged. Never returns an empty string
     * unless `path` was empty. */
    static std::string ResolveImportedPath(const std::string& path, const std::unordered_map<std::string, std::string>& byFileName);

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

    /** @brief Writes a JSON object to an arbitrary file path (2-space indented, same style as
     * the internal data files). Logs and returns false on failure. */
    bool WriteJson(const Poco::JSON::Object& root, const std::string& filePath) const;

    /** @brief Shared tail end of ImportPlaylist()/ImportFavoritesAsPlaylist(): names a new
     * playlist after `filePath`'s own filename (suffixing on a name collision), resolves
     * `paths` against the current preset tree, saves, and posts a summary toast. */
    std::string CreatePlaylistFromImportedPaths(const std::string& filePath, const std::vector<std::string>& paths);

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
