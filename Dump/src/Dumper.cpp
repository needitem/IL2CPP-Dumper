#include "../include/Dumper.h"
#include "../include/IL2CPP_API.h"
#include "../include/Mono_API.h"
#include "../include/Utils.h"
#include <Windows.h>
#include <cstring>
#include <fstream>
#include <map>
#include <algorithm>

namespace {

// ---- ECMA-335 attribute bits shared by IL2CPP and Mono metadata ----------
constexpr uint32_t kMemberStatic   = 0x0010; // method + field
constexpr uint32_t kFieldLiteral   = 0x0040;
constexpr uint32_t kMethodVirtual  = 0x0040;
constexpr uint32_t kMethodAbstract = 0x0400;
constexpr uint32_t kTypeAbstract   = 0x0080;
constexpr uint32_t kTypeSealed     = 0x0100;

// ELEMENT_TYPE codes for the integer-like primitives we can print as numbers
constexpr int kTypeBool = 0x02, kTypeI1 = 0x04, kTypeU1 = 0x05, kTypeI2 = 0x06, kTypeU2 = 0x07,
              kTypeI4 = 0x08, kTypeU4 = 0x09, kTypeI8 = 0x0a, kTypeU8 = 0x0b, kTypeChar = 0x03;

std::string JoinNs(const std::string& ns, const std::string& name) {
    return ns.empty() ? name : ns + "." + name;
}

bool IsImplicitBase(const std::string& fullName) {
    return fullName == "System.Object" || fullName == "System.ValueType" || fullName == "System.Enum";
}

std::string SafeFileName(std::string s) {
    std::replace(s.begin(), s.end(), '.', '_');
    std::replace(s.begin(), s.end(), '-', '_');
    return s;
}

// Formats an integer-like literal (enum member / const) read into buf.
bool FormatLiteral(int code, const unsigned char* buf, std::string& out) {
    switch (code) {
        case kTypeBool:
        case kTypeU1:   out = std::to_string((unsigned)buf[0]); return true;
        case kTypeI1:   out = std::to_string((int)(signed char)buf[0]); return true;
        case kTypeChar:
        case kTypeU2:   { uint16_t v; memcpy(&v, buf, 2); out = std::to_string((unsigned)v); return true; }
        case kTypeI2:   { int16_t v;  memcpy(&v, buf, 2); out = std::to_string((int)v);      return true; }
        case kTypeI4:   { int32_t v;  memcpy(&v, buf, 4); out = std::to_string(v);           return true; }
        case kTypeU4:   { uint32_t v; memcpy(&v, buf, 4); out = std::to_string(v);           return true; }
        case kTypeI8:   { int64_t v;  memcpy(&v, buf, 8); out = std::to_string(v);           return true; }
        case kTypeU8:   { uint64_t v; memcpy(&v, buf, 8); out = std::to_string(v);           return true; }
    }
    return false;
}

// Writes one assembly as one or more JSON files (one class per line), splitting
// after `chunk` classes so a huge Assembly-CSharp never becomes a single blob.
class AssemblyJsonWriter {
public:
    AssemblyJsonWriter(std::string dir, std::string safeName, std::string asmName,
                       std::string runtime, size_t chunk)
        : dir_(std::move(dir)), safe_(std::move(safeName)), asm_(std::move(asmName)),
          runtime_(std::move(runtime)), chunk_(chunk) {
        entry_.assembly = asm_;
        Open();
    }
    ~AssemblyJsonWriter() { Close(); }

    bool ok() const { return !entry_.files.empty(); }

    void Add(const ClassData& c, bool summary) {
        if (!out_.is_open()) return;
        if (chunk_ > 0 && inFile_ >= chunk_) {
            Close();
            Open();
            if (!out_.is_open()) return;
        }
        if (inFile_ > 0) out_ << ",\n";
        out_ << Json::SerializeClass(c, summary);
        inFile_++;
        entry_.classCount++;
        entry_.namespaces[c.ns]++;
    }

    IndexEntry Finish() {
        Close();
        return entry_;
    }

private:
    void Open() {
        part_++;
        std::string fn = (part_ == 1) ? safe_ + ".json" : safe_ + ".part" + std::to_string(part_) + ".json";
        out_.open(dir_ + fn, std::ios::binary);
        if (!out_.is_open()) return;
        entry_.files.push_back(fn);
        out_ << "{\"schemaVersion\":" << kSchemaVersion
             << ",\"runtime\":\"" << runtime_ << "\""
             << ",\"assembly\":\"" << Json::Escape(asm_) << "\""
             << ",\"part\":" << part_ << ",\"classes\":[\n";
        inFile_ = 0;
    }

