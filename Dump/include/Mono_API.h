#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace Mono {

extern bool Initialized;

// Assembly enumeration callback
using AssemblyCallback = std::function<void(void* assembly)>;

bool Initialize();
const std::string& GetDiagLog(); // Initialize() 실패 시 진단 로그

void ForEachAssembly(void* domain, AssemblyCallback cb);
void* GetRootDomain();
void* AssemblyGetImage(void* assembly);
const char* ImageGetName(void* image);
int ImageGetTableRows(void* image, int tableId); // tableId=2 for TYPEDEF

// token = 0x02000001 + row_index (1-based)
void* ClassGet(void* image, uint32_t typeToken);
const char* ClassGetName(void* klass);
const char* ClassGetNamespace(void* klass);
void* ClassGetParent(void* klass);
bool ClassIsValueType(void* klass);
bool ClassIsInterface(void* klass);

// iter: pass nullptr to start, then pass back returned iter
void* ClassGetMethods(void* klass, void** iter);
const char* MethodGetName(void* method);
uint32_t MethodGetFlags(void* method, uint32_t* iflags);
void* MethodGetSignature(void* method);
void* SignatureGetReturnType(void* sig);
void* SignatureGetParams(void* sig, void** iter);
uint32_t SignatureGetParamCount(void* sig);
const char* MethodGetParamName(void* method, int index);

void* ClassGetFields(void* klass, void** iter);
const char* FieldGetName(void* field);
void* FieldGetType(void* field);
uint32_t FieldGetFlags(void* field);
int32_t FieldGetOffset(void* field);

// Returns heap-allocated string, must be freed with MonoFree()
char* TypeGetName(void* type);
void MonoFree(void* ptr);

} // namespace Mono
