#pragma once
#include <string>
#include <vector>

struct FileFilter {
    std::string label;
    std::string pattern;
};

class FileDialog
{
public:
    // Open a single file — returns path or empty if cancelled
    static std::string openFile(
        const std::string&             title,
        const std::vector<FileFilter>& filters,
        const std::string&             defaultExt = "");

    // Open multiple files — returns list of paths (empty if cancelled)
    static std::vector<std::string> openFiles(
        const std::string&             title,
        const std::vector<FileFilter>& filters,
        const std::string&             defaultExt = "");

    // Save dialog — returns path or empty if cancelled
    static std::string saveFile(
        const std::string&             title,
        const std::vector<FileFilter>& filters,
        const std::string&             defaultExt = "");
};
