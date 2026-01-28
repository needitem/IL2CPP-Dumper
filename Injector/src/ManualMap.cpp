#include "ManualMap.h"
#include <fstream>
#include <vector>

#pragma comment(lib, "ntdll.lib")

// Shellcode to call DllMain
#ifdef _WIN64
unsigned char Shellcode[] = {
    0x48, 0x83, 0xEC, 0x28,                         // sub rsp, 0x28
    0x48, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rcx, DllBase
    0xBA, 0x01, 0x00, 0x00, 0x00,                   // mov edx, DLL_PROCESS_ATTACH
    0x4D, 0x31, 0xC0,                               // xor r8, r8
    0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rax, EntryPoint
    0xFF, 0xD0,                                     // call rax
    0x48, 0x83, 0xC4, 0x28,                         // add rsp, 0x28
    0xC3                                            // ret
};
const int SHELLCODE_DLLBASE_OFFSET = 6;
const int SHELLCODE_ENTRY_OFFSET = 24;
#else
unsigned char Shellcode[] = {
    0x68, 0x00, 0x00, 0x00, 0x00,                   // push 0 (lpvReserved)
    0x68, 0x01, 0x00, 0x00, 0x00,                   // push DLL_PROCESS_ATTACH
    0x68, 0x00, 0x00, 0x00, 0x00,                   // push DllBase
    0xB8, 0x00, 0x00, 0x00, 0x00,                   // mov eax, EntryPoint
    0xFF, 0xD0,                                     // call eax
    0xC3                                            // ret
};
const int SHELLCODE_DLLBASE_OFFSET = 11;
const int SHELLCODE_ENTRY_OFFSET = 16;
#endif

namespace ManualMap {

bool Inject(DWORD pid, const std::string& dllPath, std::string& error) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        error = "Failed to open process (Error: " + std::to_string(GetLastError()) + ")";
        return false;
    }

    bool result = InjectToProcess(hProcess, dllPath, error);
    CloseHandle(hProcess);
    return result;
}

