#include "sample_catalog_scanner.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
void touch(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.put('\0');
}
}

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("amen_catalog_" + std::to_string(stamp));
    std::filesystem::create_directories(root / "EMPTY");
    touch(root / "ROOT.WAV");
    touch(root / "notes.txt");
    touch(root / "BREAKS" / "AMEN.wav");
    touch(root / "BREAKS" / "DEEP" / "SOUL.WaV");
    touch(root / "IGNORED" / "image.png");

    SampleCatalog catalog;
    std::string error;
    assert(scanSampleDirectory(root, catalog, error));
    assert(error.empty());
    assert(catalog.wavCount() == 3);

    const auto entries = catalog.entries(SampleCatalog::rootId());
    assert(entries.size() == 2);
    assert(entries[0].kind == SampleCatalog::EntryKind::Folder);
    assert(entries[0].name == "BREAKS");
    assert(entries[1].kind == SampleCatalog::EntryKind::Wav);
    assert(entries[1].name == "ROOT.WAV");

    SampleCatalog unchanged;
    assert(unchanged.addWavPath("KEEP.WAV"));
    assert(!scanSampleDirectory(root / "missing", unchanged, error));
    assert(unchanged.wavCount() == 1);

    std::filesystem::remove_all(root);
    return 0;
}
