#pragma once

#include <string>
#include <cstdint>

namespace Utils {

// Settings read once from IL2CPP_Dumper.cfg next to the game executable
// (the file is deleted after reading, so it only affects one injection).
//
// Format: one entry per line.
//   - A line without '=' is the output directory (legacy format).
//   - key=value lines:
//       out=C:\dump\          output directory
//       auto=ai               run headless: human | ai | mono  (no GUI window)
//       chunk=1000            max classes per JSON file (0 = never split)
//       skipUnity=0|1  skipSystem=0|1  skipPrivate=0|1  skipCompilerGenerated=0|1
struct Config {
    std::string outDir;     // always ends with a backslash
    std::string autoMode;   // empty = show the GUI
    int chunkSize = 1000;
    int skipUnity = -1;     // -1 = keep the built-in default
    int skipSystem = -1;
    int skipPrivate = -1;
    int skipCompilerGenerated = -1;
};

// Parses cfg text (no I/O; GetConfig() does the file read).
Config ParseConfig(const std::string& text);

const Config& GetConfig();

void CreateDir(const std::string& path);
std::string AccessModifier(uint32_t flags);
std::string GetGameDir();

} // namespace Utils
