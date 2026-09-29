#pragma once

// Runtime-independent description of dumped types plus the JSON serializer.
// Both the IL2CPP and the Mono exporters fill these structs, so the JSON
// layout lives in exactly one place. No Windows headers here on purpose:
// this module is unit-tested off-Windows (see tests/).

#include <cstdint>
#include <map>
#include <string>
#include <vector>

constexpr int kSchemaVersion = 2;

struct ParamData {
    std::string type;
    std::string name;
};

struct MethodData {
    std::string name;
    std::string returns;
    std::string access;
    bool isStatic = false;
    bool isAbstract = false;
    bool isVirtual = false;
    bool hasRva = false;
    uint64_t rva = 0;
    std::vector<ParamData> params;
};

struct FieldData {
    std::string name;
    std::string type;
    std::string access;
    bool isStatic = false;
    bool isConst = false;
    bool hasOffset = false;
    int32_t offset = 0;
    bool hasValue = false;
    std::string value; // already formatted as a JSON number
};

struct PropertyData {
    std::string name;
    std::string type;
    std::string access;
    bool isStatic = false;
    bool hasGet = false;
    bool hasSet = false;
};

struct ClassData {
    std::string name;
    std::string ns;
    std::string kind;       // class | struct | interface | enum
    std::string parent;     // full name, empty for Object/ValueType/Enum
    std::string underlying; // enums only
    bool hasToken = false;
    uint32_t token = 0;
    bool isAbstract = false;
    bool isSealed = false;
    std::vector<std::string> interfaces;
    std::vector<FieldData> fields;
    std::vector<PropertyData> properties;
    std::vector<MethodData> methods;

    std::string FullName() const { return ns.empty() ? name : ns + "." + name; }
};

// One row of index.json: which files hold an assembly and what is in them.
struct IndexEntry {
    std::string assembly;
    std::vector<std::string> files;
    size_t classCount = 0;
    std::map<std::string, size_t> namespaces;
};

namespace Json {

std::string Escape(const std::string& s);

// Serializes one class as a single line (no trailing newline).
// summary=true drops token/offset/RVA and default ("public") access.
std::string SerializeClass(const ClassData& c, bool summary);

std::string SerializeIndex(const std::string& runtime, const std::string& mode,
                           const std::vector<IndexEntry>& entries);

// Returns false when the file cannot be written.
bool WriteIndexFile(const std::string& path, const std::string& runtime, const std::string& mode,
                    const std::vector<IndexEntry>& entries);

} // namespace Json
