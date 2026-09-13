#include "PresetLibrary.h"

#include "ProjectMSDLApplication.h"
#include "ProjectMWrapper.h"

#include "notifications/DisplayToastNotification.h"

#include <Poco/Delegate.h>
#include <Poco/NotificationCenter.h>
#include <Poco/DirectoryIterator.h>
#include <Poco/File.h>
#include <Poco/Path.h>
#include <Poco/String.h>

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <algorithm>
#include <ctime>
#include <fstream>
#include <functional>
#include <unordered_map>

namespace {

void FlattenTree(const std::vector<PresetTreeNode>& nodes, std::vector<std::string>& out)
{
    for (const auto& node : nodes)
    {
        if (node.isDirectory)
        {
            FlattenTree(node.children, out);
        }
        else
        {
            out.push_back(node.fullPath);
        }
    }
}

} // namespace

const char* PresetLibrary::name() const
{
    return "Preset Library";
}

void PresetLibrary::initialize(Poco::Util::Application& app)
{
    auto& projectMSDLApp = dynamic_cast<ProjectMSDLApplication&>(app);
    _projectMConfigView = projectMSDLApp.config().createView("projectM");
    _userConfig = projectMSDLApp.UserConfiguration();
    _projectMWrapper = &app.getSubsystem<ProjectMWrapper>();

    LoadFavorites();
    LoadPlaylists();
    Rescan();

    _userConfig->propertyChanged += Poco::delegate(this, &PresetLibrary::OnConfigurationPropertyChanged);
    _userConfig->propertyRemoved += Poco::delegate(this, &PresetLibrary::OnConfigurationPropertyRemoved);

    ApplyScopeToLivePlaylist();
}

void PresetLibrary::uninitialize()
{
    _userConfig->propertyRemoved -= Poco::delegate(this, &PresetLibrary::OnConfigurationPropertyRemoved);
    _userConfig->propertyChanged -= Poco::delegate(this, &PresetLibrary::OnConfigurationPropertyChanged);
    _projectMWrapper = nullptr;
}

// --- folder tree -----------------------------------------------------------

void PresetLibrary::Rescan()
{
    _tree.clear();
    for (const auto& root : GetPresetPaths())
    {
        Poco::File rootFile(root);
        if (!rootFile.exists() || !rootFile.isDirectory())
        {
            continue;
        }

        // Multiple configured preset roots are flattened into one combined tree,
        // matching how the live playlist already treats them as one pool.
        std::vector<PresetTreeNode> rootChildren;
        ScanDirectory(root, rootChildren);
        for (auto& child : rootChildren)
        {
            _tree.push_back(std::move(child));
        }
    }

    ReconcilePathsAfterRescan();

    poco_information_f1(_logger, "Scanned preset tree: %?d top-level entries.", static_cast<int>(_tree.size()));
}

void PresetLibrary::ReconcilePathsAfterRescan()
{
    // Re-links paths that no longer exist to a same-named file found elsewhere in the new
    // tree (i.e. the preset was moved/reorganised on disk since it was favorited or added to
    // a playlist). Also the basis for resolving imported favorites/playlists, see
    // BuildFileNameIndex()/ResolveImportedPath().
    auto byFileName = BuildFileNameIndex();

    bool favoritesChanged = false;
    for (auto& path : _favorites)
    {
        auto resolved = ResolveImportedPath(path, byFileName);
        if (resolved != path)
        {
            path = resolved;
            favoritesChanged = true;
        }
    }
    if (favoritesChanged)
    {
        SaveFavorites();
    }

    bool playlistsChanged = false;
    for (auto& playlist : _playlists)
    {
        for (auto& path : playlist.items)
        {
            auto resolved = ResolveImportedPath(path, byFileName);
            if (resolved != path)
            {
                path = resolved;
                playlistsChanged = true;
            }
        }
    }
    if (playlistsChanged)
    {
        SavePlaylists();
    }
}

