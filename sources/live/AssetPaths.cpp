#include "AssetPaths.h"

#include <vector>

namespace live {

std::filesystem::path findSpeakerFile(const char* argv0) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path exe(argv0);
  fs::path exeDir = fs::absolute(exe, ec).parent_path();

  std::vector<fs::path> candidates;
  candidates.push_back(exeDir / "JD2.speaker");
#if defined(__APPLE__)
  // For .app bundles: argv0 is .../Contents/MacOS/vtl_live.
  candidates.push_back(exeDir.parent_path() / "Resources" / "JD2.speaker");
#endif
  candidates.push_back(exeDir / ".." / "JD2.speaker");
  candidates.push_back(exeDir / ".." / "data" / "speakers" / "JD2.speaker");
  candidates.push_back(exeDir / ".." / ".." / "data" / "speakers" /
                       "JD2.speaker");
  candidates.push_back(exeDir / ".." / ".." / ".." / "data" / "speakers" /
                       "JD2.speaker");

  for (const auto& p : candidates) {
    if (fs::exists(p, ec)) return fs::canonical(p, ec);
  }
  return fs::path();
}

std::filesystem::path findRuntimeAsset(const char* argv0,
                                       const char* filename) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path exe(argv0);
  fs::path exeDir = fs::absolute(exe, ec).parent_path();

  std::vector<fs::path> candidates;
  candidates.push_back(exeDir / filename);
#if defined(__APPLE__)
  candidates.push_back(exeDir.parent_path() / "Resources" / filename);
#endif

  for (const auto& p : candidates) {
    if (fs::exists(p, ec)) return fs::canonical(p, ec);
  }
  return fs::path();
}

}  // namespace live
