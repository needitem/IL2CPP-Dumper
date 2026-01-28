#include "../include/IL2CPP_Class.h"
#include "../include/IL2CPP_API.h"

std::string IL2CPP_Class::GetName() const {
    if (!handle) return "";
    const char* name = IL2CPP::ClassGetName(handle);
    return name ? name : "";
}

std::string IL2CPP_Class::GetNamespace() const {
    if (!handle) return "";
    const char* ns = IL2CPP::ClassGetNamespace(handle);
    return ns ? ns : "";
}

uint32_t IL2CPP_Class::GetToken() const {
    return handle ? IL2CPP::ClassGetTypeToken(handle) : 0;
}

bool IL2CPP_Class::IsValueType() const {
    return handle ? IL2CPP::ClassIsValueType(handle) : false;
}

bool IL2CPP_Class::IsInterface() const {
    return handle ? IL2CPP::ClassIsInterface(handle) : false;
}

IL2CPP_Class IL2CPP_Class::GetParent() const {
    if (!handle) return IL2CPP_Class();
    return IL2CPP_Class(IL2CPP::ClassGetParent(handle));
}

std::vector<IL2CPP_Class> IL2CPP_Class::GetInterfaces() const {
    std::vector<IL2CPP_Class> result;
    if (!handle) return result;

    void* iter = nullptr;
    while (void* iface = IL2CPP::ClassGetInterfaces(handle, &iter)) {
        result.emplace_back(iface);
    }
    return result;
}

std::vector<IL2CPP_Class::FieldInfo> IL2CPP_Class::GetFields() const {
    std::vector<FieldInfo> result;
    if (!handle) return result;

    void* iter = nullptr;
    while (void* field = IL2CPP::ClassGetFields(handle, &iter)) {
        uint32_t flags = IL2CPP::FieldGetFlags(field);
        void* type = IL2CPP::FieldGetType(field);
        const char* typeName = IL2CPP::TypeGetName(type);
        const char* fieldName = IL2CPP::FieldGetName(field);
        int32_t offset = IL2CPP::FieldGetOffset(field);

        result.emplace_back(
            flags,
            typeName ? typeName : "?",
            fieldName ? fieldName : "?",
            offset
        );
    }
    return result;
}

std::vector<IL2CPP_Class::MethodInfo> IL2CPP_Class::GetMethods() const {
    std::vector<MethodInfo> result;
    if (!handle) return result;

    void* iter = nullptr;
    while (void* method = IL2CPP::ClassGetMethods(handle, &iter)) {
        uint32_t flags = IL2CPP::MethodGetFlags(method);
        void* retType = IL2CPP::MethodGetReturnType(method);
        const char* retName = IL2CPP::TypeGetName(retType);
        const char* methodName = IL2CPP::MethodGetName(method);

        std::vector<ParamInfo> params;
        uint32_t paramCount = IL2CPP::MethodGetParamCount(method);
        for (uint32_t i = 0; i < paramCount; i++) {
            void* paramType = IL2CPP::MethodGetParam(method, i);
            const char* paramTypeName = IL2CPP::TypeGetName(paramType);
            const char* paramName = IL2CPP::MethodGetParamName(method, i);
            params.emplace_back(
                paramTypeName ? paramTypeName : "?",
                paramName ? paramName : ("arg" + std::to_string(i))
            );
        }

        result.emplace_back(
            flags,
            retName ? retName : "void",
            methodName ? methodName : "?",
            params
        );
    }
    return result;
}