std::unordered_map<std::string, std::string> PresetLibrary::BuildFileNameIndex() const
{
    // Filename -> full path, from the current scanned tree. First match wins if the
    // library happens to contain two files with the same name.
    std::unordered_map<std::string, std::string> byFileName;
    std::function<void(const std::vector<PresetTreeNode>&)> walk = [&](const std::vector<PresetTreeNode>& nodes) {
        for (const auto& node : nodes)
        {
            if (node.isDirectory)
            {
                walk(node.children);
            }
            else
            {
                byFileName.emplace(Poco::toLower(node.name), node.fullPath);
            }
        }
    };
    walk(_tree);
    return byFileName;
}

std::string PresetLibrary::ResolveImportedPath(const std::string& path, const std::unordered_map<std::string, std::string>& byFileName)
{
    if (path.empty() || Poco::File(path).exists())
    {
        return path;
    }
    auto it = byFileName.find(Poco::toLower(Poco::Path(path).getFileName()));
    return it != byFileName.end() ? it->second : path;
}

const std::vector<PresetTreeNode>& PresetLibrary::Tree() const
{
    return _tree;
}

std::string PresetLibrary::RelativeFolder(const std::string& fullPath) const
{
    auto parentDir = Poco::Path(fullPath).parent();
    parentDir.makeDirectory();
    auto parentStr = parentDir.toString();

    for (const auto& root : GetPresetPaths())
    {
        auto rootPath = Poco::Path(root);
        rootPath.makeDirectory();
        auto rootStr = rootPath.toString();

        if (parentStr.size() >= rootStr.size() && Poco::icompare(parentStr.substr(0, rootStr.size()), rootStr) == 0)
        {
            auto relative = parentStr.substr(rootStr.size());
            while (!relative.empty() && (relative.back() == '\\' || relative.back() == '/'))
            {
                relative.pop_back();
            }
            return relative;
        }
    }
    return "";
}

void PresetLibrary::ScanDirectory(const std::string& path, std::vector<PresetTreeNode>& outChildren)
{
    std::vector<PresetTreeNode> dirs;
    std::vector<PresetTreeNode> files;

    try
    {
        Poco::DirectoryIterator it(path);
        Poco::DirectoryIterator end;
        for (; it != end; ++it)
        {
            std::string entryName = it.name();
            if (entryName.empty() || entryName[0] == '.')
            {
                continue;
            }

            if (it->isDirectory())
            {
                PresetTreeNode node;
                node.name = entryName;
                node.fullPath = it->path();
                node.isDirectory = true;
                ScanDirectory(node.fullPath, node.children);
                if (!node.children.empty())
                {
                    // Prune folders that contain no presets, directly or nested.
                    dirs.push_back(std::move(node));
                }
            }
            else if (it->isFile() && IsPresetFile(entryName))
            {
                PresetTreeNode node;
                node.name = entryName;
                node.fullPath = it->path();
                node.isDirectory = false;
                files.push_back(std::move(node));
            }
        }
    }
    catch (Poco::Exception& ex)
    {
        poco_warning_f2(_logger, "Could not scan preset directory \"%s\": %s", path, ex.displayText());
        return;
    }

    auto byName = [](const PresetTreeNode& a, const PresetTreeNode& b) {
        return Poco::icompare(a.name, b.name) < 0;
    };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);

    outChildren.reserve(dirs.size() + files.size());
    for (auto& d : dirs)
    {
        outChildren.push_back(std::move(d));
    }
    for (auto& f : files)
    {
        outChildren.push_back(std::move(f));
    }
}

std::vector<std::string> PresetLibrary::GetPresetPaths() const
{
    std::vector<std::string> pathList;
    auto defaultPath = _projectMConfigView->getString("presetPath", Poco::Util::Application::instance().config().getString("application.dir", ""));
    if (!defaultPath.empty())
    {
        pathList.push_back(defaultPath);
    }

    Poco::Util::AbstractConfiguration::Keys subKeys;
    _projectMConfigView->keys("presetPath", subKeys);
    for (const auto& key : subKeys)
    {
        auto path = _projectMConfigView->getString("presetPath." + key, "");
        if (!path.empty())
        {
            pathList.push_back(std::move(path));
        }
    }
    return pathList;
}

