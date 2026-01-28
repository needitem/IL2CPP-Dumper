#pragma once

#include <vector>
#include <string>
#include <functional>
#include "IL2CPP_Image.h"

enum class OutputFormat {
    CSharp,
    JsonFull,
    JsonSummary
};

struct FilterOptions {
    bool skipUnityEngine = true;
    bool skipSystem = true;
    bool skipPrivate = false;
    bool skipCompilerGenerated = true;
};

class Dumper {
public:
    using LogFunc = std::function<void(const std::string&)>;
    using ProgressFunc = std::function<void(int, int, const std::string&)>;

    std::vector<IL2CPP_Image> images;
    FilterOptions filters;

    Dumper();

    void OnLog(LogFunc callback);
    void OnProgress(ProgressFunc callback);

    bool ShouldSkipAssembly(const std::string& name) const;
    bool ShouldSkipClass(const std::string& name) const;
    bool ShouldSkipMember(uint32_t flags) const;

    void ExportAssembly(const IL2CPP_Image& img, OutputFormat format);

    void ExportHuman();
    void ExportAI();
    void ExportCustom(bool cs, bool json, bool summary);

private:
    LogFunc logCallback;
    ProgressFunc progressCallback;

    void Log(const std::string& msg);
    void Progress(int current, int total, const std::string& item);
};
