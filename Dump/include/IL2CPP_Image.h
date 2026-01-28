#pragma once

#include <string>
#include "IL2CPP_Class.h"

class IL2CPP_Image {
public:
    void* handle = nullptr;

    IL2CPP_Image() = default;
    explicit IL2CPP_Image(void* ptr) : handle(ptr) {}

    std::string GetName() const;
    size_t GetClassCount() const;
    IL2CPP_Class GetClass(size_t index) const;
};
