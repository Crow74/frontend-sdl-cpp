#include "FileChooser.h"

#include "imgui.h"

#include <Poco/SortedDirectoryIterator.h>
#include <Poco/String.h>

#include <algorithm>

FileChooser::FileChooser(FileChooser::Mode mode)
    : _mode(mode)
{
}

FileChooser::FileChooser(std::string title, const std::string& initialDirectory, Mode mode)
    : _title(std::move(title))
    , _mode(mode)
{
    if (!initialDirectory.empty())
    {
        ChangeDirectory(initialDirectory);
    }
}

void FileChooser::Title(const std::string& title)
{
    _title = title;
}

void FileChooser::CurrentDirectory(const std::string& path)
{
    if (path.empty())
    {
        return;
    }

    ChangeDirectory(path);
}

std::string FileChooser::CurrentDirectory() const
{
    return _currentDir.toString();
}

void FileChooser::DefaultFileName(const std::string& name)
{
    _defaultFileName = name;
}

void FileChooser::Context(const std::string& context)
{
    _context = context;
}

const std::string& FileChooser::Context() const
{
    return _context;
}

void FileChooser::AllowedExtensions(std::vector<std::string> extensions)
{
    _extensions = std::move(extensions);
}

const std::vector<std::string>& FileChooser::AllowedExtensions() const
{
    return _extensions;
}


void FileChooser::MultiSelect(bool enabled)
{
    _multiSelect = enabled;
}

bool FileChooser::MultiSelect() const
{
    return _multiSelect;
}

void FileChooser::Show()
{
    _selectedFiles.clear();
    _confirmOverwrite = false;

    if (_mode == Mode::SaveFile)
    {
        std::strncpy(_fileNameBuffer, _defaultFileName.c_str(), sizeof(_fileNameBuffer) - 1);
        _fileNameBuffer[sizeof(_fileNameBuffer) - 1] = '\0';
    }

    // Files may have been created (e.g. exported) since this directory was last listed, and
    // ChangeDirectory() otherwise skips re-scanning a path it's already sitting in.
    ChangeDirectory(_currentDir, true);

    _visible = true;
}

void FileChooser::Close()
{
    ImGui::CloseCurrentPopup();
    _visible = false;
}

