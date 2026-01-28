#include "../include/Dumper.h"
#include "../include/IL2CPP_API.h"
#include "../include/Utils.h"
#include <Windows.h>
#include <fstream>
#include <map>
#include <algorithm>

namespace {
    std::string EscapeJson(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:   out += c;
            }
        }
        return out;
    }
}

Dumper::Dumper() {
    IL2CPP::Initialize();
    Sleep(500);

    if (!IL2CPP::Initialized) return;

    void* domain = IL2CPP::GetDomain();
    if (!domain) return;

    size_t count = 0;
    void** assemblies = IL2CPP::GetAssemblies(domain, &count);
    if (!assemblies) return;

    for (size_t i = 0; i < count; i++) {
        void* assembly = assemblies[i];
        if (!assembly) continue;

        void* image = IL2CPP::AssemblyGetImage(assembly);
        if (!image) continue;

        const char* name = IL2CPP::ImageGetName(image);
        if (!name || !*name) continue;

        images.emplace_back(image);
    }
}

void Dumper::OnLog(LogFunc callback) {
    logCallback = callback;
}

void Dumper::OnProgress(ProgressFunc callback) {
    progressCallback = callback;
}

void Dumper::Log(const std::string& msg) {
    if (logCallback) logCallback(msg);
}

void Dumper::Progress(int current, int total, const std::string& item) {
    if (progressCallback) progressCallback(current, total, item);
}

bool Dumper::ShouldSkipAssembly(const std::string& name) const {
    if (filters.skipUnityEngine) {
        if (name.find("UnityEngine") == 0) return true;
        if (name.find("Unity.") == 0) return true;
    }
    if (filters.skipSystem) {
        if (name.find("System") == 0) return true;
        if (name == "mscorlib" || name == "mscorlib.dll") return true;
        if (name.find("Mono.") == 0) return true;
        if (name.find("netstandard") == 0) return true;
    }
    return false;
}

bool Dumper::ShouldSkipClass(const std::string& name) const {
    if (!filters.skipCompilerGenerated) return false;
    if (name.find("__") == 0) return true;
    if (name.find("<>") != std::string::npos) return true;
    if (name.find("<Module>") != std::string::npos) return true;
    if (name.find("$") != std::string::npos) return true;
    if (name.find("`") != std::string::npos) return true;
    return false;
}

bool Dumper::ShouldSkipMember(uint32_t flags) const {
    if (!filters.skipPrivate) return false;
    return (flags & 0x0007) < 0x0004;
}

