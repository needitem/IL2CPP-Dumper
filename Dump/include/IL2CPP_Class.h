#pragma once

#include <string>
#include <vector>
#include <tuple>
#include <cstdint>

class IL2CPP_Class {
public:
    void* handle = nullptr;

    IL2CPP_Class() = default;
    explicit IL2CPP_Class(void* ptr) : handle(ptr) {}

    std::string GetName() const;
    std::string GetNamespace() const;
    uint32_t GetToken() const;

    bool IsValueType() const;
    bool IsInterface() const;

    IL2CPP_Class GetParent() const;
    std::vector<IL2CPP_Class> GetInterfaces() const;

    // Field: flags, typeName, fieldName, offset
    using FieldInfo = std::tuple<uint32_t, std::string, std::string, int32_t>;
    std::vector<FieldInfo> GetFields() const;

    // Method: flags, returnType, methodName, params
    using ParamInfo = std::pair<std::string, std::string>;
    using MethodInfo = std::tuple<uint32_t, std::string, std::string, std::vector<ParamInfo>>;
    std::vector<MethodInfo> GetMethods() const;
};
