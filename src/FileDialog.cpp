#include "FileDialog.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <iostream>

static std::string buildFilterString(const std::vector<FileFilter>& filters)
{
    std::string r;
    for (auto& f : filters) {
        r += f.label;   r += '\0';
        r += f.pattern; r += '\0';
    }
    r += "All Files"; r += '\0';
    r += "*.*";        r += '\0';
    r += '\0';
    return r;
}

// ---- Single file -----------------------------------------------------------

std::string FileDialog::openFile(const std::string& title,
                                  const std::vector<FileFilter>& filters,
                                  const std::string& defaultExt)
{
    char buf[MAX_PATH] = {};
    std::string filt = buildFilterString(filters);

    OPENFILENAMEA ofn = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.lpstrFilter  = filt.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile    = buf;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrTitle   = title.c_str();
    ofn.Flags        = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!defaultExt.empty()) ofn.lpstrDefExt = defaultExt.c_str();

    return GetOpenFileNameA(&ofn) ? std::string(buf) : "";
}

// ---- Multi-file ------------------------------------------------------------
// GetOpenFileName with OFN_ALLOWMULTISELECT returns results in a single buffer:
//   - If one file:   "C:\dir\file.obj\0"
//   - If multiple:   "C:\dir\0file1.obj\0file2.obj\0\0"
// We parse that format here.

std::vector<std::string> FileDialog::openFiles(const std::string& title,
                                                const std::vector<FileFilter>& filters,
                                                const std::string& defaultExt)
{
    // Large buffer needed for multiple long paths
    static const int BUF = 32768;
    std::vector<char> buf(BUF, 0);

    std::string filt = buildFilterString(filters);

    OPENFILENAMEA ofn = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.lpstrFilter  = filt.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile    = buf.data();
    ofn.nMaxFile     = BUF;
    ofn.lpstrTitle   = title.c_str();
    ofn.Flags        = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST |
                       OFN_NOCHANGEDIR   | OFN_ALLOWMULTISELECT |
                       OFN_EXPLORER;
    if (!defaultExt.empty()) ofn.lpstrDefExt = defaultExt.c_str();

    if (!GetOpenFileNameA(&ofn)) {
        DWORD err = CommDlgExtendedError();
        if (err) std::cerr << "[FileDialog] Error: " << err << "\n";
        return {};
    }

    std::vector<std::string> results;

    // Parse the double-null-terminated result buffer
    const char* p = buf.data();

    // First string: directory (or full path if single file)
    std::string first(p);
    p += first.size() + 1;

    if (*p == '\0') {
        // Only one token — it's the full path of a single file
        results.push_back(first);
    } else {
        // Multiple files: first token is the directory
        std::string dir = first;
        if (dir.back() != '\\' && dir.back() != '/')
            dir += '\\';

        while (*p != '\0') {
            std::string filename(p);
            results.push_back(dir + filename);
            p += filename.size() + 1;
        }
    }

    return results;
}

// ---- Save ------------------------------------------------------------------

std::string FileDialog::saveFile(const std::string& title,
                                  const std::vector<FileFilter>& filters,
                                  const std::string& defaultExt)
{
    char buf[MAX_PATH] = {};
    std::string filt = buildFilterString(filters);

    OPENFILENAMEA ofn = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.lpstrFilter  = filt.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile    = buf;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrTitle   = title.c_str();
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!defaultExt.empty()) ofn.lpstrDefExt = defaultExt.c_str();

    return GetSaveFileNameA(&ofn) ? std::string(buf) : "";
}

#else

// ---- Non-Windows stubs -----------------------------------------------------
std::string FileDialog::openFile(const std::string&, const std::vector<FileFilter>&, const std::string&)
{ return ""; }

std::vector<std::string> FileDialog::openFiles(const std::string&, const std::vector<FileFilter>&, const std::string&)
{ return {}; }

std::string FileDialog::saveFile(const std::string&, const std::vector<FileFilter>&, const std::string&)
{ return ""; }

#endif