bool FileChooser::Draw()
{
    if (!_visible)
    {
        return false;
    }

    bool fileSelected{false};

    if (!_currentDir.isDirectory() || (!_currentDir.toString().empty() && !Poco::File(_currentDir).exists()))
    {
        ChangeDirectory(Poco::Path::home());
    }

    std::string dialogTileAndId = _title + "###File Chooser";

    ImGui::OpenPopup(dialogTileAndId.c_str());

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal(dialogTileAndId.c_str(), &_visible, ImGuiWindowFlags_NoCollapse))
    {
        DrawNavButtons();

        ImGui::Separator();

        char pathBuffer[2048]{};
        strncpy(pathBuffer, _currentDir.toString().c_str(), std::min<size_t>(2047, _currentDir.toString().size()));

        ImGui::SetNextItemWidth(-1);

        if (ImGui::InputText("##path", &pathBuffer[0], IM_ARRAYSIZE(pathBuffer)), ImGuiInputTextFlags_EnterReturnsTrue)
        {
            ChangeDirectory(std::string(pathBuffer));
        }

        // Reserve enough room below the list for whatever controls this mode draws there:
        // one row (Cancel/Select) normally, but two in SaveFile mode (a filename field, or an
        // overwrite warning, above the Cancel/Save row) - otherwise those get pushed below the
        // popup's visible bounds.
        float bottomRows = (_mode == Mode::SaveFile) ? 2.0f : 1.0f;
        float reservedBottomHeight = bottomRows * ImGui::GetFrameHeightWithSpacing() + 4.0f;
        if (ImGui::BeginListBox("##filelist", ImVec2(-1, -reservedBottomHeight)))
        {
            if (_currentDir.toString().empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No path entered");
            }
            else
            {
                Poco::File pathCheck(_currentDir);

                if (!pathCheck.exists())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Directory does not exist");
                }
                else if (!pathCheck.canRead())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Directory cannot be accessed");
                }
                else
                {
                    fileSelected = PopulateFileList();
                }
            }

            ImGui::EndListBox();
        }

        if (_mode == Mode::SaveFile && _confirmOverwrite)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "\"%s\" already exists.", _pendingSavePath.getFileName().c_str());
            if (ImGui::Button("Overwrite"))
            {
                _selectedFiles.clear();
                _selectedFiles.emplace_back(_pendingSavePath);
                fileSelected = true;
                Close();
            }
            ImGui::SameLine();
            if (ImGui::Button("Choose a Different Name"))
            {
                _confirmOverwrite = false;
            }
        }
        else if (_mode == Mode::SaveFile)
        {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##savefilename", _fileNameBuffer, IM_ARRAYSIZE(_fileNameBuffer));

            ImGui::PushStyleColor(ImGuiCol_Button, 0xFF000080);
            if (ImGui::Button("Cancel"))
            {
                _selectedFiles.clear();
                fileSelected = true;
                Close();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button("Save"))
            {
                std::string typedName = Poco::trim(std::string(_fileNameBuffer));
                if (!typedName.empty())
                {
                    if (!_extensions.empty())
                    {
                        std::string extensionWithDot = "." + _extensions.front();
                        bool hasExtension = typedName.size() >= extensionWithDot.size() &&
                            Poco::icompare(typedName.substr(typedName.size() - extensionWithDot.size()), extensionWithDot) == 0;
                        if (!hasExtension)
                        {
                            typedName += extensionWithDot;
                        }
                    }

                    Poco::Path target(_currentDir);
                    target.setFileName(typedName);

                    if (Poco::File(target).exists())
                    {
                        _confirmOverwrite = true;
                        _pendingSavePath = target;
                    }
                    else
                    {
                        _selectedFiles.clear();
                        _selectedFiles.emplace_back(target);
                        fileSelected = true;
                        Close();
                    }
                }
            }
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, 0xFF000080);
            if (ImGui::Button("Cancel"))
            {
                _selectedFiles.clear();
                fileSelected = true;
                Close();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button("Select"))
            {
                for (auto index : _selectedFileIndices)
                {
                    _selectedFiles.emplace_back(_currentFileList.at(index));
                }

                if (_selectedFileIndices.empty() && _mode == Mode::Directory)
                {
                    _selectedFiles.emplace_back(Poco::Path(_currentDir).makeDirectory());
                }
                else
                {
                    // ToDo: Display "Select at least one entry from the list"
                }

                fileSelected = true;
                Close();
            }
        }

        ImGui::EndPopup();
    }
    else
    {
        Close();
    }

    return fileSelected;
}

const std::vector<Poco::File>& FileChooser::SelectedFiles() const
{
    return _selectedFiles;
}

void FileChooser::DrawNavButtons()
{
    ImGui::Checkbox("Show hidden", &_showHidden);

    // Root path buttons first
    std::vector<std::string> roots;
    Poco::Path::listRoots(roots);

    if (ImGui::Button("Up"))
    {
        ChangeDirectory(_currentDir.parent());
        poco_debug_f1(_logger, "Going one dir up: %s", _currentDir.toString());
    }

    ImGui::SameLine();

    if (ImGui::Button("Home"))
    {
        ChangeDirectory(Poco::Path::home());
        poco_debug_f1(_logger, "Going to user's home dir: %s", _currentDir.toString());
    }

    for (const auto& root : roots)
    {
        ImGui::SameLine();

        if (ImGui::Button(root.c_str()))
        {
            ChangeDirectory(root);
            poco_debug_f1(_logger, "Changing root/drive to: %s", _currentDir.toString());
        }
    }
}