std::vector<std::string> PresetLibrary::FilesInFolder(const std::string& folderPath) const
{
    std::vector<std::string> result;
    try
    {
        Poco::DirectoryIterator it(folderPath);
        Poco::DirectoryIterator end;
        for (; it != end; ++it)
        {
            if (it->isFile() && IsPresetFile(it.name()))
            {
                result.push_back(it->path());
            }
        }
    }
    catch (Poco::Exception&)
    {
        // Folder may have vanished since the tree was scanned; return what we have.
    }

    std::sort(result.begin(), result.end(), [](const std::string& a, const std::string& b) {
        return Poco::icompare(Poco::Path(a).getFileName(), Poco::Path(b).getFileName()) < 0;
    });
    return result;
}

std::vector<std::string> PresetLibrary::AllFiles() const
{
    std::vector<std::string> result;
    FlattenTree(_tree, result);
    return result;
}

bool PresetLibrary::IsPresetFile(const std::string& fileName)
{
    auto ext = Poco::Path(fileName).getExtension();
    return Poco::icompare(ext, "milk") == 0 || Poco::icompare(ext, "milk2") == 0 || Poco::icompare(ext, "prjm") == 0;
}

// --- favorites ---------------------------------------------------------------

bool PresetLibrary::IsFavorite(const std::string& path) const
{
    return std::find(_favorites.begin(), _favorites.end(), path) != _favorites.end();
}

void PresetLibrary::ToggleFavorite(const std::string& path)
{
    auto it = std::find(_favorites.begin(), _favorites.end(), path);
    if (it != _favorites.end())
    {
        _favorites.erase(it);
    }
    else
    {
        _favorites.push_back(path);
    }
    SaveFavorites();
}

void PresetLibrary::ToggleFavoriteCurrent()
{
    auto path = CurrentPresetPath();
    if (path.empty())
    {
        return;
    }

    ToggleFavorite(path);

    std::string toastText = (IsFavorite(path) ? "Favorited: " : "Unfavorited: ") + Poco::Path(path).getFileName();
    Poco::NotificationCenter::defaultCenter().postNotification(new DisplayToastNotification(std::move(toastText)));
}

std::vector<std::string> PresetLibrary::FavoritesSorted() const
{
    auto result = _favorites;
    std::sort(result.begin(), result.end(), [](const std::string& a, const std::string& b) {
        return Poco::icompare(a, b) < 0;
    });
    return result;
}

// --- named playlists -----------------------------------------------------------

const std::vector<NamedPlaylist>& PresetLibrary::Playlists() const
{
    return _playlists;
}

NamedPlaylist* PresetLibrary::FindPlaylist(const std::string& id)
{
    for (auto& playlist : _playlists)
    {
        if (playlist.id == id)
        {
            return &playlist;
        }
    }
    return nullptr;
}

std::string PresetLibrary::CreatePlaylist(const std::string& name)
{
    static int counter = 0;
    std::string id = "pl_" + std::to_string(static_cast<long long>(std::time(nullptr))) + "_" + std::to_string(++counter);

    NamedPlaylist playlist;
    playlist.id = id;
    playlist.name = name.empty() ? ("Playlist " + std::to_string(_playlists.size() + 1)) : name;
    _playlists.push_back(std::move(playlist));
    SavePlaylists();
    return id;
}

void PresetLibrary::RenamePlaylist(const std::string& id, const std::string& name)
{
    if (auto* playlist = FindPlaylist(id))
    {
        playlist->name = name;
        SavePlaylists();
    }
}

