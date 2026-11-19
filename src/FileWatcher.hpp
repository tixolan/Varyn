#pragma once

#include <filesystem>
#include <map>
#include <vector>

namespace varyn {

namespace fs = std::filesystem;

// Polls a set of files for modifications by comparing their timestamps.
//
// Polling is simple, portable and plenty fast for a handful of files.
// Editors that save by "write temp file + rename" are handled too: while a
// file is briefly missing we simply keep the last known timestamp.
class FileWatcher {
public:
    void setFiles(const std::vector<fs::path>& files);

    // Returns true if any watched file changed since the previous call.
    bool poll();

private:
    static fs::file_time_type timestampOf(const fs::path& file);

    std::map<fs::path, fs::file_time_type> timestamps_;
};

}  // namespace varyn
