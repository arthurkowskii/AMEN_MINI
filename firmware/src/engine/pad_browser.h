#pragma once
#include "pcm_wav.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace amen {
class PadBrowser {
public:
    static constexpr size_t kEntries = 64, kPageSize = 61, kPath = 384, kName = 256;
    enum class Kind { File, Directory, Parent, PreviousPage, NextPage };
    struct Entry { char name[kName]{}; Kind kind = Kind::File; };
    struct Pad { char path[kPath]{}; PcmWav wav; };
    Pad pads[20]{};
    Entry entries[kEntries]{};
    char directory[kPath] = "/";
    size_t count = 0, selected = 0;
    uint8_t target = 0;
    bool active = false, scanning = false;
    uint32_t pageStart = 0;
    void open(uint8_t pad) { target = pad; active = pad < 20; pageStart = 0; }
    void cancel() { active = false; scanning = false; }
    void resetEntries() {
        count = selected = visited_ = pageEntries_ = 0;
        append("..", Kind::Parent);
        if (pageStart) append("PREVIOUS PAGE", Kind::PreviousPage);
    }
    static bool isWav(const char* name) {
        const size_t length = strlen(name);
        if (length < 4) return false;
        const char* extension = name + length - 4;
        return extension[0] == '.' && (extension[1] == 'w' || extension[1] == 'W') &&
            (extension[2] == 'a' || extension[2] == 'A') && (extension[3] == 'v' || extension[3] == 'V');
    }
    bool add(const char* name, bool folder) {
        if ((!folder && !isWav(name)) || strlen(name) >= kName) return true;
        if (visited_++ < pageStart) return true;
        if (pageEntries_ == kPageSize) {
            append("NEXT PAGE", Kind::NextPage);
            return false;
        }
        append(name, folder ? Kind::Directory : Kind::File);
        ++pageEntries_;
        return true;
    }
    void turn(int32_t delta) {
        if (!active || !count) return;
        const int64_t next = int64_t(selected) + delta;
        selected = next < 0 ? 0 : (next >= int64_t(count) ? count - 1 : size_t(next));
    }
    bool selectedPath(char* out) const {
        if (selected >= count || (entries[selected].kind != Kind::File && entries[selected].kind != Kind::Directory)) return false;
        const int n = snprintf(out, kPath, "%s%s%s", directory, strcmp(directory, "/") ? "/" : "", entries[selected].name);
        return n >= 0 && n < int(kPath);
    }
    bool enter() {
        if (selected >= count) return false;
        if (entries[selected].kind == Kind::NextPage) { pageStart += kPageSize; return true; }
        if (entries[selected].kind == Kind::PreviousPage) { pageStart -= kPageSize; return true; }
        if (entries[selected].kind == Kind::Parent) {
            char* slash = strrchr(directory, '/');
            if (slash == directory) directory[1] = 0; else if (slash) *slash = 0;
            pageStart = 0;
            return true;
        }
        if (entries[selected].kind != Kind::Directory) return false;
        char path[kPath];
        if (!selectedPath(path)) return false;
        strcpy(directory, path); pageStart = 0; return true;
    }
    bool assign(const char* path, const PcmWav& wav) {
        if (!active || target >= 20 || strlen(path) >= kPath || !wav.frames || (wav.channels != 1 && wav.channels != 2)) return false;
        strcpy(pads[target].path, path); pads[target].wav = wav; cancel(); return true;
    }
private:
    uint32_t visited_ = 0;
    size_t pageEntries_ = 0;
    void append(const char* name, Kind kind) {
        if (count >= kEntries) return;
        strcpy(entries[count].name, name);
        entries[count++].kind = kind;
    }
};
}
