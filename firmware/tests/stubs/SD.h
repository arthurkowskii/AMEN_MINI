#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

constexpr unsigned FILE_READ = 0;
struct TestFile {
    std::vector<uint8_t> bytes;
    size_t failAt = SIZE_MAX;
};
class File {
public:
    File() = default;
    explicit File(std::shared_ptr<TestFile> file) : file_(std::move(file)) {}
    explicit operator bool() const { return bool(file_); }
    uint64_t size() const { return file_ ? file_->bytes.size() : 0; }
    bool seek(uint32_t offset) {
        if (!file_ || offset > file_->bytes.size()) return false;
        offset_ = offset;
        return true;
    }
    size_t read(void* destination, size_t count) {
        if (!file_ || offset_ >= file_->failAt) return 0;
        count = std::min(count, file_->bytes.size() - offset_);
        count = std::min(count, file_->failAt - offset_);
        std::memcpy(destination, file_->bytes.data() + offset_, count);
        offset_ += count;
        return count;
    }
    void close() { file_.reset(); }
private:
    std::shared_ptr<TestFile> file_;
    size_t offset_ = 0;
};
class TestSd {
public:
    std::map<std::string, std::shared_ptr<TestFile>> files;
    std::vector<std::string> opened;
    File open(const char* path, unsigned) {
        opened.emplace_back(path);
        const auto found = files.find(path);
        return found == files.end() ? File{} : File(found->second);
    }
};
inline TestSd SD;
