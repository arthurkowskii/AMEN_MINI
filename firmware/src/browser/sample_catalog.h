#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

class SampleCatalog {
public:
    using FolderId = std::size_t;
    using WavId = std::size_t;

    static constexpr FolderId rootId() { return 0; }

    enum class EntryKind {
        Folder,
        Wav,
    };

    struct Entry {
        EntryKind kind;
        std::size_t id;
        std::string name;
        std::string relativePath;
    };

    struct FolderInfo {
        FolderId id;
        std::optional<FolderId> parent;
        std::string name;
        std::string relativePath;
    };

    struct WavInfo {
        WavId id;
        FolderId parent;
        std::string name;
        std::string relativePath;
    };

    SampleCatalog();

    // Adds one backend-provided relative WAV path. Invalid and duplicate paths
    // leave the catalog unchanged and return false.
    bool addWavPath(const std::string& path);

    bool validFolder(FolderId id) const;
    std::optional<FolderInfo> folder(FolderId id) const;
    std::optional<WavInfo> wav(WavId id) const;
    std::optional<FolderId> parent(FolderId id) const;

    // Returns display-sorted copies, so later additions cannot invalidate API data.
    std::vector<Entry> entries(FolderId folderId) const;

    std::size_t folderCount() const { return folders_.size(); }
    std::size_t wavCount() const { return wavs_.size(); }

private:
    struct FolderNode {
        std::optional<FolderId> parent;
        std::string name;
        std::string relativePath;
        std::vector<FolderId> children;
        std::vector<WavId> wavs;
    };

    struct WavNode {
        FolderId parent;
        std::string name;
        std::string relativePath;
    };

    std::vector<FolderNode> folders_;
    std::vector<WavNode> wavs_;
};
