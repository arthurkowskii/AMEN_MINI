#include "sample_catalog.h"

#include <algorithm>

namespace {

bool isSeparator(char value) {
    return value == '/' || value == '\\';
}

char asciiLower(char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value + ('a' - 'A'));
    }
    return value;
}

bool caseInsensitiveEqual(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }
    return std::equal(left.begin(), left.end(), right.begin(),
                      [](char leftChar, char rightChar) {
                          return asciiLower(leftChar) == asciiLower(rightChar);
                      });
}

bool caseInsensitiveLess(const std::string& left, const std::string& right) {
    const std::size_t commonSize = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < commonSize; ++i) {
        const char leftLower = asciiLower(left[i]);
        const char rightLower = asciiLower(right[i]);
        if (leftLower != rightLower) {
            return leftLower < rightLower;
        }
    }
    if (left.size() != right.size()) {
        return left.size() < right.size();
    }
    return left < right;
}

bool hasWavExtension(const std::string& name) {
    if (name.size() < 4) {
        return false;
    }
    const std::size_t extension = name.size() - 4;
    return name[extension] == '.' && asciiLower(name[extension + 1]) == 'w' &&
           asciiLower(name[extension + 2]) == 'a' &&
           asciiLower(name[extension + 3]) == 'v';
}

bool parseRelativePath(const std::string& path,
                       std::vector<std::string>& segments,
                       std::string& normalized) {
    if (path.empty() || isSeparator(path.front())) {
        return false;
    }
    std::string segment;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i != path.size() && !isSeparator(path[i])) {
            segment.push_back(path[i]);
            continue;
        }
        if (segment.empty() || segment == "." || segment == ".." ||
            segment.find(':') != std::string::npos) {
            return false;
        }
        if (!normalized.empty()) {
            normalized.push_back('/');
        }
        normalized += segment;
        segments.push_back(segment);
        segment.clear();
    }
    return !segments.empty() && hasWavExtension(segments.back());
}

}  // namespace

SampleCatalog::SampleCatalog() {
    folders_.push_back({std::nullopt, "", "", {}, {}});
}

bool SampleCatalog::addWavPath(const std::string& path) {
    std::vector<std::string> segments;
    std::string normalized;
    if (!parseRelativePath(path, segments, normalized)) {
        return false;
    }

    FolderId current = rootId();
    std::string folderPath;
    for (std::size_t segmentIndex = 0; segmentIndex + 1 < segments.size();
         ++segmentIndex) {
        const std::string& segment = segments[segmentIndex];
        const auto& children = folders_[current].children;
        const auto existing = std::find_if(children.begin(), children.end(),
                                           [&](FolderId child) {
                                               return caseInsensitiveEqual(
                                                   folders_[child].name, segment);
                                           });
        if (existing != children.end()) {
            current = *existing;
            folderPath = folders_[current].relativePath;
            continue;
        }

        if (!folderPath.empty()) {
            folderPath.push_back('/');
        }
        folderPath += segment;
        const FolderId newId = folders_.size();
        folders_.push_back({current, segment, folderPath, {}, {}});
        folders_[current].children.push_back(newId);
        current = newId;
    }

    const auto& wavIds = folders_[current].wavs;
    const bool duplicate = std::any_of(wavIds.begin(), wavIds.end(), [&](WavId wavId) {
        return caseInsensitiveEqual(wavs_[wavId].name, segments.back());
    });
    if (duplicate) return false;

    const WavId wavId = wavs_.size();
    wavs_.push_back({current, segments.back(), normalized});
    folders_[current].wavs.push_back(wavId);
    return true;
}

bool SampleCatalog::validFolder(FolderId id) const {
    return id < folders_.size();
}

std::optional<SampleCatalog::FolderInfo> SampleCatalog::folder(FolderId id) const {
    if (!validFolder(id)) {
        return std::nullopt;
    }
    const FolderNode& node = folders_[id];
    return FolderInfo{id, node.parent, node.name, node.relativePath};
}

std::optional<SampleCatalog::WavInfo> SampleCatalog::wav(WavId id) const {
    if (id >= wavs_.size()) {
        return std::nullopt;
    }
    const WavNode& node = wavs_[id];
    return WavInfo{id, node.parent, node.name, node.relativePath};
}

std::optional<SampleCatalog::FolderId> SampleCatalog::parent(FolderId id) const {
    if (!validFolder(id)) {
        return std::nullopt;
    }
    return folders_[id].parent;
}

std::vector<SampleCatalog::Entry> SampleCatalog::entries(FolderId folderId) const {
    std::vector<Entry> result;
    if (!validFolder(folderId)) {
        return result;
    }

    const FolderNode& folderNode = folders_[folderId];
    result.reserve(folderNode.children.size() + folderNode.wavs.size());
    for (FolderId childId : folderNode.children) {
        const FolderNode& child = folders_[childId];
        result.push_back({EntryKind::Folder, childId, child.name, child.relativePath});
    }
    for (WavId wavId : folderNode.wavs) {
        const WavNode& wavNode = wavs_[wavId];
        result.push_back({EntryKind::Wav, wavId, wavNode.name, wavNode.relativePath});
    }

    std::sort(result.begin(), result.end(), [](const Entry& left, const Entry& right) {
        if (left.kind != right.kind) {
            return left.kind == EntryKind::Folder;
        }
        if (left.name != right.name) {
            return caseInsensitiveLess(left.name, right.name);
        }
        if (left.relativePath != right.relativePath) {
            return caseInsensitiveLess(left.relativePath, right.relativePath);
        }
        return left.id < right.id;
    });
    return result;
}
