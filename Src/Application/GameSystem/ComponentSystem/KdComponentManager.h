#pragma once

#include <cstdint>
#include <cstdlib>
#include <span>
#include <functional>
#include <cassert>
#include <algorithm>
#include <type_traits>
#include <new>
#if defined(_WIN32)
    #include <malloc.h>
#endif

#ifndef KD_ECS_ASSERT
    #define KD_ECS_ASSERT(cond, msg) assert((cond) && (msg))
#endif

namespace kd::ecs {

// =============================================================================
//  定数 / Entity
// =============================================================================
inline constexpr std::uint32_t kInvalidStateIndex = 0xFFFFFFFFu;

struct Entity {
    std::uint32_t index      = kInvalidStateIndex;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool isValid() const noexcept { return index != kInvalidStateIndex; }
    friend constexpr bool operator==(const Entity& a, const Entity& b) noexcept {
        return a.index == b.index && a.generation == b.generation;
    }
    friend constexpr bool operator!=(const Entity& a, const Entity& b) noexcept { return !(a == b); }
};

struct EntityHash {
    std::size_t operator()(const Entity& e) const noexcept {
        return (static_cast<std::uint64_t>(e.generation) << 32) | e.index;
    }
};

// =============================================================================
//  Component コンセプト (trivially copyable のみ許可 → memcpy/GPU転送が安全)
// =============================================================================
template<class T>
concept Component =
    std::is_trivially_copyable_v<T> &&
    std::is_default_constructible_v<T> &&
    !std::is_pointer_v<T>;

// =============================================================================
//  16byte アライメント アロケータ (XMVECTOR / XMMATRIX 対応)
// =============================================================================
namespace detail {
    inline void* alignedAlloc(std::size_t bytes, std::size_t align) noexcept {
    #if defined(_WIN32)
        return _aligned_malloc(bytes, align);
    #else
        void* p = nullptr;
        if (::posix_memalign(&p, align, bytes) != 0) p = nullptr;
        return p;
    #endif
    }
    inline void alignedFree(void* p) noexcept {
    #if defined(_WIN32)
        _aligned_free(p);
    #else
        std::free(p);
    #endif
    }
}

template<class T>
struct AlignedAllocator {
    using value_type = T;
    static constexpr std::size_t alignment = (alignof(T) > 16 ? alignof(T) : 16);
    AlignedAllocator() noexcept = default;
    template<class U> AlignedAllocator(const AlignedAllocator<U>&) noexcept {}
    [[nodiscard]] T* allocate(std::size_t n) {
        if (n == 0) return nullptr;
        void* p = detail::alignedAlloc(n * sizeof(T), alignment);
        if (!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    void deallocate(T* p, std::size_t) noexcept { detail::alignedFree(p); }
    template<class U> bool operator==(const AlignedAllocator<U>&) const noexcept { return true; }
    template<class U> bool operator!=(const AlignedAllocator<U>&) const noexcept { return false; }
};
template<class T> using AlignedVector = std::vector<T, AlignedAllocator<T>>;

// =============================================================================
//  コンポーネント型IDの自動割り当て
// =============================================================================
namespace detail {
    inline std::uint32_t nextComponentTypeId() noexcept { static std::uint32_t c = 0; return c++; }
    template<class T> inline std::uint32_t componentTypeId() noexcept {
        static const std::uint32_t id = nextComponentTypeId(); return id;
    }
}

// =============================================================================
//  EntityManager
// =============================================================================
class EntityManager {
public:
    [[nodiscard]] Entity create() {
        if (!freeList_.empty()) {
            const std::uint32_t idx = freeList_.back(); freeList_.pop_back();
            live_[idx] = 1;
            return Entity{ idx, generations_[idx] };
        }
        const std::uint32_t idx = static_cast<std::uint32_t>(generations_.size());
        generations_.push_back(0);
        live_.push_back(1);
        return Entity{ idx, 0 };
    }
    void destroy(Entity e) {
        KD_ECS_ASSERT(isAlive(e), "destroy: 破棄済み or 不正なエンティティ");
        ++generations_[e.index];
        live_[e.index] = 0;
        freeList_.push_back(e.index);
    }
    [[nodiscard]] bool isAlive(Entity e) const noexcept {
        return e.index < generations_.size() && generations_[e.index] == e.generation;
    }
    // 生存エンティティ列挙 (エディタ/デバッグ用): fn(Entity)
    template<class F> void forEachAlive(F&& fn) const {
        for (std::uint32_t i = 0; i < live_.size(); ++i)
            if (live_[i]) fn(Entity{ i, generations_[i] });
    }
    [[nodiscard]] std::size_t aliveCount() const noexcept {
        std::size_t n = 0; for (auto f : live_) n += f; return n;
    }
    void reset() { generations_.clear(); freeList_.clear(); live_.clear(); }
private:
    std::vector<std::uint32_t> generations_;
    std::vector<std::uint32_t> freeList_;
    std::vector<std::uint8_t>  live_;       // index -> 生存フラグ (列挙の高速化)
};

// =============================================================================
//  型消去プール基底
// =============================================================================
class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void removeIfPresent(Entity e) = 0;
    virtual void clear()                   = 0;
    virtual void swapAndBaseline()         = 0; // sync() で呼ぶ
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
};

// =============================================================================
//  ComponentPool<T> : SoA + Sparse Set + ダブルバッファ値 + dirty
//    構造 : entities_ / sparse_ (単一)
//    値   : buf_[0], buf_[1]        (read_/write_ で切替)
//    dirty: dirty_[0], dirty_[1]    (値と同じ添字でスワップ)
//    fullUpload_[2]: 構造変化時に「全件アップロード」を要求するフラグ
// =============================================================================
template<Component T>
class ComponentPool final : public IComponentPool {
public:
    // ---------------- 構造変更 (即時。sync時 or セットアップ時のみ) ----------------
    T& add(Entity e, const T& value) {
        ensureSparse(e.index);
        std::uint32_t d = sparse_[e.index];
        if (d != kInvalidStateIndex && d < entities_.size() && entities_[d].index == e.index) {
            entities_[d] = e;                 // スロット再利用 (世代違い含む)
            buf_[0][d] = value; buf_[1][d] = value;
            dirty_[0][d] = 1;   dirty_[1][d] = 1;
            return buf_[write_][d];
        }
        d = static_cast<std::uint32_t>(entities_.size());
        sparse_[e.index] = d;
        entities_.push_back(e);
        buf_[0].push_back(value); buf_[1].push_back(value);
        dirty_[0].push_back(1);   dirty_[1].push_back(1);
        markStructural();
        return buf_[write_][d];
    }

