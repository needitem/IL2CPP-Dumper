#pragma once

#include <cstddef>
#include <cstdint>

namespace IL2CPP {

// Function pointers
extern bool Initialized;

void* GetDomain();
void** GetAssemblies(void* domain, size_t* count);
void* AssemblyGetImage(void* assembly);

const char* ImageGetName(void* image);
size_t ImageGetClassCount(void* image);
void* ImageGetClass(void* image, size_t index);

const char* ClassGetName(void* klass);
const char* ClassGetNamespace(void* klass);
void* ClassGetParent(void* klass);
uint32_t ClassGetTypeToken(void* klass);
bool ClassIsValueType(void* klass);
bool ClassIsInterface(void* klass);
void* ClassGetInterfaces(void* klass, void** iter);

void* ClassGetFields(void* klass, void** iter);
const char* FieldGetName(void* field);
void* FieldGetType(void* field);
uint32_t FieldGetFlags(void* field);
int32_t FieldGetOffset(void* field);

void* ClassGetMethods(void* klass, void** iter);
const char* MethodGetName(void* method);
void* MethodGetReturnType(void* method);
uint32_t MethodGetFlags(void* method);
uint32_t MethodGetParamCount(void* method);
void* MethodGetParam(void* method, uint32_t index);
const char* MethodGetParamName(void* method, uint32_t index);

const char* TypeGetName(void* type);

// The exports below are optional; wrappers return 0/null/false when the
// game's GameAssembly does not export them.
uint32_t ClassGetFlags(void* klass);               // TypeAttributes
bool ClassIsEnum(void* klass);
void* ClassEnumBaseType(void* klass);
int TypeGetTypeCode(void* type);                   // ELEMENT_TYPE_* (0x08 = int32, ...)

void* ClassGetProperties(void* klass, void** iter);
const char* PropertyGetName(void* prop);
void* PropertyGetGetMethod(void* prop);
void* PropertyGetSetMethod(void* prop);

// Reads a static/literal field value into buf (SEH-guarded). False on failure.
bool FieldStaticGetValue(void* field, void* buf, size_t size);

// Method RVA relative to the IL2CPP module base. False if the method has no
// native body (abstract/generic/stripped) or the pointer is outside the module.
bool MethodGetRva(void* method, uint64_t* rva);

bool Initialize();

} // namespace IL2CPP