void Dumper::ExportAssembly(const IL2CPP_Image& img, OutputFormat format) {
    std::string asmName = img.GetName();
    std::string safeName = asmName;
    std::replace(safeName.begin(), safeName.end(), '.', '_');
    std::replace(safeName.begin(), safeName.end(), '-', '_');

    std::string folder, ext;
    switch (format) {
        case OutputFormat::CSharp:
            folder = "C:\\IL2CPP_Dump\\";
            ext = ".cs";
            break;
        case OutputFormat::JsonFull:
            folder = "C:\\IL2CPP_Dump_JSON\\";
            ext = ".json";
            break;
        case OutputFormat::JsonSummary:
            folder = "C:\\IL2CPP_Dump_Summary\\";
            ext = ".json";
            break;
    }

    Utils::CreateDir(folder);
    std::ofstream out(folder + safeName + ext);
    if (!out.is_open()) {
        Log("  [ERROR] Cannot write: " + folder + safeName + ext);
        return;
    }

    // JSON output
    if (format == OutputFormat::JsonFull || format == OutputFormat::JsonSummary) {
        bool isSummary = (format == OutputFormat::JsonSummary);

        out << "{\n";
        out << "  \"assembly\": \"" << EscapeJson(asmName) << "\",\n";
        out << "  \"classes\": [\n";

        bool firstClass = true;
        for (size_t i = 0; i < img.GetClassCount(); i++) {
            auto cls = img.GetClass(i);
            if (!cls.handle) continue;

            std::string name = cls.GetName();
            std::string ns = cls.GetNamespace();
            if (name.find('<') != std::string::npos) continue;
            if (ShouldSkipClass(name)) continue;

            if (!firstClass) out << ",\n";
            firstClass = false;

            std::string type = cls.IsInterface() ? "interface" : (cls.IsValueType() ? "struct" : "class");
            std::string fullName = ns.empty() ? name : ns + "." + name;

            out << "    {\n";
            out << "      \"name\": \"" << EscapeJson(name) << "\",\n";
            out << "      \"fullName\": \"" << EscapeJson(fullName) << "\",\n";
            out << "      \"type\": \"" << type << "\"";

            if (!isSummary) {
                out << ",\n      \"token\": \"0x" << std::hex << cls.GetToken() << std::dec << "\"";
            }

            auto parent = cls.GetParent();
            if (parent.handle) {
                std::string pn = parent.GetName();
                if (pn != "Object" && pn != "ValueType" && pn != "Enum") {
                    out << ",\n      \"extends\": \"" << EscapeJson(pn) << "\"";
                }
            }

            // Fields
            out << ",\n      \"fields\": [";
            bool firstField = true;
            for (auto& [ff, ft, fn, off] : cls.GetFields()) {
                if (ShouldSkipMember(ff)) continue;
                if (!firstField) out << ",";
                firstField = false;
                out << "\n        {\"name\": \"" << EscapeJson(fn) << "\", \"type\": \"" << EscapeJson(ft) << "\"";
                if (!isSummary) {
                    out << ", \"access\": \"" << Utils::AccessModifier(ff) << "\"";
                    out << ", \"offset\": \"0x" << std::hex << off << std::dec << "\"";
                }
                out << "}";
            }
            out << "]";

            // Methods
            out << ",\n      \"methods\": [";
            bool firstMethod = true;
            for (auto& [mf, rt, mn, ps] : cls.GetMethods()) {
                if (ShouldSkipMember(mf)) continue;
                if (isSummary && (mn.find("<") != std::string::npos || mn.find("__") == 0)) continue;
                if (!firstMethod) out << ",";
                firstMethod = false;
                out << "\n        {\"name\": \"" << EscapeJson(mn) << "\", \"returns\": \"" << EscapeJson(rt) << "\"";
                if (!ps.empty()) {
                    out << ", \"params\": [";
                    for (size_t j = 0; j < ps.size(); j++) {
                        if (j > 0) out << ", ";
                        out << "{\"type\": \"" << EscapeJson(ps[j].first) << "\", \"name\": \"" << EscapeJson(ps[j].second) << "\"}";
                    }
                    out << "]";
                }
                if (!isSummary) {
                    out << ", \"access\": \"" << Utils::AccessModifier(mf) << "\"";
                }
                out << "}";
            }
            out << "]\n";
            out << "    }";
        }

        out << "\n  ]\n}\n";
        Log("  -> " + safeName + ext + (isSummary ? " [Summary]" : " [Full]"));
        return;
    }

    // C# output
    out << "// Assembly: " << asmName << "\n\n";
    out << "using System;\nusing System.Collections.Generic;\n\n";

    std::map<std::string, std::vector<IL2CPP_Class>> byNamespace;
    for (size_t i = 0; i < img.GetClassCount(); i++) {
        auto cls = img.GetClass(i);
        if (!cls.handle) continue;
        if (cls.GetName().find('<') != std::string::npos) continue;
        byNamespace[cls.GetNamespace()].push_back(cls);
    }

    for (auto& [ns, classes] : byNamespace) {
        if (!ns.empty()) out << "namespace " << ns << " {\n\n";

        for (auto& cls : classes) {
            std::string type = cls.IsInterface() ? "interface" : (cls.IsValueType() ? "struct" : "class");

            out << "    // Token: 0x" << std::hex << cls.GetToken() << std::dec << "\n";
            out << "    public " << type << " " << cls.GetName();

            auto parent = cls.GetParent();
            if (parent.handle) {
                std::string pn = parent.GetName();
                if (pn != "Object" && pn != "ValueType" && pn != "Enum") {
                    out << " : " << pn;
                }
            }
            out << " {\n";

            for (auto& [ff, ft, fn, off] : cls.GetFields()) {
                std::string acc = Utils::AccessModifier(ff);
                std::string mods = (ff & 0x0010) ? "static " : "";
                out << "        " << acc << " " << mods << ft << " " << fn << "; // 0x" << std::hex << off << std::dec << "\n";
            }

            for (auto& [mf, rt, mn, ps] : cls.GetMethods()) {
                std::string acc = Utils::AccessModifier(mf);
                std::string mods = (mf & 0x0010) ? "static " : "";
                out << "        " << acc << " " << mods << rt << " " << mn << "(";
                for (size_t j = 0; j < ps.size(); j++) {
                    if (j > 0) out << ", ";
                    out << ps[j].first << " " << ps[j].second;
                }
                out << ") { }\n";
            }

            out << "    }\n\n";
        }

        if (!ns.empty()) out << "}\n\n";
    }

    Log("  -> " + safeName + ext + " [C#]");
}