    void remove(Entity e) {
        if (!has(e)) return;
        const std::uint32_t d    = sparse_[e.index];
        const std::uint32_t last = static_cast<std::uint32_t>(entities_.size()) - 1;
        if (d != last) {
            entities_[d] = entities_[last];   // swap-and-pop を両バッファで同一に
            buf_[0][d] = buf_[0][last];
            buf_[1][d] = buf_[1][last];
            dirty_[0][d] = dirty_[0][last];
            dirty_[1][d] = dirty_[1][last];
            sparse_[entities_[d].index] = d;
        }
        entities_.pop_back();
        buf_[0].pop_back(); buf_[1].pop_back();
        dirty_[0].pop_back(); dirty_[1].pop_back();
        sparse_[e.index] = kInvalidStateIndex;
        markStructural();
    }

    [[nodiscard]] bool has(Entity e) const noexcept {
        if (e.index >= sparse_.size()) return false;
        const std::uint32_t d = sparse_[e.index];
        return d != kInvalidStateIndex && d < entities_.size() && entities_[d] == e;
    }

    // ---------------- 更新(write)側アクセス ----------------
    [[nodiscard]] T& getWrite(Entity e) {
        KD_ECS_ASSERT(has(e), "getWrite: 未所持 or 破棄済み");
        const std::uint32_t d = sparse_[e.index];
        dirty_[write_][d] = 1;                 // 変更検知: 触れたら dirty
        return buf_[write_][d];
    }
    void set(Entity e, const T& v) {
        KD_ECS_ASSERT(has(e), "set: 未所持 or 破棄済み");
        const std::uint32_t d = sparse_[e.index];
        buf_[write_][d] = v;
        dirty_[write_][d] = 1;
    }
    [[nodiscard]] const T& peekWrite(Entity e) const {  // write値を dirty立てずに読む
        KD_ECS_ASSERT(has(e), "peekWrite: 未所持");
        return buf_[write_][sparse_[e.index]];
    }

