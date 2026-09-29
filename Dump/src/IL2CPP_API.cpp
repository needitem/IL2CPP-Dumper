#include "../include/IL2CPP_API.h"
#include <Windows.h>
#include <Psapi.h>
#include <cstring>

#pragma comment(lib, "psapi.lib")

namespace IL2CPP {

bool Initialized = false;

// Function pointer types
using fn_domain_get = void*(*)();
using fn_domain_get_assemblies = void**(*)(void*, size_t*);
using fn_assembly_get_image = void*(*)(void*);
using fn_image_get_name = const char*(*)(void*);
using fn_image_get_class_count = size_t(*)(void*);
using fn_image_get_class = void*(*)(void*, size_t);
using fn_class_get_name = const char*(*)(void*);
using fn_class_get_namespace = const char*(*)(void*);
using fn_class_get_parent = void*(*)(void*);
using fn_class_get_type_token = uint32_t(*)(void*);
using fn_class_is_valuetype = bool(*)(void*);
using fn_class_is_interface = bool(*)(void*);
using fn_class_get_interfaces = void*(*)(void*, void**);
using fn_class_get_fields = void*(*)(void*, void**);
using fn_field_get_name = const char*(*)(void*);
using fn_field_get_type = void*(*)(void*);
using fn_field_get_flags = uint32_t(*)(void*);
using fn_field_get_offset = int32_t(*)(void*);
using fn_class_get_methods = void*(*)(void*, void**);
using fn_method_get_name = const char*(*)(void*);
using fn_method_get_return_type = void*(*)(void*);
using fn_method_get_flags = uint32_t(*)(void*);
using fn_method_get_param_count = uint32_t(*)(void*);
using fn_method_get_param = void*(*)(void*, uint32_t);
using fn_method_get_param_name = const char*(*)(void*, uint32_t);
using fn_type_get_name = const char*(*)(void*);
using fn_class_get_flags = uint32_t(*)(void*);
using fn_class_is_enum = int(*)(void*);
using fn_class_enum_basetype = void*(*)(void*);
using fn_type_get_type = int(*)(void*);
using fn_class_get_properties = void*(*)(void*, void**);
using fn_property_get_name = const char*(*)(void*);
using fn_property_get_method = void*(*)(void*);
using fn_field_static_get_value = void(*)(void*, void*);

// Function pointers
static fn_domain_get pDomainGet;
static fn_domain_get_assemblies pDomainGetAssemblies;
static fn_assembly_get_image pAssemblyGetImage;
static fn_image_get_name pImageGetName;
static fn_image_get_class_count pImageGetClassCount;
static fn_image_get_class pImageGetClass;
static fn_class_get_name pClassGetName;
static fn_class_get_namespace pClassGetNamespace;
static fn_class_get_parent pClassGetParent;
static fn_class_get_type_token pClassGetTypeToken;
static fn_class_is_valuetype pClassIsValueType;
static fn_class_is_interface pClassIsInterface;
static fn_class_get_interfaces pClassGetInterfaces;
static fn_class_get_fields pClassGetFields;
static fn_field_get_name pFieldGetName;
static fn_field_get_type pFieldGetType;
static fn_field_get_flags pFieldGetFlags;
static fn_field_get_offset pFieldGetOffset;
static fn_class_get_methods pClassGetMethods;
static fn_method_get_name pMethodGetName;
static fn_method_get_return_type pMethodGetReturnType;
static fn_method_get_flags pMethodGetFlags;
static fn_method_get_param_count pMethodGetParamCount;
static fn_method_get_param pMethodGetParam;
static fn_method_get_param_name pMethodGetParamName;
static fn_type_get_name pTypeGetName;
static fn_class_get_flags pClassGetFlags;
static fn_class_is_enum pClassIsEnum;
static fn_class_enum_basetype pClassEnumBaseType;
static fn_type_get_type pTypeGetType;
static fn_class_get_properties pClassGetProperties;
static fn_property_get_name pPropertyGetName;
static fn_property_get_method pPropertyGetGetMethod;
static fn_property_get_method pPropertyGetSetMethod;
static fn_field_static_get_value pFieldStaticGetValue;

static uintptr_t g_ModuleBase = 0;
static size_t g_ModuleSize = 0;

static bool HasRequiredExports(HMODULE hModule) {
    if (!hModule) return false;
    return GetProcAddress(hModule, "il2cpp_domain_get")
        && GetProcAddress(hModule, "il2cpp_domain_get_assemblies")
        && GetProcAddress(hModule, "il2cpp_assembly_get_image");
}

static HMODULE FindIL2CPPModule() {
    const char* fastCandidates[] = {
        "GameAssembly.dll",
        nullptr
    };

    for (int i = 0; fastCandidates[i]; i++) {
        HMODULE h = GetModuleHandleA(fastCandidates[i]);
        if (HasRequiredExports(h)) return h;
    }

    HMODULE mods[2048];
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        return nullptr;
    }

