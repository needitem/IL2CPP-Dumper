#include "../include/Mono_API.h"
#include <Windows.h>

namespace Mono {

bool Initialized = false;

// Function pointer types
using fn_get_root_domain     = void*(*)();
using fn_domain_foreach      = void(*)(void*, void(*)(void*, void*), void*);
using fn_assembly_get_image  = void*(*)(void*);
using fn_image_get_name      = const char*(*)(void*);
using fn_image_get_table_rows= int(*)(void*, int);
using fn_class_get           = void*(*)(void*, uint32_t);
using fn_class_get_name      = const char*(*)(void*);
using fn_class_get_namespace = const char*(*)(void*);
using fn_class_get_parent    = void*(*)(void*);
using fn_class_is_valuetype  = bool(*)(void*);
using fn_class_is_interface  = bool(*)(void*);
using fn_class_get_methods   = void*(*)(void*, void**);
using fn_method_get_name     = const char*(*)(void*);
using fn_method_get_flags    = uint32_t(*)(void*, uint32_t*);
using fn_method_signature    = void*(*)(void*);
using fn_sig_get_return_type = void*(*)(void*);
using fn_sig_get_params      = void*(*)(void*, void**);
using fn_sig_get_param_count = uint32_t(*)(void*);
using fn_method_get_param_name = const char*(*)(void*, int);
using fn_class_get_fields    = void*(*)(void*, void**);
using fn_field_get_name      = const char*(*)(void*);
using fn_field_get_type      = void*(*)(void*);
using fn_field_get_flags     = uint32_t(*)(void*);
using fn_field_get_offset    = int32_t(*)(void*);
using fn_type_get_name       = char*(*)(void*);
using fn_free                = void(*)(void*);

static fn_get_root_domain      pGetRootDomain;
static fn_domain_foreach       pDomainForeach;
static fn_assembly_get_image   pAssemblyGetImage;
static fn_image_get_name       pImageGetName;
static fn_image_get_table_rows pImageGetTableRows;
static fn_class_get            pClassGet;
static fn_class_get_name       pClassGetName;
static fn_class_get_namespace  pClassGetNamespace;
static fn_class_get_parent     pClassGetParent;
static fn_class_is_valuetype   pClassIsValueType;
static fn_class_is_interface   pClassIsInterface;
static fn_class_get_methods    pClassGetMethods;
static fn_method_get_name      pMethodGetName;
static fn_method_get_flags     pMethodGetFlags;
static fn_method_signature     pMethodSignature;
static fn_sig_get_return_type  pSigGetReturnType;
static fn_sig_get_params       pSigGetParams;
static fn_sig_get_param_count  pSigGetParamCount;
static fn_method_get_param_name pMethodGetParamName;
static fn_class_get_fields     pClassGetFields;
static fn_field_get_name       pFieldGetName;
static fn_field_get_type       pFieldGetType;
static fn_field_get_flags      pFieldGetFlags;
static fn_field_get_offset     pFieldGetOffset;
static fn_type_get_name        pTypeGetName;
static fn_free                 pFree;

bool Initialize() {
    HMODULE hMono = GetModuleHandleA("mono-2.0-sgen.dll");
    if (!hMono) return false;

    auto get = [&](const char* name) {
        return GetProcAddress(hMono, name);
    };

    pGetRootDomain      = (fn_get_root_domain)     get("mono_get_root_domain");
    pDomainForeach      = (fn_domain_foreach)       get("mono_domain_assembly_foreach");
    pAssemblyGetImage   = (fn_assembly_get_image)   get("mono_assembly_get_image");
    pImageGetName       = (fn_image_get_name)       get("mono_image_get_name");
    pImageGetTableRows  = (fn_image_get_table_rows) get("mono_image_get_table_rows");
    pClassGet           = (fn_class_get)            get("mono_class_get");
    pClassGetName       = (fn_class_get_name)       get("mono_class_get_name");
    pClassGetNamespace  = (fn_class_get_namespace)  get("mono_class_get_namespace");
    pClassGetParent     = (fn_class_get_parent)     get("mono_class_get_parent");
    pClassIsValueType   = (fn_class_is_valuetype)   get("mono_class_is_valuetype");
    pClassIsInterface   = (fn_class_is_interface)   get("mono_class_is_interface");
    pClassGetMethods    = (fn_class_get_methods)    get("mono_class_get_methods");
    pMethodGetName      = (fn_method_get_name)      get("mono_method_get_name");
    pMethodGetFlags     = (fn_method_get_flags)     get("mono_method_get_flags");
    pMethodSignature    = (fn_method_signature)     get("mono_method_signature");
    pSigGetReturnType   = (fn_sig_get_return_type)  get("mono_signature_get_return_type");
    pSigGetParams       = (fn_sig_get_params)       get("mono_signature_get_params");
    pSigGetParamCount   = (fn_sig_get_param_count)  get("mono_signature_get_param_count");
    pMethodGetParamName = (fn_method_get_param_name)get("mono_method_get_param_name");
    pClassGetFields     = (fn_class_get_fields)     get("mono_class_get_fields");
    pFieldGetName       = (fn_field_get_name)       get("mono_field_get_name");
    pFieldGetType       = (fn_field_get_type)       get("mono_field_get_type");
    pFieldGetFlags      = (fn_field_get_flags)      get("mono_field_get_flags");
    pFieldGetOffset     = (fn_field_get_offset)     get("mono_field_get_offset");
    pTypeGetName        = (fn_type_get_name)        get("mono_type_get_name");
    pFree               = (fn_free)                 get("mono_free");
    if (!pFree) pFree   = (fn_free)                 get("g_free");

    Initialized = pGetRootDomain && pDomainForeach && pAssemblyGetImage &&
                  pImageGetName  && pClassGet;
    return Initialized;
}

void* GetRootDomain() {
    return pGetRootDomain ? pGetRootDomain() : nullptr;
}

void ForEachAssembly(void* domain, AssemblyCallback cb) {
    if (!pDomainForeach || !domain) return;
    // Wrap callback into a raw function pointer via static + lambda
    struct Ctx { AssemblyCallback* cb; };
    Ctx ctx{ &cb };
    pDomainForeach(domain, [](void* assembly, void* userData) {
        Ctx* c = static_cast<Ctx*>(userData);
        (*c->cb)(assembly);
    }, &ctx);
}

void* AssemblyGetImage(void* assembly) {
    return pAssemblyGetImage ? pAssemblyGetImage(assembly) : nullptr;
}

const char* ImageGetName(void* image) {
    return pImageGetName ? pImageGetName(image) : "";
}

int ImageGetTableRows(void* image, int tableId) {
    return pImageGetTableRows ? pImageGetTableRows(image, tableId) : 0;
}

void* ClassGet(void* image, uint32_t typeToken) {
    return pClassGet ? pClassGet(image, typeToken) : nullptr;
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

bool ClassIsValueType(void* klass) {
    return pClassIsValueType ? pClassIsValueType(klass) : false;
}

bool ClassIsInterface(void* klass) {
    return pClassIsInterface ? pClassIsInterface(klass) : false;
}

void* ClassGetMethods(void* klass, void** iter) {
    return pClassGetMethods ? pClassGetMethods(klass, iter) : nullptr;
}

const char* MethodGetName(void* method) {
    return pMethodGetName ? pMethodGetName(method) : "";
}

uint32_t MethodGetFlags(void* method, uint32_t* iflags) {
    return pMethodGetFlags ? pMethodGetFlags(method, iflags) : 0;
}

void* MethodGetSignature(void* method) {
    return pMethodSignature ? pMethodSignature(method) : nullptr;
}

void* SignatureGetReturnType(void* sig) {
    return pSigGetReturnType ? pSigGetReturnType(sig) : nullptr;
}

void* SignatureGetParams(void* sig, void** iter) {
    return pSigGetParams ? pSigGetParams(sig, iter) : nullptr;
}

uint32_t SignatureGetParamCount(void* sig) {
    return pSigGetParamCount ? pSigGetParamCount(sig) : 0;
}

const char* MethodGetParamName(void* method, int index) {
    return pMethodGetParamName ? pMethodGetParamName(method, index) : "";
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

char* TypeGetName(void* type) {
    return pTypeGetName ? pTypeGetName(type) : nullptr;
}

void MonoFree(void* ptr) {
    if (pFree && ptr) pFree(ptr);
}

} // namespace Mono
