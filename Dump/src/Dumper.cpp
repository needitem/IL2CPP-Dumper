#include "../include/Dumper.h"
#include "../include/IL2CPP_API.h"
#include "../include/Mono_API.h"
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

    std::string baseDir = Utils::GetGameDir();
    std::string folder, ext;
    switch (format) {
        case OutputFormat::CSharp:
            folder = baseDir + "IL2CPP_Dump\\";
            ext = ".cs";
            break;
        case OutputFormat::JsonFull:
            folder = baseDir + "IL2CPP_Dump_JSON\\";
            ext = ".json";
            break;
        case OutputFormat::JsonSummary:
            folder = baseDir + "IL2CPP_Dump_Summary\\";
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
    std::string baseDir = Utils::GetGameDir();
    Utils::CreateDir(baseDir + "IL2CPP_Dump");
    int total = (int)images.size();
    for (int i = 0; i < total; i++) {
        Progress(i + 1, total, images[i].GetName());
        Log("Exporting: " + images[i].GetName());
        ExportAssembly(images[i], OutputFormat::CSharp);
    }
    Log("\nOutput: " + baseDir + "IL2CPP_Dump\\");
}

void Dumper::ExportAI() {
    std::string baseDir = Utils::GetGameDir();
    Utils::CreateDir(baseDir + "IL2CPP_Dump_JSON");
    Utils::CreateDir(baseDir + "IL2CPP_Dump_Summary");

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
    Log("\nOutput: " + baseDir + "IL2CPP_Dump_JSON\\ + " + baseDir + "IL2CPP_Dump_Summary\\");
}

void Dumper::ExportCustom(bool cs, bool json, bool summary) {
    std::string baseDir = Utils::GetGameDir();
    if (cs) Utils::CreateDir(baseDir + "IL2CPP_Dump");
    if (json) Utils::CreateDir(baseDir + "IL2CPP_Dump_JSON");
    if (summary) Utils::CreateDir(baseDir + "IL2CPP_Dump_Summary");

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

std::vector<std::string> Dumper::ScanMonoAssemblies() {
    std::vector<std::string> names;
    if (!Mono::Initialize()) {
        Log("[!] Mono API unavailable - no module exports mono_get_root_domain");
        const std::string& diag = Mono::GetDiagLog();
        if (!diag.empty()) Log(diag);
        Log("    -> Enter game world fully, then retry");
        return names;
    }
    Log("[+] Mono API initialized");
    void* domain = Mono::GetRootDomain();
    if (!domain) {
        Log("[!] Failed to get Mono root domain");
        return names;
    }
    Mono::ForEachAssembly(domain, [&](void* assembly) {
        if (!assembly) return;
        void* image = Mono::AssemblyGetImage(assembly);
        if (!image) return;
        const char* name = Mono::ImageGetName(image);
        if (name && *name) names.push_back(name);
    });
    return names;
}

void Dumper::ExportMonoFromMemory() {
    std::string baseDir = Utils::GetGameDir();
    std::string outDir  = baseDir + "Mono_Dump_Raw\\";
    Utils::CreateDir(outDir);

    Log("Scanning process memory for .NET assemblies...");
    Log("(looking for MZ+PE+CLR headers in private/mapped memory)");

    SYSTEM_INFO si;
    GetSystemInfo(&si);

    int scanned = 0, found = 0;
    auto* addr    = (uint8_t*)si.lpMinimumApplicationAddress;
    auto* maxAddr = (uint8_t*)si.lpMaximumApplicationAddress;

    while (addr < maxAddr) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) != sizeof(mbi)) break;
        addr = (uint8_t*)mbi.BaseAddress + mbi.RegionSize;

        if (mbi.State  != MEM_COMMIT) continue;
        if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) continue;
        // MEM_IMAGE = 정상 로드된 DLL → 건너뜀, MEM_PRIVATE/MEM_MAPPED = 인메모리 로드
        if (mbi.Type == MEM_IMAGE) continue;
        if (mbi.RegionSize < 0x200) continue;

        auto* base = (uint8_t*)mbi.BaseAddress;
        size_t size = mbi.RegionSize;

        // 4KB 단위로 MZ 헤더 탐색
        for (size_t off = 0; off + 0x100 <= size; off += 0x1000) {
            uint8_t* p = base + off;
            if (p[0] != 'M' || p[1] != 'Z') continue;

            // PE 오프셋 검증
            DWORD peOff = *(DWORD*)(p + 0x3C);
            if (peOff < 0x40 || off + peOff + 0x18 > size) continue;
            if (*(DWORD*)(p + peOff) != 0x00004550) continue; // "PE\0\0"

            // Optional header magic
            WORD magic = *(WORD*)(p + peOff + 0x18);
            if (magic != 0x010B && magic != 0x020B) continue;

            // DataDirectory[14] = COM Descriptor (.NET header)
            int ddOff = (magic == 0x020B) ? (0x18 + 0x70) : (0x18 + 0x60);
            if (off + peOff + ddOff + 14 * 8 + 4 > size) continue;
            DWORD clrRva = *(DWORD*)(p + peOff + ddOff + 14 * 8);
            if (clrRva == 0) continue; // native PE, .NET 아님

            // .NET 어셈블리 발견
            scanned++;
            DWORD imgSize = *(DWORD*)(p + peOff + 0x18 + 0x38); // SizeOfImage
            if (imgSize == 0 || imgSize > 64 * 1024 * 1024) imgSize = (DWORD)(size - off);
            if (off + imgSize > size) imgSize = (DWORD)(size - off);

            char fname[64];
            sprintf_s(fname, "mem_%016llX.dll", (unsigned long long)(uintptr_t)p);
            std::ofstream f(outDir + fname, std::ios::binary);
            if (f.is_open()) {
                f.write((char*)p, imgSize);
                f.close();
                found++;
                char msg[128];
                sprintf_s(msg, "  [+] %s  (%u KB)", fname, imgSize / 1024);
                Log(msg);
                Progress(found, found + 5, fname);
            }
            off += 0xFFF; // 이 영역은 건너뜀 (다음 4KB로)
        }
    }

    char buf[256];
    sprintf_s(buf, "\n[+] Found %d .NET assemblies in memory", found);
    Log(buf);
    if (found > 0) {
        Log("Output: " + outDir);
        Log("Tip: Open .dll files with dnSpy or ILSpy to view contents");
    } else {
        Log("[!] None found - game may not have loaded assemblies yet");
        Log("    Try: enter game world -> fishing area -> then dump");
    }
}

