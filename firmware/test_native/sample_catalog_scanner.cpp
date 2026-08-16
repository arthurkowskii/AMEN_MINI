#include "sample_catalog_scanner.h"

#include <system_error>

bool scanSampleDirectory(const std::filesystem::path& root, SampleCatalog& catalog,
                         std::string& error) {
    error.clear();
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        error = ec ? ec.message() : "scan root is not a directory";
        return false;
    }

    SampleCatalog scanned;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator iterator(root, options, ec);
    const std::filesystem::recursive_directory_iterator end;
    if (ec) {
        error = ec.message();
        return false;
    }

    while (iterator != end) {
        const auto entry = *iterator;
        const auto status = entry.symlink_status(ec);
        if (ec) {
            error = ec.message();
            return false;
        }
        if (std::filesystem::is_symlink(status)) {
            if (std::filesystem::is_directory(status)) iterator.disable_recursion_pending();
        } else if (std::filesystem::is_regular_file(status)) {
            const auto relative = std::filesystem::relative(entry.path(), root, ec);
            if (ec) {
                error = ec.message();
                return false;
            }
            scanned.addWavPath(relative.generic_string());
        }

        iterator.increment(ec);
        if (ec) {
            error = ec.message();
            return false;
        }
    }

    catalog = std::move(scanned);
    return true;
}
