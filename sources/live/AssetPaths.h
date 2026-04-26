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

// Find a runtime asset by filename (e.g. "JetBrainsMono-Regular.ttf") next
// to the running binary. Unlike findSpeakerFile this does not search the
// source tree — assets that aren't checked into data/ are only available
// once the build's POST_BUILD copy or bundle staging has placed them
// alongside the executable. Returns an empty path on miss.
std::filesystem::path findRuntimeAsset(const char* argv0, const char* filename);

}  // namespace live

#endif  // LIVE_ASSET_PATHS_H_