void Dumper::ExportMono(const std::vector<std::string>& include) {
    if (!Mono::Initialize()) {
        Log("[!] Mono API unavailable (no mono_get_root_domain export found)");
        Log("[*] Falling back to memory scan...");
        Log("");
        ExportMonoFromMemory();
        return;
    }
    Log("[+] Mono API initialized");

    void* domain = Mono::GetRootDomain();
    if (!domain) {
        Log("[!] Failed to get Mono root domain");
        return;
    }

    std::string baseDir = Utils::GetGameDir();
    std::string outDir  = baseDir + "Mono_Dump_JSON\\";
    Utils::CreateDir(outDir);

    int assemblyCount = 0;
    int exportedCount = 0;

    Mono::ForEachAssembly(domain, [&](void* assembly) {
        if (!assembly) return;
        void* image = Mono::AssemblyGetImage(assembly);
        if (!image) return;

        const char* rawName = Mono::ImageGetName(image);
        if (!rawName || !*rawName) return;
        assemblyCount++;

        std::string asmName(rawName);

        // include 목록이 있으면 키워드 매칭, 없으면 ShouldSkipAssembly 기본 필터 적용
        if (!include.empty()) {
            std::string lowerName = asmName;
            for (char& c : lowerName) c = (char)tolower((unsigned char)c);
            bool matched = false;
            for (const auto& kw : include) {
                std::string lowerKw = kw;
                for (char& c : lowerKw) c = (char)tolower((unsigned char)c);
                if (lowerName.find(lowerKw) != std::string::npos) { matched = true; break; }
            }
            if (!matched) return;
        } else {
            if (ShouldSkipAssembly(asmName)) return;
        }

        Log("Exporting (Mono): " + asmName);

        std::string safeName = asmName;
        for (char& c : safeName) { if (c == '.' || c == '-') c = '_'; }

        std::ofstream out(outDir + safeName + ".json");
        if (!out.is_open()) {
            Log("  [ERROR] Cannot write: " + outDir + safeName + ".json");
            return;
        }

        // TYPEDEF table = 2, rows are 1-based
        int rowCount = Mono::ImageGetTableRows(image, 2);

        out << "{\n";
        out << "  \"assembly\": \"" << EscapeJson(asmName) << "\",\n";
        out << "  \"classes\": [\n";

        bool firstClass = true;
        for (int row = 1; row <= rowCount; row++) {
            uint32_t token = 0x02000000 | (uint32_t)row;
            void* klass = Mono::ClassGet(image, token);
            if (!klass) continue;

            const char* cn  = Mono::ClassGetName(klass);
            const char* cns = Mono::ClassGetNamespace(klass);
            if (!cn || !*cn) continue;

            std::string name(cn);
            std::string ns(cns ? cns : "");
            if (ShouldSkipClass(name)) continue;

            if (!firstClass) out << ",\n";
            firstClass = false;

            std::string type = Mono::ClassIsInterface(klass) ? "interface"
                             : Mono::ClassIsValueType(klass)  ? "struct"
                             : "class";
            std::string fullName = ns.empty() ? name : ns + "." + name;

            out << "    {\n";
            out << "      \"name\": \""     << EscapeJson(name)     << "\",\n";
            out << "      \"fullName\": \"" << EscapeJson(fullName) << "\",\n";
            out << "      \"type\": \""     << type                 << "\"";

            void* parent = Mono::ClassGetParent(klass);
            if (parent) {
                const char* pn = Mono::ClassGetName(parent);
                if (pn && *pn && std::string(pn) != "Object"
                              && std::string(pn) != "ValueType"
                              && std::string(pn) != "Enum") {
                    out << ",\n      \"extends\": \"" << EscapeJson(pn) << "\"";
                }
            }

            // Fields
            out << ",\n      \"fields\": [";
            bool firstField = true;
            void* fiter = nullptr;
            void* field = nullptr;
            while ((field = Mono::ClassGetFields(klass, &fiter)) != nullptr) {
                const char* fn = Mono::FieldGetName(field);
                if (!fn || !*fn) continue;
                uint32_t ff = Mono::FieldGetFlags(field);
                if (ShouldSkipMember(ff)) continue;

                void* ftype = Mono::FieldGetType(field);
                std::string typeName;
                if (ftype) {
                    char* tn = Mono::TypeGetName(ftype);
                    if (tn) { typeName = tn; Mono::MonoFree(tn); }
                }
                int32_t offset = Mono::FieldGetOffset(field);

                if (!firstField) out << ",";
                firstField = false;
                out << "\n        {\"name\": \"" << EscapeJson(fn)
                    << "\", \"type\": \"" << EscapeJson(typeName)
                    << "\", \"access\": \"" << Utils::AccessModifier(ff)
                    << "\", \"offset\": \"0x" << std::hex << offset << std::dec << "\"}";
            }
            out << "]";

            // Methods
            out << ",\n      \"methods\": [";
            bool firstMethod = true;
            void* miter = nullptr;
            void* method = nullptr;
            while ((method = Mono::ClassGetMethods(klass, &miter)) != nullptr) {
                const char* mn = Mono::MethodGetName(method);
                if (!mn || !*mn) continue;
                uint32_t iflags = 0;
                uint32_t mf = Mono::MethodGetFlags(method, &iflags);
                if (ShouldSkipMember(mf)) continue;

                std::string retType;
                std::vector<std::pair<std::string,std::string>> params;

                void* sig = Mono::MethodGetSignature(method);
                if (sig) {
                    void* rt = Mono::SignatureGetReturnType(sig);
                    if (rt) {
                        char* rtn = Mono::TypeGetName(rt);
                        if (rtn) { retType = rtn; Mono::MonoFree(rtn); }
                    }
                    uint32_t pc = Mono::SignatureGetParamCount(sig);
                    void* piter = nullptr;
                    for (uint32_t pi = 0; pi < pc; pi++) {
                        void* pt = Mono::SignatureGetParams(sig, &piter);
                        std::string ptName;
                        if (pt) {
                            char* ptn = Mono::TypeGetName(pt);
                            if (ptn) { ptName = ptn; Mono::MonoFree(ptn); }
                        }
                        const char* pn = Mono::MethodGetParamName(method, (int)pi);
                        params.push_back({ ptName, pn ? pn : "" });
                    }
                }

                if (!firstMethod) out << ",";
                firstMethod = false;
                out << "\n        {\"name\": \"" << EscapeJson(mn)
                    << "\", \"returns\": \"" << EscapeJson(retType) << "\"";
                if (!params.empty()) {
                    out << ", \"params\": [";
                    for (size_t j = 0; j < params.size(); j++) {
                        if (j > 0) out << ", ";
                        out << "{\"type\": \"" << EscapeJson(params[j].first)
                            << "\", \"name\": \"" << EscapeJson(params[j].second) << "\"}";
                    }
                    out << "]";
                }
                out << ", \"access\": \"" << Utils::AccessModifier(mf) << "\"}";
            }
            out << "]\n";
            out << "    }";
        }

        out << "\n  ]\n}\n";
        exportedCount++;
    });

    char buf[128];
    sprintf_s(buf, "\n[+] Mono: %d assemblies found, %d exported", assemblyCount, exportedCount);
    Log(buf);
    Log("Output: " + outDir);
}
