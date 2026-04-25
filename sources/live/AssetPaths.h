#ifndef LIVE_ASSET_PATHS_H_
#define LIVE_ASSET_PATHS_H_

#include <filesystem>

namespace live {

// Find the JD2.speaker file relative to the running binary. Searches the
// executable's directory first (Linux/Windows install layout), then the
// macOS .app bundle's Resources/, then falls back through the source tree
// so a binary built into build/<preset>/ during development still works.
// Returns an empty path when the file cannot be located.
std::filesystem::path findSpeakerFile(const char* argv0);

}  // namespace live

#endif  // LIVE_ASSET_PATHS_H_
