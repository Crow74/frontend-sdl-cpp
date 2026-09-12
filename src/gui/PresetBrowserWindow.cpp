#include "PresetBrowserWindow.h"

#include "gui/ProjectMGUI.h"

#include "imgui.h"

#include <Poco/Path.h>
#include <Poco/String.h>
#include <Poco/Util/Application.h>

#include <algorithm>
#include <cstring>
#include <functional>

namespace {

/** @brief "Numbers/Level2/357.milk" style label: the preset's full relative folder path
 * (forward-slashed for display) plus its filename. No folder prefix if the preset sits
 * directly in a configured preset root. */
std::string PathLabel(const PresetLibrary& library, const std::string& fullPath)
{
    auto relative = library.RelativeFolder(fullPath);
    std::replace(relative.begin(), relative.end(), '\\', '/');
    auto name = Poco::Path(fullPath).getFileName();
    return relative.empty() ? name : (relative + "/" + name);
}

constexpr ImVec4 kPlayingBg{0.10f, 0.33f, 0.14f, 0.80f};
constexpr ImVec4 kPlayingBgHovered{0.14f, 0.42f, 0.18f, 0.90f};
constexpr ImVec4 kPlayingBgActive{0.10f, 0.33f, 0.14f, 1.00f};

/** @brief While in scope, paints Selectable's "selected" background dark green instead
 * of the default theme color -- used to make the currently-displayed preset stand out
 * from a merely clicked/selected row. */
struct NowPlayingStyleGuard
{
    bool active;

    explicit NowPlayingStyleGuard(bool isPlaying)
        : active(isPlaying)
    {
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Header, kPlayingBg);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kPlayingBgHovered);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, kPlayingBgActive);
        }
    }

    ~NowPlayingStyleGuard()
    {
        if (active)
        {
            ImGui::PopStyleColor(3);
        }
    }
};

} // namespace

PresetBrowserWindow::PresetBrowserWindow(ProjectMGUI& gui)
    : _gui(gui)
    , _presetLibrary(Poco::Util::Application::instance().getSubsystem<PresetLibrary>())
{
}

void PresetBrowserWindow::Show()
{
    _visible = true;
}