void PresetLibrary::DeletePlaylist(const std::string& id)
{
    _playlists.erase(std::remove_if(_playlists.begin(), _playlists.end(),
                                    [&](const NamedPlaylist& playlist) { return playlist.id == id; }),
                     _playlists.end());

    if (ActivePlaylistId() == id)
    {
        _userConfig->setString("projectM.activePlaylistId", "");
    }
    SavePlaylists();
}

void PresetLibrary::AddToPlaylist(const std::string& id, const std::string& path)
{
    if (auto* playlist = FindPlaylist(id))
    {
        playlist->items.push_back(path);
        SavePlaylists();
    }
}

void PresetLibrary::RemoveFromPlaylist(const std::string& id, size_t index)
{
    if (auto* playlist = FindPlaylist(id))
    {
        if (index < playlist->items.size())
        {
            playlist->items.erase(playlist->items.begin() + static_cast<long>(index));
            SavePlaylists();
        }
    }
}

void PresetLibrary::MoveInPlaylist(const std::string& id, size_t from, size_t to)
{
    if (auto* playlist = FindPlaylist(id))
    {
        if (from < playlist->items.size() && to < playlist->items.size() && from != to)
        {
            auto item = playlist->items[from];
            playlist->items.erase(playlist->items.begin() + static_cast<long>(from));
            playlist->items.insert(playlist->items.begin() + static_cast<long>(to), item);
            SavePlaylists();
        }
    }
}

std::string PresetLibrary::ActivePlaylistId() const
{
    return _projectMConfigView->getString("activePlaylistId", "");
}

void PresetLibrary::SetActivePlaylist(const std::string& id)
{
    _userConfig->setString("projectM.activePlaylistId", id);
    _userConfig->setString("projectM.cycleScope", "playlist");
}

// --- cycle scope -----------------------------------------------------------------

CycleScope PresetLibrary::Scope() const
{
    auto scope = _projectMConfigView->getString("cycleScope", "all");
    if (scope == "folder")
    {
        return CycleScope::Folder;
    }
    if (scope == "playlist")
    {
        return CycleScope::Playlist;
    }
    if (scope == "favorites")
    {
        return CycleScope::Favorites;
    }
    return CycleScope::All;
}

void PresetLibrary::SetScope(CycleScope scope)
{
    std::string value = "all";
    if (scope == CycleScope::Folder)
    {
        value = "folder";
    }
    else if (scope == CycleScope::Playlist)
    {
        value = "playlist";
    }
    else if (scope == CycleScope::Favorites)
    {
        value = "favorites";
    }
    _userConfig->setString("projectM.cycleScope", value);
}

// --- navigation ------------------------------------------------------------------

std::string PresetLibrary::CurrentPresetPath() const
{
    if (!_projectMWrapper || !_projectMWrapper->Playlist())
    {
        return "";
    }

    auto playlist = _projectMWrapper->Playlist();
    auto size = projectm_playlist_size(playlist);
    if (size == 0)
    {
        return "";
    }

    auto position = projectm_playlist_get_position(playlist);
    if (position >= size)
    {
        return "";
    }

    auto* item = projectm_playlist_item(playlist, position);
    std::string path = item ? item : "";
    projectm_playlist_free_string(item);
    return path;
}

void PresetLibrary::PlayFromBrowse(const std::string& path)
{
    // projectM's own "preset duration elapsed" timer autonomously asks the (permanently
    // connected) live playlist for "the next preset" and reloads from ITS tracked
    // position -- entirely independent of anything we do here. So playing a preset must
    // always go through RebuildLivePlaylist()/the live playlist's position, never a
    // direct projectm_load_preset_file() bypass, or the very next autonomous switch will
    // silently revert to wherever the playlist's stale position was.
    bool inSubfolder = !RelativeFolder(path).empty();

    if (inSubfolder && Scope() == CycleScope::Folder)
    {
        // Stay in Folder scope, re-centred on this preset's own folder.
        RebuildLivePlaylist(FilesInFolder(Poco::Path(path).parent().toString()), path);
        return;
    }

    SetScope(CycleScope::All);
    RebuildLivePlaylist(AllFiles(), path);
}

