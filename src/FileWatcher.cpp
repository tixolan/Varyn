#include "FileWatcher.hpp"

namespace varyn {

void FileWatcher::setFiles(const std::vector<fs::path>& files) {
    // Files we already watch keep the timestamp recorded when we last looked
    // at them. Otherwise an edit made while a reload was running would be
    // silently absorbed here and never trigger another reload.
    std::map<fs::path, fs::file_time_type> updated;
    for (const fs::path& file : files) {
        auto known = timestamps_.find(file);
        updated[file] = known != timestamps_.end() ? known->second : timestampOf(file);
    }
    timestamps_ = std::move(updated);
}

bool FileWatcher::poll() {
    bool changed = false;
    for (auto& [file, lastTimestamp] : timestamps_) {
        fs::file_time_type current = timestampOf(file);
        if (current == fs::file_time_type::min()) continue;  // temporarily missing
        if (current != lastTimestamp) {
            lastTimestamp = current;
            changed = true;
        }
    }
    return changed;
}

fs::file_time_type FileWatcher::timestampOf(const fs::path& file) {
    std::error_code error;
    fs::file_time_type timestamp = fs::last_write_time(file, error);
    return error ? fs::file_time_type::min() : timestamp;
}

}  // namespace varyn