void PresetBrowserWindow::Draw()
{
    if (!_visible)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Preset Browser", &_visible))
    {
        DrawScopeSelector();
        ImGui::Separator();

        if (ImGui::BeginTabBar("PresetBrowserTabs"))
        {
            if (ImGui::BeginTabItem("Browse"))
            {
                DrawBrowseTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Favorites"))
            {
                DrawFavoritesTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Playlists"))
            {
                DrawPlaylistsTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    // Popups must be opened from a stable ID-stack location (not from inside nested
    // tree/tab widgets), so this runs unconditionally, once per frame.
    if (_openAddToPlaylistPopup)
    {
        ImGui::OpenPopup("AddToPlaylistPopup");
        _openAddToPlaylistPopup = false;
    }
    DrawAddToPlaylistPopup();
}

void PresetBrowserWindow::DrawScopeSelector()
{
    ImGui::TextUnformatted("Cycle through:");
    ImGui::SameLine();

    auto scope = _presetLibrary.Scope();

    bool isPlaylist = scope == CycleScope::Playlist;
    if (ImGui::RadioButton("Playlist", isPlaylist))
    {
        _presetLibrary.SetScope(CycleScope::Playlist);
    }
    ImGui::SameLine();

    bool isFolder = scope == CycleScope::Folder;
    if (ImGui::RadioButton("Folder", isFolder))
    {
        _presetLibrary.SetScope(CycleScope::Folder);
    }
    ImGui::SameLine();

    bool isAll = scope == CycleScope::All;
    if (ImGui::RadioButton("All", isAll))
    {
        _presetLibrary.SetScope(CycleScope::All);
    }
    ImGui::SameLine();

    bool isFavorites = scope == CycleScope::Favorites;
    if (ImGui::RadioButton("Favorites", isFavorites))
    {
        _presetLibrary.SetScope(CycleScope::Favorites);
    }

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12.0f, 0.0f));
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
    {
        _presetLibrary.Rescan();
        _presetLibrary.ApplyScopeToLivePlaylist();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Re-scan the preset folder for added/removed/moved files.");
    }
}

void PresetBrowserWindow::DrawBrowseTab()
{
    if (ImGui::InputTextWithHint("##filter", "Filter presets...", _filterBuffer, sizeof(_filterBuffer)))
    {
        _filter = Poco::toLower(std::string(_filterBuffer));
    }

    ImGui::BeginChild("##BrowseTree", ImVec2(0, 0), true);
    if (_filter.empty())
    {
        for (const auto& node : _presetLibrary.Tree())
        {
            DrawTreeNode(node);
        }
        if (_presetLibrary.Tree().empty())
        {
            ImGui::TextDisabled("No presets found. Check the Preset Path in Settings.");
        }
    }
    else
    {
        DrawFilteredList();
    }
    ImGui::EndChild();
}

void PresetBrowserWindow::DrawTreeNode(const PresetTreeNode& node)
{
    if (node.isDirectory)
    {
        constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        bool open = ImGui::TreeNodeEx(node.fullPath.c_str(), flags, "%s", node.name.c_str());
        if (open)
        {
            for (const auto& child : node.children)
            {
                DrawTreeNode(child);
            }
            ImGui::TreePop();
        }
    }
    else
    {
        DrawFileRow(node.name, node.fullPath, [this](const std::string& p) { _presetLibrary.PlayFromBrowse(p); });
    }
}

void PresetBrowserWindow::DrawFilteredList()
{
    std::vector<const PresetTreeNode*> matches;

    std::function<void(const std::vector<PresetTreeNode>&)> walk = [&](const std::vector<PresetTreeNode>& nodes) {
        for (const auto& node : nodes)
        {
            if (node.isDirectory)
            {
                walk(node.children);
            }
            else if (Poco::toLower(node.fullPath).find(_filter) != std::string::npos)
            {
                matches.push_back(&node);
            }
        }
    };
    walk(_presetLibrary.Tree());

    if (matches.empty())
    {
        ImGui::TextDisabled("No presets match.");
        return;
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(matches.size()));
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            const auto* node = matches[static_cast<size_t>(i)];
            DrawFileRow(PathLabel(_presetLibrary, node->fullPath), node->fullPath, [this](const std::string& p) { _presetLibrary.PlayFromBrowse(p); });
        }
    }
}

void PresetBrowserWindow::DrawFavoritesTab()
{
    auto favorites = _presetLibrary.FavoritesSorted();

    if (favorites.empty())
    {
        ImGui::TextDisabled("No favorites yet. Press \"b\" while a preset is playing, or use the star button.");
        return;
    }

    ImGui::BeginChild("##FavoritesList", ImVec2(0, 0), true);
    for (const auto& path : favorites)
    {
        DrawFileRow(PathLabel(_presetLibrary, path), path, [this](const std::string& p) { _presetLibrary.PlayFromFavorites(p); });
    }
    ImGui::EndChild();
}

void PresetBrowserWindow::DrawPlaylistsTab()
{
    const auto& playlists = _presetLibrary.Playlists();

    if (ImGui::Button("+ New Playlist"))
    {
        _selectedPlaylistId = _presetLibrary.CreatePlaylist("");
    }

    ImGui::SameLine();

    bool haveSelection = !_selectedPlaylistId.empty() && _presetLibrary.FindPlaylist(_selectedPlaylistId) != nullptr;

    ImGui::BeginDisabled(!haveSelection);
    if (ImGui::Button("Delete"))
    {
        _presetLibrary.DeletePlaylist(_selectedPlaylistId);
        _selectedPlaylistId.clear();
    }
    ImGui::SameLine();
    bool isActive = haveSelection && _presetLibrary.ActivePlaylistId() == _selectedPlaylistId;
    if (isActive)
    {
        ImGui::BeginDisabled(true);
        ImGui::Button("Active");
        ImGui::EndDisabled();
    }
    else if (ImGui::Button("Set Active"))
    {
        _presetLibrary.SetActivePlaylist(_selectedPlaylistId);
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    ImGui::BeginChild("##PlaylistList", ImVec2(180, 0), true);
    for (const auto& playlist : playlists)
    {
        bool selected = playlist.id == _selectedPlaylistId;
        std::string label = playlist.name + " (" + std::to_string(playlist.items.size()) + ")";
        if (playlist.id == _presetLibrary.ActivePlaylistId())
        {
            label = "* " + label;
        }
        if (ImGui::Selectable((label + "##" + playlist.id).c_str(), selected))
        {
            _selectedPlaylistId = playlist.id;
            std::strncpy(_renameBuffer, playlist.name.c_str(), sizeof(_renameBuffer) - 1);
            _renameBuffer[sizeof(_renameBuffer) - 1] = '\0';
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##PlaylistItems", ImVec2(0, 0), true);
    if (auto* playlist = haveSelection ? _presetLibrary.FindPlaylist(_selectedPlaylistId) : nullptr)
    {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##rename", _renameBuffer, sizeof(_renameBuffer)))
        {
            // Live buffer only; committed below once editing finishes.
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            _presetLibrary.RenamePlaylist(playlist->id, _renameBuffer);
        }

        ImGui::Separator();

        if (playlist->items.empty())
        {
            ImGui::TextDisabled("Empty. Add presets from Browse or Favorites (\">>\" button).");
        }

        for (size_t i = 0; i < playlist->items.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));

            const auto& path = playlist->items[i];
            auto name = Poco::Path(path).getFileName();
            bool isPlaying = path == _presetLibrary.CurrentPresetPath();

            constexpr float rowButtonsWidth = 150.0f;
            float avail = ImGui::GetContentRegionAvail().x;
            float selectableWidth = avail > rowButtonsWidth ? avail - rowButtonsWidth : avail;

            NowPlayingStyleGuard style(isPlaying);
            if (ImGui::Selectable(name.c_str(), path == _selectedPath || isPlaying, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(selectableWidth, 0)))
            {
                _selectedPath = path;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    _presetLibrary.PlayFromPlaylist(playlist->id, i);
                }
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(i == 0);
            if (ImGui::SmallButton("Up"))
            {
                _presetLibrary.MoveInPlaylist(playlist->id, i, i - 1);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(i + 1 >= playlist->items.size());
            if (ImGui::SmallButton("Dn"))
            {
                _presetLibrary.MoveInPlaylist(playlist->id, i, i + 1);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::SmallButton("\xC3\x97")) // "\xC3\x97" is the UTF-8 encoding of U+00D7 (x), written as
                                                 // raw bytes so the literal survives regardless of source-file
                                                 // encoding detection.
            {
                _presetLibrary.RemoveFromPlaylist(playlist->id, i);
                ImGui::PopID();
                break;
            }

            ImGui::PopID();
        }
    }
    else
    {
        ImGui::TextDisabled("Select or create a playlist.");
    }
    ImGui::EndChild();
}

void PresetBrowserWindow::DrawFileRow(const std::string& label, const std::string& fullPath, const std::function<void(const std::string&)>& onPlay)
{
    bool favorite = _presetLibrary.IsFavorite(fullPath);
    std::string starId = std::string(favorite ? "*" : "+") + "##fav_" + fullPath;
    if (ImGui::SmallButton(starId.c_str()))
    {
        _presetLibrary.ToggleFavorite(fullPath);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", favorite ? "Remove favorite" : "Add favorite");
    }

    ImGui::SameLine();
    std::string addId = std::string(">>") + "##add_" + fullPath;
    if (ImGui::SmallButton(addId.c_str()))
    {
        _addToPlaylistTarget = fullPath;
        _openAddToPlaylistPopup = true;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Add to playlist");
    }

    ImGui::SameLine();
    bool isPlaying = fullPath == _presetLibrary.CurrentPresetPath();
    bool isSelected = fullPath == _selectedPath;
    std::string selectableId = label + "##sel_" + fullPath;

    NowPlayingStyleGuard style(isPlaying);
    if (ImGui::Selectable(selectableId.c_str(), isSelected || isPlaying, ImGuiSelectableFlags_AllowDoubleClick))
    {
        _selectedPath = fullPath;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            onPlay(fullPath);
        }
    }
}

void PresetBrowserWindow::DrawAddToPlaylistPopup()
{
    if (ImGui::BeginPopup("AddToPlaylistPopup"))
    {
        ImGui::TextDisabled("%s", Poco::Path(_addToPlaylistTarget).getFileName().c_str());
        ImGui::Separator();

        for (const auto& playlist : _presetLibrary.Playlists())
        {
            std::string label = playlist.name + " (" + std::to_string(playlist.items.size()) + ")";
            if (ImGui::Selectable(label.c_str()))
            {
                _presetLibrary.AddToPlaylist(playlist.id, _addToPlaylistTarget);
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::Separator();
        if (ImGui::Selectable("+ New playlist"))
        {
            auto id = _presetLibrary.CreatePlaylist("");
            _presetLibrary.AddToPlaylist(id, _addToPlaylistTarget);
            _selectedPlaylistId = id;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}