void PresetLibrary::PlayFromFavorites(const std::string& path)
{
    SetScope(CycleScope::Favorites);
    RebuildLivePlaylist(FavoritesSorted(), path);
}

void PresetLibrary::PlayFromPlaylist(const std::string& playlistId, size_t index)
{
    auto* playlist = FindPlaylist(playlistId);
    if (!playlist || index >= playlist->items.size())
    {
        return;
    }

    _userConfig->setString("projectM.activePlaylistId", playlistId);
    _userConfig->setString("projectM.cycleScope", "playlist");
    RebuildLivePlaylist(playlist->items, playlist->items[index]);
}

void PresetLibrary::ApplyScopeToLivePlaylist()
{
    if (!_projectMWrapper || !_projectMWrapper->Playlist())
    {
        return;
    }

    auto keep = CurrentPresetPath();

    switch (Scope())
    {
        case CycleScope::Folder:
        {
            auto files = keep.empty() ? AllFiles() : FilesInFolder(Poco::Path(keep).parent().toString());
            RebuildLivePlaylist(files, keep);
            break;
        }

        case CycleScope::All:
        {
            RebuildLivePlaylist(AllFiles(), keep);
            break;
        }

        case CycleScope::Favorites:
        {
            auto favorites = FavoritesSorted();
            if (!favorites.empty())
            {
                RebuildLivePlaylist(favorites, keep);
            }
            // No favorites yet: leave the live playlist as-is.
            break;
        }

        case CycleScope::Playlist:
        default:
        {
            auto* playlist = FindPlaylist(ActivePlaylistId());
            if (playlist && !playlist->items.empty())
            {
                RebuildLivePlaylist(playlist->items, keep);
            }
            // No active/empty playlist: leave the live playlist as-is rather than
            // stranding the user with nothing to cycle through.
            break;
        }
    }
}

void PresetLibrary::RebuildLivePlaylist(const std::vector<std::string>& items, const std::string& jumpToPath)
{
    if (!_projectMWrapper || !_projectMWrapper->Playlist())
    {
        return;
    }

    auto playlist = _projectMWrapper->Playlist();

    projectm_playlist_clear(playlist);
    for (const auto& item : items)
    {
        projectm_playlist_add_preset(playlist, item.c_str(), false);
    }

    auto size = projectm_playlist_size(playlist);
    uint32_t targetIndex = 0;
    if (!jumpToPath.empty())
    {
        for (uint32_t i = 0; i < size; ++i)
        {
            auto* item = projectm_playlist_item(playlist, i);
            bool match = jumpToPath == item;
            projectm_playlist_free_string(item);
            if (match)
            {
                targetIndex = i;
                break;
            }
        }
    }

    if (size > 0)
    {
        projectm_playlist_set_position(playlist, targetIndex, false);
    }
}

// --- persistence -------------------------------------------------------------------

std::string PresetLibrary::DataFilePath(const std::string& suffix) const
{
    Poco::Path dir = Poco::Path::configHome();
    dir.makeDirectory().append("projectM/");
    Poco::File(dir).createDirectories();

    auto baseName = Poco::Util::Application::instance().config().getString("application.baseName", "projectMSDL");
    dir.setFileName(baseName + suffix);
    return dir.toString();
}

bool PresetLibrary::WriteJson(const Poco::JSON::Object& root, const std::string& filePath) const
{
    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        poco_warning_f1(_logger, "Could not write file: %s", filePath);
        return false;
    }
    root.stringify(out, 2);
    return true;
}