void Dumper::ExportHuman() {
    Utils::CreateDir("C:\\IL2CPP_Dump");
    int total = (int)images.size();
    for (int i = 0; i < total; i++) {
        Progress(i + 1, total, images[i].GetName());
        Log("Exporting: " + images[i].GetName());
        ExportAssembly(images[i], OutputFormat::CSharp);
    }
    Log("\nOutput: C:\\IL2CPP_Dump\\");
}

void Dumper::ExportAI() {
    Utils::CreateDir("C:\\IL2CPP_Dump_JSON");
    Utils::CreateDir("C:\\IL2CPP_Dump_Summary");

    std::vector<IL2CPP_Image*> filtered;
    for (auto& img : images) {
        if (!ShouldSkipAssembly(img.GetName())) {
            filtered.push_back(&img);
        }
    }

    int total = (int)filtered.size();
    for (int i = 0; i < total; i++) {
        Progress(i + 1, total, filtered[i]->GetName());
        Log("Exporting: " + filtered[i]->GetName());
        ExportAssembly(*filtered[i], OutputFormat::JsonFull);
        ExportAssembly(*filtered[i], OutputFormat::JsonSummary);
    }
    Log("\nOutput: C:\\IL2CPP_Dump_JSON\\ + C:\\IL2CPP_Dump_Summary\\");
}

void Dumper::ExportCustom(bool cs, bool json, bool summary) {
    if (cs) Utils::CreateDir("C:\\IL2CPP_Dump");
    if (json) Utils::CreateDir("C:\\IL2CPP_Dump_JSON");
    if (summary) Utils::CreateDir("C:\\IL2CPP_Dump_Summary");

    std::vector<IL2CPP_Image*> filtered;
    for (auto& img : images) {
        if (!ShouldSkipAssembly(img.GetName())) {
            filtered.push_back(&img);
        }
    }

    int steps = 0;
    if (cs) steps += (int)images.size();
    if (json) steps += (int)filtered.size();
    if (summary) steps += (int)filtered.size();

    int current = 0;

    if (cs) {
        Log("--- C# ---");
        for (auto& img : images) {
            Progress(++current, steps, img.GetName());
            Log("Exporting: " + img.GetName());
            ExportAssembly(img, OutputFormat::CSharp);
        }
    }

    if (json) {
        Log("\n--- JSON Full ---");
        for (auto* img : filtered) {
            Progress(++current, steps, img->GetName());
            Log("Exporting: " + img->GetName());
            ExportAssembly(*img, OutputFormat::JsonFull);
        }
    }

    if (summary) {
        Log("\n--- JSON Summary ---");
        for (auto* img : filtered) {
            Progress(++current, steps, img->GetName());
            Log("Exporting: " + img->GetName());
            ExportAssembly(*img, OutputFormat::JsonSummary);
        }
    }
}