bool InjectToProcess(HANDLE hProcess, const std::string& dllPath, std::string& error) {
    // Read DLL file
    std::ifstream file(dllPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        error = "Failed to open DLL file";
        return false;
    }

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<BYTE> fileData(fileSize);
    file.read((char*)fileData.data(), fileSize);
    file.close();

    // Parse PE headers
    auto dosHeader = (PIMAGE_DOS_HEADER)fileData.data();
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        error = "Invalid DOS signature";
        return false;
    }

    auto ntHeaders = (PIMAGE_NT_HEADERS)(fileData.data() + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        error = "Invalid NT signature";
        return false;
    }

    auto optHeader = &ntHeaders->OptionalHeader;
    auto fileHeader = &ntHeaders->FileHeader;

    // Allocate memory in target process
    LPVOID remoteBase = VirtualAllocEx(hProcess, nullptr, optHeader->SizeOfImage,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remoteBase) {
        error = "Failed to allocate remote memory (Error: " + std::to_string(GetLastError()) + ")";
        return false;
    }

    // Write headers
    if (!WriteProcessMemory(hProcess, remoteBase, fileData.data(), optHeader->SizeOfHeaders, nullptr)) {
        error = "Failed to write headers";
        VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
        return false;
    }

    // Write sections
    auto section = IMAGE_FIRST_SECTION(ntHeaders);
    for (WORD i = 0; i < fileHeader->NumberOfSections; i++, section++) {
        if (section->SizeOfRawData == 0) continue;

        LPVOID sectionDest = (LPBYTE)remoteBase + section->VirtualAddress;
        if (!WriteProcessMemory(hProcess, sectionDest,
            fileData.data() + section->PointerToRawData, section->SizeOfRawData, nullptr)) {
            error = "Failed to write section";
            VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
            return false;
        }
    }

    // Process relocations
    ULONG_PTR delta = (ULONG_PTR)remoteBase - optHeader->ImageBase;
    if (delta != 0) {
        auto relocDir = &optHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (relocDir->Size > 0) {
            auto reloc = (PIMAGE_BASE_RELOCATION)(fileData.data() + relocDir->VirtualAddress);

            // Find relocation section
            section = IMAGE_FIRST_SECTION(ntHeaders);
            for (WORD i = 0; i < fileHeader->NumberOfSections; i++, section++) {
                if (relocDir->VirtualAddress >= section->VirtualAddress &&
                    relocDir->VirtualAddress < section->VirtualAddress + section->Misc.VirtualSize) {
                    reloc = (PIMAGE_BASE_RELOCATION)(fileData.data() + section->PointerToRawData +
                        (relocDir->VirtualAddress - section->VirtualAddress));
                    break;
                }
            }

            DWORD processed = 0;
            while (processed < relocDir->Size && reloc->VirtualAddress != 0) {
                DWORD numEntries = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                PWORD entries = (PWORD)((LPBYTE)reloc + sizeof(IMAGE_BASE_RELOCATION));

                for (DWORD i = 0; i < numEntries; i++) {
                    WORD type = entries[i] >> 12;
                    WORD offset = entries[i] & 0xFFF;

                    if (type == IMAGE_REL_BASED_HIGHLOW || type == IMAGE_REL_BASED_DIR64) {
                        LPVOID patchAddr = (LPBYTE)remoteBase + reloc->VirtualAddress + offset;
                        ULONG_PTR value = 0;
                        SIZE_T bytesRead;

                        ReadProcessMemory(hProcess, patchAddr, &value, sizeof(value), &bytesRead);
                        value += delta;
                        WriteProcessMemory(hProcess, patchAddr, &value, sizeof(value), nullptr);
                    }
                }

                processed += reloc->SizeOfBlock;
                reloc = (PIMAGE_BASE_RELOCATION)((LPBYTE)reloc + reloc->SizeOfBlock);
            }
        }
    }

    // Resolve imports
    auto importDir = &optHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir->Size > 0) {
        // Find import section
        section = IMAGE_FIRST_SECTION(ntHeaders);
        PIMAGE_IMPORT_DESCRIPTOR importDesc = nullptr;

        for (WORD i = 0; i < fileHeader->NumberOfSections; i++, section++) {
            if (importDir->VirtualAddress >= section->VirtualAddress &&
                importDir->VirtualAddress < section->VirtualAddress + section->Misc.VirtualSize) {
                importDesc = (PIMAGE_IMPORT_DESCRIPTOR)(fileData.data() + section->PointerToRawData +
                    (importDir->VirtualAddress - section->VirtualAddress));
                break;
            }
        }

        if (importDesc) {
            for (; importDesc->Name != 0; importDesc++) {
                // Get DLL name from file data
                section = IMAGE_FIRST_SECTION(ntHeaders);
                char* dllName = nullptr;
                for (WORD i = 0; i < fileHeader->NumberOfSections; i++, section++) {
                    if (importDesc->Name >= section->VirtualAddress &&
                        importDesc->Name < section->VirtualAddress + section->Misc.VirtualSize) {
                        dllName = (char*)(fileData.data() + section->PointerToRawData +
                            (importDesc->Name - section->VirtualAddress));
                        break;
                    }
                }

                if (!dllName) continue;

                HMODULE hMod = LoadLibraryA(dllName);
                if (!hMod) continue;

                PIMAGE_THUNK_DATA thunk = nullptr;
                PIMAGE_THUNK_DATA origThunk = nullptr;

                section = IMAGE_FIRST_SECTION(ntHeaders);
                for (WORD i = 0; i < fileHeader->NumberOfSections; i++, section++) {
                    if (importDesc->FirstThunk >= section->VirtualAddress &&
                        importDesc->FirstThunk < section->VirtualAddress + section->Misc.VirtualSize) {
                        thunk = (PIMAGE_THUNK_DATA)(fileData.data() + section->PointerToRawData +
                            (importDesc->FirstThunk - section->VirtualAddress));
                        break;
                    }
                }

                DWORD origThunkRVA = importDesc->OriginalFirstThunk ? importDesc->OriginalFirstThunk : importDesc->FirstThunk;
                section = IMAGE_FIRST_SECTION(ntHeaders);
                for (WORD i = 0; i < fileHeader->NumberOfSections; i++, section++) {
                    if (origThunkRVA >= section->VirtualAddress &&
                        origThunkRVA < section->VirtualAddress + section->Misc.VirtualSize) {
                        origThunk = (PIMAGE_THUNK_DATA)(fileData.data() + section->PointerToRawData +
                            (origThunkRVA - section->VirtualAddress));
                        break;
                    }
                }

                if (!thunk || !origThunk) continue;

                for (; origThunk->u1.AddressOfData != 0; origThunk++, thunk++) {
                    FARPROC func = nullptr;

                    if (IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal)) {
                        func = GetProcAddress(hMod, (LPCSTR)IMAGE_ORDINAL(origThunk->u1.Ordinal));
                    } else {
                        // Get function name
                        PIMAGE_IMPORT_BY_NAME importByName = nullptr;
                        section = IMAGE_FIRST_SECTION(ntHeaders);
                        for (WORD i = 0; i < fileHeader->NumberOfSections; i++, section++) {
                            if (origThunk->u1.AddressOfData >= section->VirtualAddress &&
                                origThunk->u1.AddressOfData < section->VirtualAddress + section->Misc.VirtualSize) {
                                importByName = (PIMAGE_IMPORT_BY_NAME)(fileData.data() + section->PointerToRawData +
                                    (origThunk->u1.AddressOfData - section->VirtualAddress));
                                break;
                            }
                        }
                        if (importByName) {
                            func = GetProcAddress(hMod, importByName->Name);
                        }
                    }

                    if (func) {
                        LPVOID iatAddr = (LPBYTE)remoteBase + importDesc->FirstThunk +
                            ((LPBYTE)thunk - (LPBYTE)(fileData.data() +
                            ((PIMAGE_SECTION_HEADER)((LPBYTE)thunk - (origThunkRVA - importDesc->FirstThunk)))->PointerToRawData));

                        // Simpler approach: calculate IAT address
                        SIZE_T thunkOffset = (LPBYTE)thunk - fileData.data();
                        section = IMAGE_FIRST_SECTION(ntHeaders);
                        for (WORD i = 0; i < fileHeader->NumberOfSections; i++, section++) {
                            if (thunkOffset >= section->PointerToRawData &&
                                thunkOffset < section->PointerToRawData + section->SizeOfRawData) {
                                iatAddr = (LPBYTE)remoteBase + section->VirtualAddress +
                                    (thunkOffset - section->PointerToRawData);
                                break;
                            }
                        }

                        WriteProcessMemory(hProcess, iatAddr, &func, sizeof(func), nullptr);
                    }
                }
            }
        }
    }

    // Prepare and write shellcode
    LPVOID entryPoint = (LPBYTE)remoteBase + optHeader->AddressOfEntryPoint;

    memcpy(Shellcode + SHELLCODE_DLLBASE_OFFSET, &remoteBase, sizeof(remoteBase));
    memcpy(Shellcode + SHELLCODE_ENTRY_OFFSET, &entryPoint, sizeof(entryPoint));

    LPVOID shellcodeAddr = VirtualAllocEx(hProcess, nullptr, sizeof(Shellcode),
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!shellcodeAddr) {
        error = "Failed to allocate shellcode memory";
        VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
        return false;
    }

    WriteProcessMemory(hProcess, shellcodeAddr, Shellcode, sizeof(Shellcode), nullptr);

    // Execute shellcode
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
        (LPTHREAD_START_ROUTINE)shellcodeAddr, nullptr, 0, nullptr);
    if (!hThread) {
        error = "Failed to create remote thread (Error: " + std::to_string(GetLastError()) + ")";
        VirtualFreeEx(hProcess, shellcodeAddr, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);

    // Cleanup shellcode (keep DLL mapped)
    VirtualFreeEx(hProcess, shellcodeAddr, 0, MEM_RELEASE);

    return true;
}

} // namespace ManualMap