    void Close() {
        if (out_.is_open()) {
            out_ << "\n]}\n";
            out_.close();
        }
    }

    std::string dir_, safe_, asm_, runtime_;
    size_t chunk_;
    std::ofstream out_;
    int part_ = 0;
    size_t inFile_ = 0;
    IndexEntry entry_;
};

std::string JoinFiles(const std::vector<std::string>& files) {
    std::string s;
    for (size_t i = 0; i < files.size(); i++) {
        if (i) s += ", ";
        s += files[i];
    }
    return s;
}

// ---- IL2CPP -> ClassData ------------------------------------------------

ClassData CollectIl2cppClass(const IL2CPP_Class& cls, const Dumper& dumper) {
    ClassData d;
    void* h = cls.handle;
    d.name = cls.GetName();
    d.ns = cls.GetNamespace();

    const bool isEnum = IL2CPP::ClassIsEnum(h);
    d.kind = cls.IsInterface() ? "interface" : isEnum ? "enum" : cls.IsValueType() ? "struct" : "class";
    d.hasToken = true;
    d.token = cls.GetToken();

    if (d.kind != "interface") {
        uint32_t cf = IL2CPP::ClassGetFlags(h);
        d.isAbstract = (cf & kTypeAbstract) != 0;
        d.isSealed = (cf & kTypeSealed) != 0;
    }

    auto parent = cls.GetParent();
    if (parent.handle) {
        std::string full = JoinNs(parent.GetNamespace(), parent.GetName());
        if (!IsImplicitBase(full)) d.parent = full;
    }
    for (auto& iface : cls.GetInterfaces()) {
        d.interfaces.push_back(JoinNs(iface.GetNamespace(), iface.GetName()));
    }

    int enumCode = 0;
    if (isEnum) {
        void* bt = IL2CPP::ClassEnumBaseType(h);
        if (bt) {
            const char* n = IL2CPP::TypeGetName(bt);
            if (n) d.underlying = n;
            enumCode = IL2CPP::TypeGetTypeCode(bt);
        }
    }

    // Fields
    void* iter = nullptr;
    while (void* f = IL2CPP::ClassGetFields(h, &iter)) {
        const char* fname = IL2CPP::FieldGetName(f);
        if (!fname || !*fname) continue;
        uint32_t ff = IL2CPP::FieldGetFlags(f);
        if (isEnum && !(ff & kFieldLiteral)) continue; // value__ is reported as "underlying"
        if (dumper.ShouldSkipMember(ff)) continue;

        FieldData fd;
        fd.name = fname;
        void* ft = IL2CPP::FieldGetType(f);
        const char* tname = IL2CPP::TypeGetName(ft);
        fd.type = tname ? tname : "?";
        fd.access = Utils::AccessModifier(ff);
        fd.isStatic = (ff & kMemberStatic) != 0;
        fd.isConst = (ff & kFieldLiteral) != 0;

        if (fd.isConst) {
            int code = isEnum ? enumCode : IL2CPP::TypeGetTypeCode(ft);
            unsigned char buf[8];
            if (IL2CPP::FieldStaticGetValue(f, buf, sizeof(buf)))
                fd.hasValue = FormatLiteral(code, buf, fd.value);
        } else {
            fd.hasOffset = true;
            fd.offset = IL2CPP::FieldGetOffset(f);
        }
        d.fields.push_back(std::move(fd));
    }

    // Properties
    iter = nullptr;
    while (void* p = IL2CPP::ClassGetProperties(h, &iter)) {
        const char* pname = IL2CPP::PropertyGetName(p);
        if (!pname || !*pname) continue;
        void* getter = IL2CPP::PropertyGetGetMethod(p);
        void* setter = IL2CPP::PropertyGetSetMethod(p);
        void* any = getter ? getter : setter;
        if (!any) continue;
        uint32_t mf = IL2CPP::MethodGetFlags(any);
        if (dumper.ShouldSkipMember(mf)) continue;

        PropertyData pd;
        pd.name = pname;
        pd.access = Utils::AccessModifier(mf);
        pd.isStatic = (mf & kMemberStatic) != 0;
        pd.hasGet = getter != nullptr;
        pd.hasSet = setter != nullptr;
        void* type = nullptr;
        if (getter) {
            type = IL2CPP::MethodGetReturnType(getter);
        } else {
            uint32_t pc = IL2CPP::MethodGetParamCount(setter);
            if (pc > 0) type = IL2CPP::MethodGetParam(setter, pc - 1); // value is the last param
        }
        const char* tn = type ? IL2CPP::TypeGetName(type) : nullptr;
        pd.type = tn ? tn : "?";
        d.properties.push_back(std::move(pd));
    }

    // Methods
    iter = nullptr;
    while (void* m = IL2CPP::ClassGetMethods(h, &iter)) {
        const char* mname = IL2CPP::MethodGetName(m);
        if (!mname || !*mname) continue;
        uint32_t mf = IL2CPP::MethodGetFlags(m);
        if (dumper.ShouldSkipMember(mf)) continue;

        MethodData md;
        md.name = mname;
        const char* rt = IL2CPP::TypeGetName(IL2CPP::MethodGetReturnType(m));
        md.returns = (rt && *rt) ? rt : "void";
        md.access = Utils::AccessModifier(mf);
        md.isStatic = (mf & kMemberStatic) != 0;
        md.isAbstract = (mf & kMethodAbstract) != 0;
        md.isVirtual = (mf & kMethodVirtual) != 0;
        md.hasRva = IL2CPP::MethodGetRva(m, &md.rva);

        uint32_t pc = IL2CPP::MethodGetParamCount(m);
        for (uint32_t i = 0; i < pc; i++) {
            const char* pt = IL2CPP::TypeGetName(IL2CPP::MethodGetParam(m, i));
            const char* pn = IL2CPP::MethodGetParamName(m, i);
            md.params.push_back({ pt ? pt : "?", pn ? pn : ("arg" + std::to_string(i)) });
        }
        d.methods.push_back(std::move(md));
    }
    return d;
}

// ---- Mono -> ClassData --------------------------------------------------

std::string MonoTypeName(void* type) {
    std::string s;
    if (!type) return s;
    char* tn = Mono::TypeGetName(type);
    if (tn) {
        s = tn;
        Mono::MonoFree(tn);
    }
    return s;
}

std::string MonoClassFullName(void* klass) {
    const char* n = Mono::ClassGetName(klass);
    const char* ns = Mono::ClassGetNamespace(klass);
    return JoinNs(ns ? ns : "", n ? n : "");
}

struct MonoSig {
    std::string ret;
    std::vector<ParamData> params;
    std::string lastParamType;
};

MonoSig ReadMonoSig(void* method) {
    MonoSig s;
    void* sig = Mono::MethodGetSignature(method);
    if (!sig) return s;
    s.ret = MonoTypeName(Mono::SignatureGetReturnType(sig));
    uint32_t pc = Mono::SignatureGetParamCount(sig);
    void* piter = nullptr;
    for (uint32_t i = 0; i < pc; i++) {
        void* pt = Mono::SignatureGetParams(sig, &piter);
        const char* pn = Mono::MethodGetParamName(method, (int)i);
        s.params.push_back({ MonoTypeName(pt), (pn && *pn) ? pn : "arg" + std::to_string(i) });
    }
    if (!s.params.empty()) s.lastParamType = s.params.back().type;
    return s;
}

ClassData CollectMonoClass(void* klass, const Dumper& dumper) {
    ClassData d;
    const char* cn = Mono::ClassGetName(klass);
    const char* cns = Mono::ClassGetNamespace(klass);
    d.name = cn ? cn : "";
    d.ns = cns ? cns : "";

    const bool isEnum = Mono::ClassIsEnum(klass);
    d.kind = Mono::ClassIsInterface(klass) ? "interface" : isEnum ? "enum"
           : Mono::ClassIsValueType(klass) ? "struct" : "class";

    if (d.kind != "interface") {
        uint32_t cf = Mono::ClassGetFlags(klass);
        d.isAbstract = (cf & kTypeAbstract) != 0;
        d.isSealed = (cf & kTypeSealed) != 0;
    }

    if (void* parent = Mono::ClassGetParent(klass)) {
        std::string full = MonoClassFullName(parent);
        if (!IsImplicitBase(full)) d.parent = full;
    }
    void* iter = nullptr;
    while (void* iface = Mono::ClassGetInterfaces(klass, &iter)) {
        d.interfaces.push_back(MonoClassFullName(iface));
    }

    // Fields
    iter = nullptr;
    while (void* field = Mono::ClassGetFields(klass, &iter)) {
        const char* fn = Mono::FieldGetName(field);
        if (!fn || !*fn) continue;
        uint32_t ff = Mono::FieldGetFlags(field);
        if (isEnum && !(ff & kFieldLiteral)) {
            d.underlying = MonoTypeName(Mono::FieldGetType(field)); // value__
            continue;
        }
        if (dumper.ShouldSkipMember(ff)) continue;

        FieldData fd;
        fd.name = fn;
        fd.type = MonoTypeName(Mono::FieldGetType(field));
        fd.access = Utils::AccessModifier(ff);
        fd.isStatic = (ff & kMemberStatic) != 0;
        fd.isConst = (ff & kFieldLiteral) != 0;
        if (!fd.isConst) {
            fd.hasOffset = true;
            fd.offset = Mono::FieldGetOffset(field);
        }
        d.fields.push_back(std::move(fd));
    }

    // Properties
    iter = nullptr;
    while (void* p = Mono::ClassGetProperties(klass, &iter)) {
        const char* pname = Mono::PropertyGetName(p);
        if (!pname || !*pname) continue;
        void* getter = Mono::PropertyGetGetMethod(p);
        void* setter = Mono::PropertyGetSetMethod(p);
        void* any = getter ? getter : setter;
        if (!any) continue;
        uint32_t iflags = 0;
        uint32_t mf = Mono::MethodGetFlags(any, &iflags);
        if (dumper.ShouldSkipMember(mf)) continue;

        PropertyData pd;
        pd.name = pname;
        pd.access = Utils::AccessModifier(mf);
        pd.isStatic = (mf & kMemberStatic) != 0;
        pd.hasGet = getter != nullptr;
        pd.hasSet = setter != nullptr;
        pd.type = getter ? ReadMonoSig(getter).ret : ReadMonoSig(setter).lastParamType;
        d.properties.push_back(std::move(pd));
    }

    // Methods
    iter = nullptr;
    while (void* m = Mono::ClassGetMethods(klass, &iter)) {
        const char* mn = Mono::MethodGetName(m);
        if (!mn || !*mn) continue;
        uint32_t iflags = 0;
        uint32_t mf = Mono::MethodGetFlags(m, &iflags);
        if (dumper.ShouldSkipMember(mf)) continue;

        MethodData md;
        md.name = mn;
        MonoSig sig = ReadMonoSig(m);
        md.returns = sig.ret;
        md.params = std::move(sig.params);
        md.access = Utils::AccessModifier(mf);
        md.isStatic = (mf & kMemberStatic) != 0;
        md.isAbstract = (mf & kMethodAbstract) != 0;
        md.isVirtual = (mf & kMethodVirtual) != 0;
        d.methods.push_back(std::move(md));
    }
    return d;
}

} // namespace