void PresetLibrary::LoadFavorites()
{
    _favorites.clear();

    std::ifstream in(DataFilePath(".favorites.json"), std::ios::binary);
    if (!in)
    {
        return;
    }

    try
    {
        Poco::JSON::Parser parser;
        auto result = parser.parse(in);
        auto root = result.extract<Poco::JSON::Object::Ptr>();
        auto array = root->getArray("favorites");
        if (array)
        {
            for (size_t i = 0; i < array->size(); ++i)
            {
                _favorites.push_back(array->getElement<std::string>(static_cast<unsigned int>(i)));
            }
        }
    }
    catch (Poco::Exception& ex)
    {
        poco_warning_f1(_logger, "Could not read favorites file: %s", ex.displayText());
    }
}

void PresetLibrary::SaveFavorites()
{
    Poco::JSON::Array array;
    for (const auto& path : _favorites)
    {
        array.add(path);
    }

    Poco::JSON::Object root;
    root.set("favorites", array);

    WriteJson(root, DataFilePath(".favorites.json"));
}

void PresetLibrary::ExportFavorites(const std::string& filePath) const
{
    Poco::JSON::Array array;
    for (const auto& path : _favorites)
    {
        array.add(path);
    }

    Poco::JSON::Object root;
    root.set("favorites", array);

    if (WriteJson(root, filePath))
    {
        std::string toastText = "Exported " + std::to_string(_favorites.size()) + " favorite" +
            (_favorites.size() == 1 ? "" : "s") + " to " + Poco::Path(filePath).getFileName();
        Poco::NotificationCenter::defaultCenter().postNotification(new DisplayToastNotification(std::move(toastText)));
    }
}

void PresetLibrary::ImportFavorites(const std::string& filePath)
{
    std::ifstream in(filePath, std::ios::binary);
    if (!in)
    {
        Poco::NotificationCenter::defaultCenter().postNotification(
            new DisplayToastNotification("Could not open file: " + Poco::Path(filePath).getFileName()));
        return;
    }

    std::vector<std::string> importedPaths;
    try
    {
        Poco::JSON::Parser parser;
        auto result = parser.parse(in);
        auto root = result.extract<Poco::JSON::Object::Ptr>();
        auto array = root->getArray("favorites");
        if (array)
        {
            for (size_t i = 0; i < array->size(); ++i)
            {
                importedPaths.push_back(array->getElement<std::string>(static_cast<unsigned int>(i)));
            }
        }
    }
    catch (Poco::Exception& ex)
    {
        poco_warning_f1(_logger, "Could not read favorites import file: %s", ex.displayText());
        Poco::NotificationCenter::defaultCenter().postNotification(
            new DisplayToastNotification("Invalid favorites file: " + Poco::Path(filePath).getFileName()));
        return;
    }

    auto byFileName = BuildFileNameIndex();

    int added = 0;
    int missing = 0;
    for (const auto& importedPath : importedPaths)
    {
        auto resolved = ResolveImportedPath(importedPath, byFileName);
        if (std::find(_favorites.begin(), _favorites.end(), resolved) != _favorites.end())
        {
            continue;
        }

        _favorites.push_back(resolved);
        added++;
        if (!Poco::File(resolved).exists())
        {
            missing++;
        }
    }

    if (added > 0)
    {
        SaveFavorites();
    }

    std::string toastText = "Imported " + std::to_string(added) + " favorite" + (added == 1 ? "" : "s");
    if (missing > 0)
    {
        toastText += " (" + std::to_string(missing) + " not found locally)";
    }
    Poco::NotificationCenter::defaultCenter().postNotification(new DisplayToastNotification(std::move(toastText)));
}

