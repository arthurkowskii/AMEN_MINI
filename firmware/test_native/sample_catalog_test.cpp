#include "sample_catalog.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        std::exit(1);
    }
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

SampleCatalog::FolderId folderId(const std::vector<SampleCatalog::Entry>& entries,
                                 const std::string& name) {
    for (const auto& entry : entries) {
        if (entry.kind == SampleCatalog::EntryKind::Folder && entry.name == name) {
            return entry.id;
        }
    }
    REQUIRE(false);
    return SampleCatalog::rootId();
}

void testHierarchyAndNavigation() {
    SampleCatalog catalog;
    REQUIRE(catalog.addWavPath("root.wav"));
    REQUIRE(catalog.addWavPath("Kits/808/kick.wav"));
    REQUIRE(catalog.addWavPath("Loops\\Amen.WAV"));

    const auto rootEntries = catalog.entries(SampleCatalog::rootId());
    REQUIRE(rootEntries.size() == 3);
    REQUIRE(rootEntries[0].kind == SampleCatalog::EntryKind::Folder);
    REQUIRE(rootEntries[0].name == "Kits");
    REQUIRE(rootEntries[1].kind == SampleCatalog::EntryKind::Folder);
    REQUIRE(rootEntries[1].name == "Loops");
    REQUIRE(rootEntries[2].kind == SampleCatalog::EntryKind::Wav);
    REQUIRE(rootEntries[2].name == "root.wav");

    const auto kitsId = folderId(rootEntries, "Kits");
    const auto kitsEntries = catalog.entries(kitsId);
    REQUIRE(kitsEntries.size() == 1);
    REQUIRE(kitsEntries[0].name == "808");
    const auto deepId = kitsEntries[0].id;
    const auto deepEntries = catalog.entries(deepId);
    REQUIRE(deepEntries.size() == 1);
    REQUIRE(deepEntries[0].kind == SampleCatalog::EntryKind::Wav);
    REQUIRE(deepEntries[0].name == "kick.wav");
    REQUIRE(deepEntries[0].relativePath == "Kits/808/kick.wav");

    REQUIRE(!catalog.parent(SampleCatalog::rootId()).has_value());
    REQUIRE(catalog.parent(kitsId) == SampleCatalog::rootId());
    REQUIRE(catalog.parent(deepId) == kitsId);
    REQUIRE(!catalog.parent(9999).has_value());
}

void testRejectionsAndDeduplication() {
    SampleCatalog catalog;
    REQUIRE(!catalog.addWavPath("Pending/readme.txt"));
    REQUIRE(!catalog.addWavPath("/absolute.wav"));
    REQUIRE(!catalog.addWavPath("C:\\absolute.wav"));
    REQUIRE(!catalog.addWavPath("C:file.wav"));
    REQUIRE(!catalog.addWavPath("bad:name/file.wav"));
    REQUIRE(!catalog.addWavPath("bad/file:name.wav"));
    REQUIRE(!catalog.addWavPath("bad//empty.wav"));
    REQUIRE(!catalog.addWavPath("bad/./dot.wav"));
    REQUIRE(!catalog.addWavPath("bad/../escape.wav"));
    REQUIRE(!catalog.addWavPath("bad/trailing.wav/"));
    REQUIRE(!catalog.addWavPath(""));
    REQUIRE(catalog.folderCount() == 1);
    REQUIRE(catalog.wavCount() == 0);

    REQUIRE(catalog.addWavPath("Drums\\Kick.WaV"));
    REQUIRE(!catalog.addWavPath("drums/KICK.wav"));
    REQUIRE(!catalog.addWavPath("DRUMS\\kick.WAV"));
    REQUIRE(catalog.addWavPath("drums/Snare.wav"));
    REQUIRE(catalog.folderCount() == 2);
    REQUIRE(catalog.wavCount() == 2);

    const auto rootEntries = catalog.entries(SampleCatalog::rootId());
    REQUIRE(rootEntries.size() == 1);
    REQUIRE(rootEntries[0].name == "Drums");
    REQUIRE(rootEntries[0].relativePath == "Drums");
    const auto drumEntries = catalog.entries(rootEntries[0].id);
    REQUIRE(drumEntries.size() == 2);
    REQUIRE(drumEntries[0].relativePath == "Drums/Kick.WaV");
    REQUIRE(drumEntries[1].relativePath == "drums/Snare.wav");
}

void testDisplaySortAndStableIds() {
    SampleCatalog catalog;
    REQUIRE(catalog.addWavPath("beta/item.wav"));
    const auto betaId = folderId(catalog.entries(SampleCatalog::rootId()), "beta");
    REQUIRE(catalog.addWavPath("Alpha/item.wav"));
    REQUIRE(!catalog.addWavPath("alpha/ITEM.WAV"));
    REQUIRE(catalog.addWavPath("ALPHA/other.wav"));
    REQUIRE(catalog.addWavPath("z.wav"));
    REQUIRE(catalog.addWavPath("A.wav"));
    REQUIRE(!catalog.addWavPath("a.WAV"));

    const auto entries = catalog.entries(SampleCatalog::rootId());
    REQUIRE(entries.size() == 4);
    REQUIRE(entries[0].name == "Alpha");
    REQUIRE(entries[1].name == "beta");
    REQUIRE(entries[2].name == "A.wav");
    REQUIRE(entries[3].name == "z.wav");

    const auto alphaEntries = catalog.entries(entries[0].id);
    REQUIRE(alphaEntries.size() == 2);
    REQUIRE(alphaEntries[0].relativePath == "Alpha/item.wav");
    REQUIRE(alphaEntries[1].relativePath == "ALPHA/other.wav");

    const auto beta = catalog.folder(betaId);
    REQUIRE(beta.has_value());
    REQUIRE(beta->name == "beta");
    REQUIRE(beta->relativePath == "beta");
    REQUIRE(catalog.validFolder(betaId));
    REQUIRE(!catalog.validFolder(9999));

    const auto firstWav = catalog.wav(entries[2].id);
    REQUIRE(firstWav.has_value());
    REQUIRE(firstWav->name == "A.wav");
}

}  // namespace

int main() {
    testHierarchyAndNavigation();
    testRejectionsAndDeduplication();
    testDisplaySortAndStableIds();
    std::cout << "sample_catalog_test: OK\n";
    return 0;
}
