#pragma once
#include "common.h"
#include <unknwn.h>
namespace cronq {
template <typename T>
class ComPtr {
   public:
    ComPtr() = default;
    ComPtr(T* p) : p_(p) {
        if (p_) p_->AddRef();
    }
    ComPtr(const ComPtr& o) : p_(o.p_) {
        if (p_) p_->AddRef();
    }
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ~ComPtr() { Reset(); }

    ComPtr& operator=(const ComPtr& o) {
        if (this != &o) {
            if (o.p_) o.p_->AddRef();
            Reset();
            p_ = o.p_;
        }
        return *this;
    }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) {
            Reset();
            p_ = o.p_;
            o.p_ = nullptr;
        }
        return *this;
    }

    T* Get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }

    // For out-parameters: `CoCreate(..., ptr.ReleaseAndGetAddressOf())`.
    T** ReleaseAndGetAddressOf() {
        Reset();
        return &p_;
    }
    void Reset() {
        if (p_) {
            p_->Release();
            p_ = nullptr;
        }
    }
    // Takes ownership of a pointer that already carries a reference.
    void Attach(T* p) {
        Reset();
        p_ = p;
    }

    template <typename U>
    HRESULT As(ComPtr<U>* out) const {
        if (!p_) return E_POINTER;
        return p_->QueryInterface(__uuidof(U), reinterpret_cast<void**>(out->ReleaseAndGetAddressOf()));
    }

   private:
    T* p_ = nullptr;
};

}  // namespace cronq