Dumper::Dumper() {
    const Utils::Config& cfg = Utils::GetConfig();
    if (cfg.skipUnity >= 0)             filters.skipUnityEngine = cfg.skipUnity != 0;
    if (cfg.skipSystem >= 0)            filters.skipSystem = cfg.skipSystem != 0;
    if (cfg.skipPrivate >= 0)           filters.skipPrivate = cfg.skipPrivate != 0;
    if (cfg.skipCompilerGenerated >= 0) filters.skipCompilerGenerated = cfg.skipCompilerGenerated != 0;
    filters.jsonChunkSize = cfg.chunkSize;

    IL2CPP::Initialize();
    if (!IL2CPP::Initialized) return;

    constexpr int kWaitTimeoutMs = 15000;
    constexpr int kStepMs = 200;

    for (int waited = 0; waited <= kWaitTimeoutMs; waited += kStepMs) {
        void* domain = IL2CPP::GetDomain();
        if (!domain) {
            Sleep(kStepMs);
            continue;
        }

        size_t count = 0;
        void** assemblies = IL2CPP::GetAssemblies(domain, &count);
        if (!assemblies || count == 0) {
            Sleep(kStepMs);
            continue;
        }

        for (size_t i = 0; i < count; i++) {
            void* assembly = assemblies[i];
            if (!assembly) continue;

            void* image = IL2CPP::AssemblyGetImage(assembly);
            if (!image) continue;

            const char* name = IL2CPP::ImageGetName(image);
            if (!name || !*name) continue;

            images.emplace_back(image);
        }

        if (!images.empty()) return;
        Sleep(kStepMs);
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

void Dumper::WriteIndexes() {
    std::string baseDir = Utils::GetGameDir();
    if (!indexFull_.empty()) {
        Json::WriteIndexFile(baseDir + "IL2CPP_Dump_JSON\\index.json", "il2cpp", "full", indexFull_);
    }
    if (!indexSummary_.empty()) {
        Json::WriteIndexFile(baseDir + "IL2CPP_Dump_Summary\\index.json", "il2cpp", "summary", indexSummary_);
    }
    indexFull_.clear();
    indexSummary_.clear();
}

void Dumper::ExportAssembly(const IL2CPP_Image& img, OutputFormat format) {
    std::string asmName = img.GetName();
    std::string safeName = SafeFileName(asmName);
    std::string baseDir = Utils::GetGameDir();

    // JSON output (Full / Summary)
    if (format == OutputFormat::JsonFull || format == OutputFormat::JsonSummary) {
        const bool isSummary = (format == OutputFormat::JsonSummary);
        std::string folder = baseDir + (isSummary ? "IL2CPP_Dump_Summary\\" : "IL2CPP_Dump_JSON\\");
        Utils::CreateDir(folder);

        AssemblyJsonWriter writer(folder, safeName, asmName, "il2cpp",
                                  filters.jsonChunkSize > 0 ? (size_t)filters.jsonChunkSize : 0);
        if (!writer.ok()) {
            Log("  [ERROR] Cannot write: " + folder + safeName + ".json");
            return;
        }

        for (size_t i = 0; i < img.GetClassCount(); i++) {
            auto cls = img.GetClass(i);
            if (!cls.handle) continue;

            std::string name = cls.GetName();
            if (name.find('<') != std::string::npos) continue;
            if (ShouldSkipClass(name)) continue;

            writer.Add(CollectIl2cppClass(cls, *this), isSummary);
        }

        IndexEntry entry = writer.Finish();
        Log("  -> " + JoinFiles(entry.files) + (isSummary ? " [Summary]" : " [Full]"));
        (isSummary ? indexSummary_ : indexFull_).push_back(std::move(entry));
        return;
    }

    // C# output
    std::string folder = baseDir + "IL2CPP_Dump\\";
    const std::string ext = ".cs";
    Utils::CreateDir(folder);
    std::ofstream out(folder + safeName + ext);
    if (!out.is_open()) {
        Log("  [ERROR] Cannot write: " + folder + safeName + ext);
        return;
    }

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
    WriteIndexes();
    Log("\nOutput: " + baseDir + "IL2CPP_Dump_JSON\\ + " + baseDir + "IL2CPP_Dump_Summary\\");
    Log("Start with index.json in each folder (assembly -> files -> namespaces).");
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
    WriteIndexes();
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

            // PE offset validation
            DWORD peOffset = *(DWORD*)(p + 0x3C);
            if (peOffset < 0x40 || off + peOffset + 0x18 > size) continue;
            if (*(DWORD*)(p + peOffset) != 0x00004550) continue; // "PE\0\0"

            // Optional header magic
            WORD magic = *(WORD*)(p + peOffset + 0x18);
            if (magic != 0x010B && magic != 0x020B) continue;

            // DataDirectory[14] = COM Descriptor (.NET header)
            int ddOff = (magic == 0x020B) ? (0x18 + 0x70) : (0x18 + 0x60);
            if (off + peOffset + ddOff + 14 * 8 + 4 > size) continue;
            DWORD clrRva = *(DWORD*)(p + peOffset + ddOff + 14 * 8);
            if (clrRva == 0) continue; // native PE, .NET 아님

            // .NET 어셈블리 발견
            scanned++;
            DWORD imgSize = *(DWORD*)(p + peOffset + 0x18 + 0x38); // SizeOfImage
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
        Log("    Try again after runtime/assemblies are fully loaded");
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
    std::vector<IndexEntry> index;

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

        std::string safeName = SafeFileName(asmName);
        AssemblyJsonWriter writer(outDir, safeName, asmName, "mono",
                                  filters.jsonChunkSize > 0 ? (size_t)filters.jsonChunkSize : 0);
        if (!writer.ok()) {
            Log("  [ERROR] Cannot write: " + outDir + safeName + ".json");
            return;
        }

        // TYPEDEF table = 2, rows are 1-based
        int rowCount = Mono::ImageGetTableRows(image, 2);
        for (int row = 1; row <= rowCount; row++) {
            uint32_t token = 0x02000000 | (uint32_t)row;
            void* klass = Mono::ClassGet(image, token);
            if (!klass) continue;

            const char* cn = Mono::ClassGetName(klass);
            if (!cn || !*cn) continue;
            std::string name(cn);
            if (name.find('<') != std::string::npos) continue;
            if (ShouldSkipClass(name)) continue;

            ClassData cd = CollectMonoClass(klass, *this);
            cd.hasToken = true;
            cd.token = token;
            writer.Add(cd, false);
        }

        IndexEntry entry = writer.Finish();
        Log("  -> " + JoinFiles(entry.files));
        index.push_back(std::move(entry));
        exportedCount++;
    });

    if (!index.empty()) Json::WriteIndexFile(outDir + "index.json", "mono", "full", index);

    char buf[128];
    sprintf_s(buf, "\n[+] Mono: %d assemblies found, %d exported", assemblyCount, exportedCount);
    Log(buf);
    Log("Output: " + outDir);
}