    DWORD count = needed / sizeof(HMODULE);
    for (DWORD i = 0; i < count; i++) {
        if (HasRequiredExports(mods[i])) {
            return mods[i];
        }
    }
    return nullptr;
}

bool Initialize() {
    HMODULE hModule = FindIL2CPPModule();
    if (!hModule) return false;

    auto get = [&](const char* name) {
        return GetProcAddress(hModule, name);
    };

    pDomainGet = (fn_domain_get)get("il2cpp_domain_get");
    pDomainGetAssemblies = (fn_domain_get_assemblies)get("il2cpp_domain_get_assemblies");
    pAssemblyGetImage = (fn_assembly_get_image)get("il2cpp_assembly_get_image");
    pImageGetName = (fn_image_get_name)get("il2cpp_image_get_name");
    pImageGetClassCount = (fn_image_get_class_count)get("il2cpp_image_get_class_count");
    pImageGetClass = (fn_image_get_class)get("il2cpp_image_get_class");
    pClassGetName = (fn_class_get_name)get("il2cpp_class_get_name");
    pClassGetNamespace = (fn_class_get_namespace)get("il2cpp_class_get_namespace");
    pClassGetParent = (fn_class_get_parent)get("il2cpp_class_get_parent");
    pClassGetTypeToken = (fn_class_get_type_token)get("il2cpp_class_get_type_token");
    pClassIsValueType = (fn_class_is_valuetype)get("il2cpp_class_is_valuetype");
    pClassIsInterface = (fn_class_is_interface)get("il2cpp_class_is_interface");
    pClassGetInterfaces = (fn_class_get_interfaces)get("il2cpp_class_get_interfaces");
    pClassGetFields = (fn_class_get_fields)get("il2cpp_class_get_fields");
    pFieldGetName = (fn_field_get_name)get("il2cpp_field_get_name");
    pFieldGetType = (fn_field_get_type)get("il2cpp_field_get_type");
    pFieldGetFlags = (fn_field_get_flags)get("il2cpp_field_get_flags");
    pFieldGetOffset = (fn_field_get_offset)get("il2cpp_field_get_offset");
    pClassGetMethods = (fn_class_get_methods)get("il2cpp_class_get_methods");
    pMethodGetName = (fn_method_get_name)get("il2cpp_method_get_name");
    pMethodGetReturnType = (fn_method_get_return_type)get("il2cpp_method_get_return_type");
    pMethodGetFlags = (fn_method_get_flags)get("il2cpp_method_get_flags");
    pMethodGetParamCount = (fn_method_get_param_count)get("il2cpp_method_get_param_count");
    pMethodGetParam = (fn_method_get_param)get("il2cpp_method_get_param");
    pMethodGetParamName = (fn_method_get_param_name)get("il2cpp_method_get_param_name");
    pTypeGetName = (fn_type_get_name)get("il2cpp_type_get_name");
    pClassGetFlags = (fn_class_get_flags)get("il2cpp_class_get_flags");
    pClassIsEnum = (fn_class_is_enum)get("il2cpp_class_is_enum");
    pClassEnumBaseType = (fn_class_enum_basetype)get("il2cpp_class_enum_basetype");
    pTypeGetType = (fn_type_get_type)get("il2cpp_type_get_type");
    pClassGetProperties = (fn_class_get_properties)get("il2cpp_class_get_properties");
    pPropertyGetName = (fn_property_get_name)get("il2cpp_property_get_name");
    pPropertyGetGetMethod = (fn_property_get_method)get("il2cpp_property_get_get_method");
    pPropertyGetSetMethod = (fn_property_get_method)get("il2cpp_property_get_set_method");
    pFieldStaticGetValue = (fn_field_static_get_value)get("il2cpp_field_static_get_value");

    MODULEINFO mi = {};
    if (GetModuleInformation(GetCurrentProcess(), hModule, &mi, sizeof(mi))) {
        g_ModuleBase = (uintptr_t)mi.lpBaseOfDll;
        g_ModuleSize = mi.SizeOfImage;
    }

    Initialized = pDomainGet && pDomainGetAssemblies && pAssemblyGetImage;
    return Initialized;
}

void* GetDomain() {
    return pDomainGet ? pDomainGet() : nullptr;
}

void** GetAssemblies(void* domain, size_t* count) {
    return pDomainGetAssemblies ? pDomainGetAssemblies(domain, count) : nullptr;
}

void* AssemblyGetImage(void* assembly) {
    return pAssemblyGetImage ? pAssemblyGetImage(assembly) : nullptr;
}

const char* ImageGetName(void* image) {
    return pImageGetName ? pImageGetName(image) : "";
}

size_t ImageGetClassCount(void* image) {
    return pImageGetClassCount ? pImageGetClassCount(image) : 0;
}