bool FileChooser::PopulateFileList()
{
    bool fileSelected{false};
    bool changeDir{false};
    Poco::Path newDir;

    int index = 0;
    for (const auto& file : _currentFileList)
    {
        bool isSelected = (_selectedFileIndices.find(index) != _selectedFileIndices.end());
        bool isDirectory = false;
        try
        {
            // This will throw for broken symlinks or if the file/dir isn't accessible
            isDirectory = file.isDirectory();
        }
        catch (...)
        {
        }

        Poco::Path filePath(file.path());
        auto filename = filePath.getFileName();

        if (isDirectory)
        {
            filename.append("/");
        }

        if (ImGui::Selectable(filename.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
        {
            UpdateListSelection(index, isSelected);

            if (ImGui::IsMouseDoubleClicked(0))
            {
                _selectedFiles.clear();
                if (isDirectory)
                {
                    newDir = filePath;
                    changeDir = true;
                    poco_debug_f1(_logger, "Changing dir to: %s", _currentDir.toString());
                }
                else if (_mode == Mode::SaveFile)
                {
                    // Copy the clicked file's name into the filename field instead of closing,
                    // so the user can review/edit it (e.g. deliberately pick an existing file
                    // to overwrite) before confirming with the Save button.
                    auto clickedName = filePath.getFileName();
                    std::strncpy(_fileNameBuffer, clickedName.c_str(), sizeof(_fileNameBuffer) - 1);
                    _fileNameBuffer[sizeof(_fileNameBuffer) - 1] = '\0';
                }
                else
                {
                    _selectedFiles.emplace_back(filePath);
                    poco_debug_f1(_logger, "User selected file: %s", filePath.toString());
                    fileSelected = true;
                    _visible = false;
                }
            }
        }

        index++;
    }

    if (changeDir)
    {
        ChangeDirectory(newDir);
    }

    return fileSelected;
}

void FileChooser::ChangeDirectory(Poco::Path newDirectory, bool forceRescan)
{
    newDirectory.makeDirectory();

    if (!forceRescan && _currentDir.toString() == newDirectory.toString())
    {
        return;
    }

    _currentDir = newDirectory;

    _currentFileList.clear();
    _selectedFileIndices.clear();
    _selectedFileIndex = -1;

    poco_information_f1(_logger, "Changing dir: %s", newDirectory.toString());

    if (_currentDir.toString().empty())
    {
        return;
    }

    Poco::File pathCheck(_currentDir);
    if (!pathCheck.exists() || !pathCheck.canRead())
    {
        return;
    }

    // Ideally, only use the directory iterator after cd'ing into a new dir and store the path list.
    Poco::SortedDirectoryIterator directoryIterator(_currentDir);
    Poco::SortedDirectoryIterator directoryEnd;

    while (directoryIterator != directoryEnd)
    {
        bool isHidden = false;
        bool isDirectory = false;
        try
        {
            // This will throw for broken symlinks or if the file/dir isn't accessible
            isHidden = directoryIterator->isHidden();
            isDirectory = directoryIterator->isDirectory();
        }
        catch (...)
        {
        }

        if ((!isHidden || _showHidden))
        {
            if ((_mode != Mode::Directory && _extensions.empty()) || isDirectory)
            {
                _currentFileList.push_back(*directoryIterator);
            }
            else if (_mode != Mode::Directory)
            {
                auto fileExtension = directoryIterator.path().getExtension();
                for (const auto& extension : _extensions)
                {
                    if (Poco::icompare(fileExtension, extension) == 0)
                    {
                        _currentFileList.push_back(*directoryIterator);
                        break;
                    }
                }
            }
        }

        ++directoryIterator;
    }
}

void FileChooser::UpdateListSelection(int index, bool isSelected)
{
    // Reset selection on simple click
    if (!ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && !ImGui::IsKeyDown(ImGuiKey_RightCtrl) && !ImGui::IsKeyDown(ImGuiKey_LeftShift) && !ImGui::IsKeyDown(ImGuiKey_RightShift))
    {
        _selectedFileIndices.clear();
        _selectedFileIndices.insert(index);
    }

    // Multiple selection on shift+click
    if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift) && _selectedFileIndex >= 0)
    {
        if (!ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && !ImGui::IsKeyDown(ImGuiKey_RightCtrl))
        {
            // Replace selection if ctrl is not pressed
            _selectedFileIndices.clear();
        }

        if (!_multiSelect)
        {
            _selectedFileIndex = index;
        }

        for (int selIndex = std::min(_selectedFileIndex, index); selIndex <= std::max(_selectedFileIndex, index); selIndex++)
        {
            _selectedFileIndices.insert(selIndex);
        }
    }
    // Toggle selection with ctrl+click
    else if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
    {
        if (isSelected)
        {
            _selectedFileIndices.erase(index);
        }
        else
        {
            if (!_multiSelect)
            {
                _selectedFileIndices.clear();
            }
            _selectedFileIndices.insert(index);
        }
    }

    _selectedFileIndex = index;
}