void PresetLibrary::LoadPlaylists()
{
    _playlists.clear();

    std::ifstream in(DataFilePath(".playlists.json"), std::ios::binary);
    if (!in)
    {
        return;
    }

    try
    {
        Poco::JSON::Parser parser;
        auto result = parser.parse(in);
        auto root = result.extract<Poco::JSON::Object::Ptr>();
        auto array = root->getArray("playlists");
        if (!array)
        {
            return;
        }

        for (size_t i = 0; i < array->size(); ++i)
        {
            auto obj = array->getObject(static_cast<unsigned int>(i));
            if (!obj)
            {
                continue;
            }

            NamedPlaylist playlist;
            playlist.id = obj->optValue<std::string>("id", "");
            playlist.name = obj->optValue<std::string>("name", "Playlist");
            auto itemsArray = obj->getArray("items");
            if (itemsArray)
            {
                for (size_t j = 0; j < itemsArray->size(); ++j)
                {
                    playlist.items.push_back(itemsArray->getElement<std::string>(static_cast<unsigned int>(j)));
                }
            }
            if (!playlist.id.empty())
            {
                _playlists.push_back(std::move(playlist));
            }
        }
    }
    catch (Poco::Exception& ex)
    {
        poco_warning_f1(_logger, "Could not read playlists file: %s", ex.displayText());
    }
}

void PresetLibrary::SavePlaylists()
{
    Poco::JSON::Array array;
    for (const auto& playlist : _playlists)
    {
        Poco::JSON::Object obj;
        obj.set("id", playlist.id);
        obj.set("name", playlist.name);

        Poco::JSON::Array items;
        for (const auto& item : playlist.items)
        {
            items.add(item);
        }
        obj.set("items", items);

        array.add(obj);
    }

    Poco::JSON::Object root;
    root.set("playlists", array);

    WriteJson(root, DataFilePath(".playlists.json"));
}

void PresetLibrary::ExportPlaylist(const std::string& id, const std::string& filePath) const
{
    auto it = std::find_if(_playlists.begin(), _playlists.end(),
                           [&](const NamedPlaylist& playlist) { return playlist.id == id; });
    if (it == _playlists.end())
    {
        return;
    }

    Poco::JSON::Array items;
    for (const auto& item : it->items)
    {
        items.add(item);
    }

    Poco::JSON::Object root;
    root.set("name", it->name);
    root.set("items", items);

    if (WriteJson(root, filePath))
    {
        std::string toastText = "Exported playlist \"" + it->name + "\" (" +
            std::to_string(it->items.size()) + " presets)";
        Poco::NotificationCenter::defaultCenter().postNotification(new DisplayToastNotification(std::move(toastText)));
    }
}

std::string PresetLibrary::ImportPlaylist(const std::string& filePath)
{
    std::ifstream in(filePath, std::ios::binary);
    if (!in)
    {
        Poco::NotificationCenter::defaultCenter().postNotification(
            new DisplayToastNotification("Could not open file: " + Poco::Path(filePath).getFileName()));
        return "";
    }

    std::vector<std::string> items;
    try
    {
        Poco::JSON::Parser parser;
        auto result = parser.parse(in);
        auto root = result.extract<Poco::JSON::Object::Ptr>();
        auto itemsArray = root->getArray("items");
        if (!itemsArray)
        {
            // Valid JSON, but not the shape ExportPlaylist() writes (e.g. a Favorites export,
            // or something unrelated) - DetectImportFileKind() is how the UI catches the
            // Favorites case ahead of time and offers ImportFavoritesAsPlaylist() instead.
            Poco::NotificationCenter::defaultCenter().postNotification(
                new DisplayToastNotification("Invalid playlist file: " + Poco::Path(filePath).getFileName()));
            return "";
        }
        for (size_t i = 0; i < itemsArray->size(); ++i)
        {
            items.push_back(itemsArray->getElement<std::string>(static_cast<unsigned int>(i)));
        }
    }
    catch (Poco::Exception& ex)
    {
        poco_warning_f1(_logger, "Could not read playlist import file: %s", ex.displayText());
        Poco::NotificationCenter::defaultCenter().postNotification(
            new DisplayToastNotification("Invalid playlist file: " + Poco::Path(filePath).getFileName()));
        return "";
    }

    return CreatePlaylistFromImportedPaths(filePath, items);
}