    // ---------------- 描画(read)側アクセス ----------------
    [[nodiscard]] const T& getRead(Entity e) const {
        KD_ECS_ASSERT(has(e), "getRead: 未所持");
        return buf_[read_][sparse_[e.index]];
    }
    [[nodiscard]] std::span<const T>      readComponents() const noexcept { return { buf_[read_].data(),  buf_[read_].size() }; }
    [[nodiscard]] std::span<const Entity> entitiesSpan()   const noexcept { return { entities_.data(),    entities_.size() }; }
    [[nodiscard]] const T* readData() const noexcept { return buf_[read_].data(); } // GPU一括転送用

    // sim側span (mutable view/each 用)
    [[nodiscard]] std::span<T> writeComponents() noexcept { return { buf_[write_].data(), buf_[write_].size() }; }

    // ---------------- 変更検知の消費 (描画スレッド) ----------------
    //   fn(Entity, const T&, std::uint32_t denseIndex) を dirty な要素だけ呼ぶ。
    //   構造変化フレームは全件。呼び終えたら read側の dirty/fullUpload をクリア。
    template<class F>
    void forEachDirtyRead(F&& fn) {
        auto& d = dirty_[read_];
        auto& b = buf_[read_];
        if (fullUpload_[read_]) {
            for (std::uint32_t i = 0; i < b.size(); ++i) fn(entities_[i], b[i], i);
        } else {
            for (std::uint32_t i = 0; i < b.size(); ++i) if (d[i]) fn(entities_[i], b[i], i);
        }
        std::fill(d.begin(), d.end(), std::uint8_t{0});
        fullUpload_[read_] = false;
    }

    // ---------------- sync: スワップ + ベースラインコピー ----------------
    void swapAndBaseline() override {
        std::swap(read_, write_);
        buf_[write_] = buf_[read_];   // trivially copyable → 実質 memcpy
        // 次フレームの write 側トラッキングを初期化 (read側は描画が消費するので触らない)
        std::fill(dirty_[write_].begin(), dirty_[write_].end(), std::uint8_t{0});
        fullUpload_[write_] = false;
    }

    // ---------------- その他 ----------------
    void reserve(std::size_t n) {
        buf_[0].reserve(n); buf_[1].reserve(n);
        dirty_[0].reserve(n); dirty_[1].reserve(n);
        entities_.reserve(n);
    }
    void removeIfPresent(Entity e) override { remove(e); }
    void clear() override {
        entities_.clear();
        buf_[0].clear();   buf_[1].clear();
        dirty_[0].clear(); dirty_[1].clear();
        std::fill(sparse_.begin(), sparse_.end(), kInvalidStateIndex);
        fullUpload_[0] = fullUpload_[1] = true;
    }
    [[nodiscard]] std::size_t size() const noexcept override { return entities_.size(); }

private:
    void ensureSparse(std::uint32_t idx) { if (idx >= sparse_.size()) sparse_.resize(idx + 1, kInvalidStateIndex); }
    void markStructural() { fullUpload_[0] = true; fullUpload_[1] = true; } // 構造変化→全件再転送

