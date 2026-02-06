# Memory Management Optimization Strategy

## Current Analysis

### Good Practices ✅
- **Smart Pointers**: Using `std::unique_ptr` and `std::shared_ptr` throughout
- **No raw `new`/`delete`**: Memory automatically cleaned up
- **RAII Pattern**: Resources tied to object lifetime
- **Thread-safe operations**: Using `std::lock_guard` for mutex protection

### Optimization Opportunities 🎯

#### 1. **Container Memory Management**
- Queues for transactions can grow unbounded
- Solution: Add capacity limits with LRU eviction

#### 2. **PeerConnection Boost Interop**
- Using `boost::shared_ptr` instead of `std::shared_ptr`
- Solution: Modernize to C++17 `std::shared_ptr`

#### 3. **Network Manager Raw Pointers**
- Returns `BlockchainNode*` from methods
- Solution: Return safe references via weak_ptr or const references

#### 4. **Vector Allocations**
- Creating temporary vectors during blockchain sync
- Solution: Use move semantics and reserve capacity

#### 5. **JSON Parsing**
- Implicit copies during JSON parsing
- Solution: Use move assignment

---

## Optimization Plan

### Phase 1: Container Limits (Low Impact, High Benefit)
Add capacity limits to mempool and pending message queues to prevent unbounded growth.

### Phase 2: Smart Pointer Migration (Medium Impact, High Benefit)
Replace Boost shared_ptr with std::shared_ptr for consistency.

### Phase 3: Reference Optimization (Low Impact, Medium Benefit)
Use const references and weak_ptr where appropriate instead of raw pointers.

### Phase 4: Vector Optimization (Low Impact, Low Benefit)
Use move semantics and reserve capacity for vector operations.

---

## Benchmarking Targets

| Metric | Current | Target | Benefit |
|--------|---------|--------|---------|
| Memory per node | ~30-50 MB | ~20-25 MB | 40% reduction |
| Mempool growth time | Unbounded | Capped at 1000 tx | DoS prevention |
| Smart pointer overhead | ~8 bytes/ptr | ~8 bytes/ptr | No change (already optimal) |
| JSON copy overhead | 2-3 copies | 1 copy | 50% reduction |

---

## Implementation

See MEMORY_OPTIMIZATION_IMPL.md for detailed code changes.
