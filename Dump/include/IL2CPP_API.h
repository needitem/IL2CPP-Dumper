#pragma once

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

bool Initialize();

} // namespace IL2CPP