    AlignedVector<T>           buf_[2];
    std::vector<Entity>        entities_;
    std::vector<std::uint32_t> sparse_;
    std::vector<std::uint8_t>  dirty_[2];
    bool                       fullUpload_[2] = { false, false };
    int read_ = 0, write_ = 1;
};

// =============================================================================
//  ComponentManager
// =============================================================================
class ComponentManager {
public:
    // ---- エンティティ (即時。並行フェーズ中は CommandBuffer 経由で) ----
    [[nodiscard]] Entity createEntity() { return entities_.create(); }
    void destroyEntity(Entity e) {
        if (!entities_.isAlive(e)) return;
        for (auto& p : pools_) if (p) p->removeIfPresent(e);
        entities_.destroy(e);
    }
    [[nodiscard]] bool isAlive(Entity e) const noexcept { return entities_.isAlive(e); }
    // 生存エンティティ列挙 (エディタ/デバッグ用): fn(Entity)
    template<class F> void forEachAliveEntity(F&& fn) const { entities_.forEachAlive(std::forward<F>(fn)); }
    [[nodiscard]] std::size_t aliveEntityCount() const noexcept { return entities_.aliveCount(); }
    void clear() { for (auto& p : pools_) if (p) p->clear(); entities_.reset(); }

    // ---- コンポーネント (即時構造変更) ----
    template<Component T> T& add(Entity e, const T& value = {}) {
        KD_ECS_ASSERT(entities_.isAlive(e), "add: 破棄済み or 不正なエンティティ");
        return pool<T>().add(e, value);
    }
    template<Component T> void remove(Entity e) { pool<T>().remove(e); }
    template<Component T> [[nodiscard]] bool has(Entity e) const {
        auto* p = tryPool<T>(); return p && p->has(e);
    }
    template<Component T> [[nodiscard]] std::size_t count() const {
        auto* p = tryPool<T>(); return p ? p->size() : 0;
    }
    template<Component T> void reserve(std::size_t n) { pool<T>().reserve(n); }

    // ---- 値アクセス: 更新(write)側 ----
    template<Component T> [[nodiscard]] T&       get(Entity e)  { return pool<T>().getWrite(e); }   // dirtyを立てる
    template<Component T> void                   set(Entity e, const T& v) { pool<T>().set(e, v); }
    template<Component T> [[nodiscard]] const T& peek(Entity e) const { return constPool<T>().peekWrite(e); } // dirty立てない
    // ---- 値アクセス: 描画(read)側 ----
    template<Component T> [[nodiscard]] const T& readGet(Entity e) const { return constPool<T>().getRead(e); }

    // ---- プール直アクセス ----
    template<Component T> [[nodiscard]] ComponentPool<T>& pool() {
        const std::uint32_t id = detail::componentTypeId<T>();
        if (id >= pools_.size()) pools_.resize(id + 1);
        if (!pools_[id]) pools_[id] = std::make_unique<ComponentPool<T>>();
        return static_cast<ComponentPool<T>&>(*pools_[id]);
    }

    // ---- 更新(write)側 巡回: fn(Entity, T&...) 触れた要素は dirty ----
    template<Component T, class F> void each(F&& fn) {
        auto& p = pool<T>();
        auto ents = p.entitiesSpan();
        for (std::size_t i = 0; i < ents.size(); ++i) fn(ents[i], p.getWrite(ents[i]));
    }
    template<Component... Ts, class F> requires (sizeof...(Ts) >= 1)
    void view(F&& fn) { viewImpl<Ts...>(std::forward<F>(fn)); }

    // ---- 描画(read)側 巡回: const。dirtyを立てない ----
    template<Component T, class F> void readEach(F&& fn) const {
        auto* p = tryPool<T>(); if (!p) return;
        auto ents = p->entitiesSpan();
        auto comps = p->readComponents();
        for (std::size_t i = 0; i < ents.size(); ++i) fn(ents[i], comps[i]);
    }
    template<Component... Ts, class F> requires (sizeof...(Ts) >= 1)
    void readView(F&& fn) const { readViewImpl<Ts...>(std::forward<F>(fn)); }

    // ---- 変更検知の消費 (GPU定数バッファ再転送): fn(Entity, const T&, denseIndex) ----
    template<Component T, class F> void uploadDirty(F&& fn) { pool<T>().forEachDirtyRead(std::forward<F>(fn)); }

