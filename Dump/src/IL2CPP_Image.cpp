#include "../include/IL2CPP_Image.h"
#include "../include/IL2CPP_API.h"

std::string IL2CPP_Image::GetName() const {
    if (!handle) return "";
    const char* name = IL2CPP::ImageGetName(handle);
    return name ? name : "";
}

size_t IL2CPP_Image::GetClassCount() const {
    return handle ? IL2CPP::ImageGetClassCount(handle) : 0;
}

IL2CPP_Class IL2CPP_Image::GetClass(size_t index) const {
    if (!handle) return IL2CPP_Class();
    return IL2CPP_Class(IL2CPP::ImageGetClass(handle, index));
}
