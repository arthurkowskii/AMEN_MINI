#pragma once

#include "sample_catalog.h"

#include <filesystem>
#include <string>

// Desktop backend used to exercise the same catalog that the Teensy SD
// backend will populate. The catalog is replaced only after a complete scan.
bool scanSampleDirectory(const std::filesystem::path& root, SampleCatalog& catalog,
                         std::string& error);
