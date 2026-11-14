// ZDX CORE-0 — lock-free single-producer / single-consumer ring.
//
// The one primitive allowed to cross ISR->loop and core0<->core1 boundaries
// without a critical section. Exactly ONE producer context and ONE consumer
// context per ring. N must be a power of two; full capacity N is usable
// (indices are free-running and masked).
#pragma once
#include "types.h"

namespace core0 {

template <typename T, size_t N>
class SpscRing {
  static_assert(N >= 2 && (N & (N - 1)) == 0, "SpscRing size must be a power of two");

 public:
  SpscRing() : head_(0), tail_(0) {}

  // Producer side ----------------------------------------------------------
  bool push(const T& v) {
    const size_t h = head_;
    if (h - tail_ >= N) return false;  // full
    buf_[h & (N - 1)] = v;
    CORE0_MEMORY_BARRIER();            // publish data before index
    head_ = h + 1;
    return true;
  }
  size_t free_space() const { return N - (head_ - tail_); }

  // Consumer side ----------------------------------------------------------
  bool pop(T& out) {
    const size_t t = tail_;
    if (head_ == t) return false;      // empty
    CORE0_MEMORY_BARRIER();            // read index before data
    out = buf_[t & (N - 1)];
    CORE0_MEMORY_BARRIER();
    tail_ = t + 1;
    return true;
  }
  bool   peek(T& out) const {
    if (head_ == tail_) return false;
    out = buf_[tail_ & (N - 1)];
    return true;
  }
  size_t size() const { return head_ - tail_; }
  bool   empty() const { return head_ == tail_; }

  static constexpr size_t capacity() { return N; }

 private:
  T buf_[N];
  volatile size_t head_;  // written by producer only
  volatile size_t tail_;  // written by consumer only
};

}  // namespace core0