    // ---- コマンドバッファ / フレーム同期 ----
    class CommandBuffer;
    void sync(CommandBuffer* cmd = nullptr);

private:
    template<Component T> [[nodiscard]] ComponentPool<T>* tryPool() {
        const std::uint32_t id = detail::componentTypeId<T>();
        return (id < pools_.size() && pools_[id]) ? static_cast<ComponentPool<T>*>(pools_[id].get()) : nullptr;
    }
    template<Component T> [[nodiscard]] const ComponentPool<T>* tryPool() const {
        const std::uint32_t id = detail::componentTypeId<T>();
        return (id < pools_.size() && pools_[id]) ? static_cast<const ComponentPool<T>*>(pools_[id].get()) : nullptr;
    }
    template<Component T> [[nodiscard]] const ComponentPool<T>& constPool() const {
        auto* p = tryPool<T>(); KD_ECS_ASSERT(p, "未登録コンポーネント型"); return *p;
    }

    template<Component Driver, Component... Rest, class F> void viewImpl(F&& fn) {
        auto& dp = pool<Driver>();
        auto ents = dp.entitiesSpan();
        for (std::size_t i = 0; i < ents.size(); ++i) {
            const Entity e = ents[i];
            if ((has<Rest>(e) && ...)) fn(e, dp.getWrite(e), pool<Rest>().getWrite(e)...);
        }
    }
    template<Component Driver, Component... Rest, class F> void readViewImpl(F&& fn) const {
        auto* dp = tryPool<Driver>(); if (!dp) return;
        auto ents = dp->entitiesSpan();
        auto comps = dp->readComponents();
        for (std::size_t i = 0; i < ents.size(); ++i) {
            const Entity e = ents[i];
            if ((has<Rest>(e) && ...)) fn(e, comps[i], constPool<Rest>().getRead(e)...);
        }
    }

    EntityManager entities_;
    std::vector<std::unique_ptr<IComponentPool>> pools_;
};

// =============================================================================
//  CommandBuffer : 構造変更を遅延記録し sync() で一括適用
//    - フレーム中(並行フェーズ)に更新スレッドから安全に積める
//    - create() は遅延生成トークンを返し、同一バッチ内で add(token, ...) できる
// =============================================================================
class ComponentManager::CommandBuffer {
public:
    struct Deferred { std::uint32_t token = kInvalidStateIndex; };

    [[nodiscard]] Deferred create() {
        created_.push_back(Entity{});
        return Deferred{ static_cast<std::uint32_t>(created_.size() - 1) };
    }
    template<Component T> void add(Entity e, const T& v = {}) {
        ops_.emplace_back([e, v](ComponentManager& m, CommandBuffer&) { m.add<T>(e, v); });
    }
    template<Component T> void add(Deferred d, const T& v = {}) {
        ops_.emplace_back([d, v](ComponentManager& m, CommandBuffer& cb) { m.add<T>(cb.created_[d.token], v); });
    }
    template<Component T> void remove(Entity e) {
        ops_.emplace_back([e](ComponentManager& m, CommandBuffer&) { m.remove<T>(e); });
    }
    void destroy(Entity e) {
        ops_.emplace_back([e](ComponentManager& m, CommandBuffer&) { m.destroyEntity(e); });
    }

    [[nodiscard]] bool empty() const noexcept { return ops_.empty() && created_.empty(); }
    [[nodiscard]] Entity resolve(Deferred d) const { return created_[d.token]; } // flush後 reset前まで有効
    void reset() { ops_.clear(); created_.clear(); }

private:
    friend class ComponentManager;
    void flush(ComponentManager& m) {
        for (auto& e : created_) e = m.createEntity(); // 先に全トークンを実体化
        for (auto& op : ops_) op(m, *this);
        ops_.clear();                                   // created_ は resolve 用に残す
    }
    std::vector<Entity> created_;
    std::vector<std::function<void(ComponentManager&, CommandBuffer&)>> ops_;
};

inline void ComponentManager::sync(CommandBuffer* cmd) {
    if (cmd) cmd->flush(*this);                          // 1) 構造変更を単一スレッド適用
    for (auto& p : pools_) if (p) p->swapAndBaseline();  // 2) read/write スワップ + ベースライン
}

} 
