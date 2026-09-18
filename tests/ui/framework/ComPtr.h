// tests/ui/framework/ComPtr.h — minimal COM RAII (no MSVC WRL dependency).
#pragma once
namespace uitest {
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    // Takes ownership of a raw pointer (no AddRef).
    explicit ComPtr(T* p) : ptr_(p) {}
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T** operator&() { reset(); return &ptr_; }          // for COM out-params
    T* operator->() const { return ptr_; }
    T* get() const { return ptr_; }
    // Returns a new owning reference to the held element (AddRef), for handing
    // ownership to wrappers that Release in their destructor.
    T* acquire() const { if (ptr_) ptr_->AddRef(); return ptr_; }
    T* release() { T* p = ptr_; ptr_ = nullptr; return p; }
    void reset() { if (ptr_) { ptr_->Release(); ptr_ = nullptr; } }
    void reset(T* p) { if (p != ptr_) { reset(); ptr_ = p; } }
private:
    T* ptr_ = nullptr;
};
}
