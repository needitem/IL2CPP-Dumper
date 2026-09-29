#include "../include/Model.h"
#include <cstdio>
#include <fstream>

namespace Json {

std::string Escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

namespace {

std::string Hex(unsigned long long v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "0x%llx", v);
    return buf;
}

// Appends  "key":"value"
void AddStr(std::string& o, const char* key, const std::string& v) {
    o += '"';
    o += key;
    o += "\":\"";
    o += Escape(v);
    o += '"';
}

bool IsHiddenMethod(const std::string& n) {
    return n.find('<') != std::string::npos || n.compare(0, 2, "__") == 0;
}

} // namespace

std::string SerializeClass(const ClassData& c, bool summary) {
    std::string o;
    o.reserve(512);

    o += '{';
    AddStr(o, "name", c.name);
    o += ',';
    AddStr(o, "fullName", c.FullName());
    o += ',';
    AddStr(o, "type", c.kind);

    if (!summary && c.hasToken) {
        o += ',';
        AddStr(o, "token", Hex(c.token));
    }
    if (!c.parent.empty()) {
        o += ',';
        AddStr(o, "extends", c.parent);
    }
    if (!c.underlying.empty()) {
        o += ',';
        AddStr(o, "underlying", c.underlying);
    }
    if (!c.interfaces.empty()) {
        o += ",\"implements\":[";
        for (size_t i = 0; i < c.interfaces.size(); i++) {
            if (i) o += ',';
            o += '"' + Escape(c.interfaces[i]) + '"';
        }
        o += ']';
    }
    if (c.isAbstract && c.isSealed) {
        o += ",\"static\":true"; // C# static class == abstract sealed
    } else {
        if (c.isAbstract) o += ",\"abstract\":true";
        if (c.isSealed)   o += ",\"sealed\":true";
    }

    const bool isEnum = (c.kind == "enum");

    // Fields
    o += ",\"fields\":[";
    for (size_t i = 0; i < c.fields.size(); i++) {
        const FieldData& f = c.fields[i];
        if (i) o += ',';
        o += '{';
        AddStr(o, "name", f.name);
        if (!(isEnum && f.isConst)) { // enum members all share the enum's own type
            o += ',';
            AddStr(o, "type", f.type);
        }
        if (!f.access.empty() && (!summary || f.access != "public")) {
            o += ',';
            AddStr(o, "access", f.access);
        }
        if (f.isStatic && !isEnum) o += ",\"static\":true";
        if (f.isConst && !isEnum)  o += ",\"const\":true";
        if (f.hasValue) o += ",\"value\":" + f.value;
        if (!summary && f.hasOffset) {
            o += ',';
            AddStr(o, "offset", Hex((unsigned long long)(uint32_t)f.offset));
        }
        o += '}';
    }
    o += ']';

    // Properties
    if (!c.properties.empty()) {
        o += ",\"properties\":[";
        for (size_t i = 0; i < c.properties.size(); i++) {
            const PropertyData& p = c.properties[i];
            if (i) o += ',';
            o += '{';
            AddStr(o, "name", p.name);
            o += ',';
            AddStr(o, "type", p.type);
            if (!p.access.empty() && (!summary || p.access != "public")) {
                o += ',';
                AddStr(o, "access", p.access);
            }
            if (p.isStatic) o += ",\"static\":true";
            o += p.hasGet ? ",\"get\":true" : "";
            o += p.hasSet ? ",\"set\":true" : "";
            o += '}';
        }
        o += ']';
    }

    // Methods
    o += ",\"methods\":[";
    bool first = true;
    for (const MethodData& m : c.methods) {
        if (summary && IsHiddenMethod(m.name)) continue;
        if (!first) o += ',';
        first = false;
        o += '{';
        AddStr(o, "name", m.name);
        o += ',';
        AddStr(o, "returns", m.returns);
        if (!m.params.empty()) {
            o += ",\"params\":[";
            for (size_t j = 0; j < m.params.size(); j++) {
                if (j) o += ',';
                o += '{';
                AddStr(o, "type", m.params[j].type);
                o += ',';
                AddStr(o, "name", m.params[j].name);
                o += '}';
            }
            o += ']';
        }
        if (!m.access.empty() && (!summary || m.access != "public")) {
            o += ',';
            AddStr(o, "access", m.access);
        }
        if (m.isStatic)   o += ",\"static\":true";
        if (m.isAbstract) o += ",\"abstract\":true";
        if (m.isVirtual && !m.isAbstract) o += ",\"virtual\":true";
        if (!summary && m.hasRva) {
            o += ',';
            AddStr(o, "rva", Hex(m.rva));
        }
        o += '}';
    }
    o += "]}";
    return o;
}

std::string SerializeIndex(const std::string& runtime, const std::string& mode,
                           const std::vector<IndexEntry>& entries) {
    std::string o;
    o += "{\"schemaVersion\":" + std::to_string(kSchemaVersion);
    o += ",\"runtime\":\"" + Escape(runtime) + "\"";
    o += ",\"mode\":\"" + Escape(mode) + "\"";
    o += ",\"assemblies\":[\n";
    for (size_t i = 0; i < entries.size(); i++) {
        const IndexEntry& e = entries[i];
        if (i) o += ",\n";
        o += "{\"assembly\":\"" + Escape(e.assembly) + "\",\"classCount\":" + std::to_string(e.classCount);
        o += ",\"files\":[";
        for (size_t j = 0; j < e.files.size(); j++) {
            if (j) o += ',';
            o += '"' + Escape(e.files[j]) + '"';
        }
        o += "],\"namespaces\":{";
        bool first = true;
        for (const auto& kv : e.namespaces) {
            if (!first) o += ',';
            first = false;
            o += '"' + Escape(kv.first.empty() ? "(global)" : kv.first) + "\":" + std::to_string(kv.second);
        }
        o += "}}";
    }
    o += "\n]}\n";
    return o;
}

bool WriteIndexFile(const std::string& path, const std::string& runtime, const std::string& mode,
                    const std::vector<IndexEntry>& entries) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;
    out << SerializeIndex(runtime, mode, entries);
    return out.good();
}

} // namespace Json