PresetLibrary::ImportFileKind PresetLibrary::DetectImportFileKind(const std::string& filePath) const
{
    std::ifstream in(filePath, std::ios::binary);
    if (!in)
    {
        return ImportFileKind::Invalid;
    }

    try
    {
        Poco::JSON::Parser parser;
        auto result = parser.parse(in);
        auto root = result.extract<Poco::JSON::Object::Ptr>();
        if (root->has("items"))
        {
            return ImportFileKind::Playlist;
        }
        if (root->has("favorites"))
        {
            return ImportFileKind::Favorites;
        }
    }
    catch (Poco::Exception&)
    {
    }
    return ImportFileKind::Invalid;
}

std::string PresetLibrary::ImportFavoritesAsPlaylist(const std::string& filePath)
{
    std::ifstream in(filePath, std::ios::binary);
    if (!in)
    {
        Poco::NotificationCenter::defaultCenter().postNotification(
            new DisplayToastNotification("Could not open file: " + Poco::Path(filePath).getFileName()));
        return "";
    }

    std::vector<std::string> paths;
    try
    {
        Poco::JSON::Parser parser;
        auto result = parser.parse(in);
        auto root = result.extract<Poco::JSON::Object::Ptr>();
        auto array = root->getArray("favorites");
        if (array)
        {
            for (size_t i = 0; i < array->size(); ++i)
            {
                paths.push_back(array->getElement<std::string>(static_cast<unsigned int>(i)));
            }
        }
    }
    catch (Poco::Exception& ex)
    {
        poco_warning_f1(_logger, "Could not read favorites file: %s", ex.displayText());
        Poco::NotificationCenter::defaultCenter().postNotification(
            new DisplayToastNotification("Invalid favorites file: " + Poco::Path(filePath).getFileName()));
        return "";
    }

    return CreatePlaylistFromImportedPaths(filePath, paths);
}

std::string PresetLibrary::CreatePlaylistFromImportedPaths(const std::string& filePath, const std::vector<std::string>& paths)
{
    // Named after the imported file itself (not whatever name might be stored inside it at
    // export time, which the user found confusing when they didn't match) - e.g.
    // "My Set.json" -> "My Set".
    std::string name = Poco::Path(filePath).getBaseName();

    // Avoid an ambiguous duplicate name: name, then "name (Imported)", "name (Imported 2)", ...
    std::string uniqueName = name;
    int attempt = 1;
    while (std::any_of(_playlists.begin(), _playlists.end(),
                       [&](const NamedPlaylist& playlist) { return playlist.name == uniqueName; }))
    {
        attempt++;
        uniqueName = name + " (Imported" + (attempt == 2 ? "" : (" " + std::to_string(attempt - 1))) + ")";
    }

    std::string id = CreatePlaylist(uniqueName);

    auto byFileName = BuildFileNameIndex();
    int missing = 0;
    std::vector<std::string> resolvedItems;
    resolvedItems.reserve(paths.size());
    for (const auto& path : paths)
    {
        auto resolved = ResolveImportedPath(path, byFileName);
        resolvedItems.push_back(resolved);
        if (!Poco::File(resolved).exists())
        {
            missing++;
        }
    }

    if (auto* playlist = FindPlaylist(id))
    {
        playlist->items = std::move(resolvedItems);
    }
    SavePlaylists();

    std::string toastText = "Imported playlist \"" + uniqueName + "\" (" + std::to_string(paths.size()) + " presets";
    if (missing > 0)
    {
        toastText += ", " + std::to_string(missing) + " not found locally";
    }
    toastText += ")";
    Poco::NotificationCenter::defaultCenter().postNotification(new DisplayToastNotification(std::move(toastText)));

    return id;
}

// --- configuration events -----------------------------------------------------------

void PresetLibrary::OnConfigurationPropertyChanged(const Poco::Util::AbstractConfiguration::KeyValue& property)
{
    OnConfigurationPropertyRemoved(property.key());
}

void PresetLibrary::OnConfigurationPropertyRemoved(const std::string& key)
{
    if (key == "projectM.cycleScope" || key == "projectM.activePlaylistId")
    {
        ApplyScopeToLivePlaylist();
    }
}