void* ImageGetClass(void* image, size_t index) {
    return pImageGetClass ? pImageGetClass(image, index) : nullptr;
}

const char* ClassGetName(void* klass) {
    return pClassGetName ? pClassGetName(klass) : "";
}

const char* ClassGetNamespace(void* klass) {
    return pClassGetNamespace ? pClassGetNamespace(klass) : "";
}

void* ClassGetParent(void* klass) {
    return pClassGetParent ? pClassGetParent(klass) : nullptr;
}

uint32_t ClassGetTypeToken(void* klass) {
    return pClassGetTypeToken ? pClassGetTypeToken(klass) : 0;
}

bool ClassIsValueType(void* klass) {
    return pClassIsValueType ? pClassIsValueType(klass) : false;
}

bool ClassIsInterface(void* klass) {
    return pClassIsInterface ? pClassIsInterface(klass) : false;
}

void* ClassGetInterfaces(void* klass, void** iter) {
    return pClassGetInterfaces ? pClassGetInterfaces(klass, iter) : nullptr;
}

void* ClassGetFields(void* klass, void** iter) {
    return pClassGetFields ? pClassGetFields(klass, iter) : nullptr;
}

const char* FieldGetName(void* field) {
    return pFieldGetName ? pFieldGetName(field) : "";
}

void* FieldGetType(void* field) {
    return pFieldGetType ? pFieldGetType(field) : nullptr;
}

uint32_t FieldGetFlags(void* field) {
    return pFieldGetFlags ? pFieldGetFlags(field) : 0;
}

int32_t FieldGetOffset(void* field) {
    return pFieldGetOffset ? pFieldGetOffset(field) : 0;
}

void* ClassGetMethods(void* klass, void** iter) {
    return pClassGetMethods ? pClassGetMethods(klass, iter) : nullptr;
}

const char* MethodGetName(void* method) {
    return pMethodGetName ? pMethodGetName(method) : "";
}

void* MethodGetReturnType(void* method) {
    return pMethodGetReturnType ? pMethodGetReturnType(method) : nullptr;
}

uint32_t MethodGetFlags(void* method) {
    return pMethodGetFlags ? pMethodGetFlags(method) : 0;
}

uint32_t MethodGetParamCount(void* method) {
    return pMethodGetParamCount ? pMethodGetParamCount(method) : 0;
}

void* MethodGetParam(void* method, uint32_t index) {
    return pMethodGetParam ? pMethodGetParam(method, index) : nullptr;
}

const char* MethodGetParamName(void* method, uint32_t index) {
    return pMethodGetParamName ? pMethodGetParamName(method, index) : "";
}

const char* TypeGetName(void* type) {
    return pTypeGetName ? pTypeGetName(type) : "";
}

uint32_t ClassGetFlags(void* klass) {
    return pClassGetFlags ? pClassGetFlags(klass) : 0;
}

bool ClassIsEnum(void* klass) {
    return pClassIsEnum ? pClassIsEnum(klass) != 0 : false;
}

void* ClassEnumBaseType(void* klass) {
    return pClassEnumBaseType ? pClassEnumBaseType(klass) : nullptr;
}

int TypeGetTypeCode(void* type) {
    return (pTypeGetType && type) ? pTypeGetType(type) : 0;
}

void* ClassGetProperties(void* klass, void** iter) {
    return pClassGetProperties ? pClassGetProperties(klass, iter) : nullptr;
}

const char* PropertyGetName(void* prop) {
    return pPropertyGetName ? pPropertyGetName(prop) : "";
}

void* PropertyGetGetMethod(void* prop) {
    return pPropertyGetGetMethod ? pPropertyGetGetMethod(prop) : nullptr;
}

void* PropertyGetSetMethod(void* prop) {
    return pPropertyGetSetMethod ? pPropertyGetSetMethod(prop) : nullptr;
}

// Plain-C helpers: __try cannot live in a function that needs C++ unwinding.
static bool CallStaticGetValueGuarded(void* field, void* buf) {
    __try {
        pFieldStaticGetValue(field, buf);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool ReadPointerGuarded(void* addr, void** out) {
    __try {
        *out = *(void**)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool FieldStaticGetValue(void* field, void* buf, size_t size) {
    if (!pFieldStaticGetValue || !field || !buf) return false;
    memset(buf, 0, size);
    return CallStaticGetValueGuarded(field, buf);
}

bool MethodGetRva(void* method, uint64_t* rva) {
    if (!method || !g_ModuleBase) return false;
    // MethodInfo::methodPointer is the first member of MethodInfo.
    void* fn = nullptr;
    if (!ReadPointerGuarded(method, &fn) || !fn) return false;
    uintptr_t p = (uintptr_t)fn;
    if (p < g_ModuleBase || p >= g_ModuleBase + g_ModuleSize) return false;
    *rva = (uint64_t)(p - g_ModuleBase);
    return true;
}

} // namespace IL2CPP
