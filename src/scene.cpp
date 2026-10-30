#include "scene.hpp"
#include "checkpoint.hpp"
#include <flecs.h>
#include <algorithm>
#include <bit>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>

namespace gm {
namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
} // namespace
struct SceneRuntime::Impl {
    std::shared_ptr<GameRegistry> registry;
    flecs::world world;
    struct Type {
        ecs_entity_t id;
        uint32_t size;
    };
    std::unordered_map<sdk::TypeId, Type> types;
    struct Query {
        ecs_query_t *value = nullptr;
        bool active = false;
        ~Query() {
            if (value)
                ecs_query_fini(value);
        }
    };
    std::map<std::array<uint64_t, 17>, std::unique_ptr<Query>> queries;
    struct EntityIndex {
        static constexpr const char *gm_name = "gm.EntityIndex";
        uint32_t value;
    };
    struct RunningSystem {
        sdk::System descriptor;
        void *state = nullptr;
        SystemTiming timing;
    };
    std::vector<RunningSystem> systems;
    std::vector<ecs_entity_t> handles;
    std::vector<SceneEntity> records;
    std::vector<uint32_t> order;
    std::vector<uint32_t> freeSlots;
    uint32_t authoredCount = 0;
    uint64_t runtimeSequence = 1;
    std::vector<std::vector<uint32_t>> children;
    std::unordered_map<std::string, uint32_t> ids;
    std::unordered_map<sdk::Entity, uint32_t> slots;
    enum class Op { Create, Destroy, Set, Remove };
    struct Command {
        Op op;
        sdk::Entity entity;
        sdk::TypeId type = 0;
        std::vector<uint32_t> fields;
    };
    std::vector<Command> commands;
    std::vector<sdk::Event> inbox, outbox;
    const sdk::System *active = nullptr;
    struct Body {
        sdk::Entity entity;
        Rect box;
        uint32_t layer, mask;
    };
    std::vector<Body> bodies;
    std::set<std::pair<sdk::Entity, sdk::Entity>> contacts;
    sdk::Frame frame{};
    std::vector<sdk::CanvasCommand> canvas;
    size_t imageCount = 0;
    bool structureDirty = true;
    uint64_t structureRevision = 0;
    std::string requestedScene;
    static const sdk::Api &api() {
        static const sdk::Api value{sdk::AbiVersion, each,  get,  getMut, create, destroy,    set,
                                    remove,          alive, find, emit,   events, switchScene, draw};
        return value;
    }
    sdk::Context context() {
        return {this, &api(), &frame};
    }
    void access(sdk::TypeId type, bool write) const {
        if (write && type == sdk::id(sdk::WorldPosition::gm_name))
            throw std::runtime_error("WorldPosition is computed and read-only");
        if (!active || !active->accessCount)
            return;
        for (uint32_t i = 0; i < active->accessCount; ++i)
            if (active->access[i].type == type && (!write || active->access[i].write))
                return;
        throw std::runtime_error(std::string(active->name) + ": undeclared component " +
                                 (write ? "write" : "read"));
    }
    static bool alive(void *ptr, sdk::Entity e) {
        auto &s = *static_cast<Impl *>(ptr);
        return s.slots.count(e) && ecs_is_alive(s.world.c_ptr(), e);
    }
    static const void *raw(Impl &s, sdk::Entity e, sdk::TypeId type) {
        auto t = s.types.find(type);
        return t == s.types.end() || !alive(&s, e) ? nullptr : ecs_get_id(s.world.c_ptr(), e, t->second.id);
    }
    static const void *get(void *ptr, sdk::Entity e, sdk::TypeId type) {
        auto &s = *static_cast<Impl *>(ptr);
        s.access(type, false);
        return raw(s, e, type);
    }
    static void *getMut(void *ptr, sdk::Entity e, sdk::TypeId type) {
        auto &s = *static_cast<Impl *>(ptr);
        s.access(type, true);
        return const_cast<void *>(raw(s, e, type));
    }
    static sdk::Entity find(void *ptr, const char *id) {
        auto &s = *static_cast<Impl *>(ptr);
        if (!id)
            return 0;
        auto it = s.ids.find(id);
        return it == s.ids.end() ? 0 : s.handles[it->second];
    }
    void queue(Command c) {
        if (commands.size() >= 100000)
            throw std::runtime_error("Structural command limit exceeded");
        commands.push_back(std::move(c));
    }
    static sdk::Entity create(void *ptr, sdk::Entity parent) {
        auto &s = *static_cast<Impl *>(ptr);
        if (parent && !alive(ptr, parent))
            throw std::runtime_error("Invalid parent entity");
        if (s.freeSlots.empty() && s.records.size() >= 100000)
            throw std::runtime_error("Runtime entity slot limit exceeded");
        // Empty entities do not match component queries. Components are added at commit.
        uint32_t depth = 1;
        for (auto p = parent; p;) {
            if (++depth > 128)
                throw std::runtime_error("Runtime hierarchy exceeds 128 levels");
            auto index = s.records[s.slots.at(p)].parent;
            p = index == NoIndex ? 0 : s.handles[index];
        }
        std::string id;
        do {
            if(!s.runtimeSequence)throw std::runtime_error("Runtime entity identity exhausted");
            id="runtime-"+std::to_string(s.runtimeSequence++);
        } while(s.ids.count(id));
        auto e = ecs_new(s.world.c_ptr());
        uint32_t index;
        if (s.freeSlots.empty()) {
            index = uint32_t(s.records.size());
            s.handles.push_back(e);
            s.records.emplace_back();
            s.children.emplace_back();
        } else {
            index = s.freeSlots.back();
            s.freeSlots.pop_back();
            s.handles[index] = e;
        }
        s.slots[e] = index;
        s.ids[id] = index;
        s.records[index] = {id, parent ? s.slots.at(parent) : NoIndex, {}};
        if (parent)
            s.children[s.slots.at(parent)].push_back(index);
        s.queue({Op::Create, e});
        return e;
    }
    static void destroy(void *ptr, sdk::Entity e) {
        auto &s = *static_cast<Impl *>(ptr);
        if (!alive(ptr, e))
            throw std::runtime_error("Entity is not alive");
        s.queue({Op::Destroy, e});
    }
    static void set(void *ptr, sdk::Entity e, sdk::TypeId type, const void *data) {
        auto &s = *static_cast<Impl *>(ptr);
        s.access(type, true);
        if (!alive(ptr, e) || !data)
            throw std::runtime_error("Invalid component target");
        auto fields = s.registry->encode(type, data);
        s.validate(type, fields);
        s.queue({Op::Set, e, type, std::move(fields)});
    }
    static void remove(void *ptr, sdk::Entity e, sdk::TypeId type) {
        auto &s = *static_cast<Impl *>(ptr);
        s.access(type, true);
        s.registry->component(type);
        if (!alive(ptr, e))
            throw std::runtime_error("Entity is not alive");
        s.queue({Op::Remove, e, type});
    }
    static void emit(void *ptr, const sdk::Event *event) {
        auto &s = *static_cast<Impl *>(ptr);
        if (!event || !event->type || event->size > 64 || s.outbox.size() >= 65536)
            throw std::runtime_error("Invalid event or event queue full");
        s.outbox.push_back(*event);
    }
    static void events(void *ptr, sdk::TypeId type, void *user, void (*visit)(void *, const sdk::Event *)) {
        if (!visit)
            throw std::runtime_error("Missing event visitor");
        for (const auto &event : static_cast<Impl *>(ptr)->inbox)
            if (event.type == type)
                visit(user, &event);
    }
    static void switchScene(void *ptr, const char *id) {
        if (!id || !*id || std::strlen(id) > 128)
            throw std::runtime_error("Invalid scene ID/name");
        static_cast<Impl *>(ptr)->requestedScene = id;
    }
    static void draw(void *ptr, const sdk::CanvasCommand *command) {
        if (!command || uint32_t(command->kind) > uint32_t(sdk::CanvasKind::ResetClip) ||
            !std::isfinite(command->x) || !std::isfinite(command->y) ||
            !std::isfinite(command->w) || !std::isfinite(command->h) ||
            !std::isfinite(command->size) || command->size < 0 ||
            !std::memchr(command->text, 0, sizeof(command->text)))
            throw std::runtime_error("Invalid canvas command");
        auto &commands = static_cast<Impl *>(ptr)->canvas;
        if (commands.size() >= 65536) throw std::runtime_error("Canvas command limit exceeded");
        commands.push_back(*command);
    }
    void validate(sdk::TypeId type, std::span<const uint32_t> fields) {
        registry->validate(type, fields);
        const auto &d = registry->component(type).descriptor;
        for (uint32_t i = 0; i < d.fieldCount; ++i)
            if (d.fields[i].kind == sdk::Kind::Image && fields[i] != NoIndex && fields[i] >= imageCount)
                throw std::runtime_error("Image index is outside the scene image bank");
    }
    void assign(sdk::Entity e, sdk::TypeId type, std::span<const uint32_t> fields) {
        const auto &d = registry->component(type).descriptor;
        auto &record = records.at(slots.at(e));
        for (uint32_t i = 0; i < d.requiresCount; ++i) {
            const auto &dependency = registry->component(d.requiresTypes[i]);
            if (!findValue(record, dependency.id)) {
                const auto &dd = dependency.descriptor;
                std::vector<uint32_t> defaults;
                for (uint32_t f = 0; f < dd.fieldCount; ++f) {
                    const auto &field = dd.fields[f];
                    defaults.push_back(field.kind == sdk::Kind::Float
                                           ? std::bit_cast<uint32_t>(float(field.initial))
                                           : uint32_t(int64_t(field.initial)));
                }
                validate(dependency.id, defaults);
                assign(e, dependency.id, defaults);
            }
        }
        std::vector<std::max_align_t> data((d.size + sizeof(std::max_align_t) - 1) /
                                           sizeof(std::max_align_t));
        registry->decode(type, fields, data.data());
        ecs_set_id(world.c_ptr(), e, types.at(type).id, d.size, data.data());
        if (!findValue(record, type)) {
            record.components.push_back({type, d.version, {}});
            structureDirty = true;
        }
    }
    void destroyTree(uint32_t index) {
        if (!handles[index])
            return;
        auto descendants = std::move(children[index]);
        children[index].clear();
        for (auto child : descendants)
            destroyTree(child);
        auto parent = records[index].parent;
        if (parent != NoIndex) {
            auto &siblings = children[parent];
            siblings.erase(std::remove(siblings.begin(), siblings.end(), index), siblings.end());
        }
        auto e = handles[index];
        ids.erase(records[index].id);
        slots.erase(e);
        ecs_delete(world.c_ptr(), e);
        handles[index] = 0;
        records[index].components.clear();
        if (index >= authoredCount)
            freeSlots.push_back(index);
        structureDirty = true;
    }
    void internal(uint32_t i) {
        auto e = handles[i];
        EntityIndex index{i};
        sdk::WorldPosition p{};
        ecs_set_id(world.c_ptr(), e, types.at(sdk::id(EntityIndex::gm_name)).id, sizeof(index), &index);
        ecs_set_id(world.c_ptr(), e, types.at(sdk::id(sdk::WorldPosition::gm_name)).id, sizeof(p), &p);
        structureDirty = true;
    }
    void commit() {
        auto pending = std::move(commands);
        commands.clear();
        for (auto &c : pending) {
            if (!alive(this, c.entity))
                continue;
            auto index = slots.at(c.entity);
            if (c.op == Op::Create)
                internal(index);
            else if (c.op == Op::Destroy)
                destroyTree(index);
            else if (c.op == Op::Set)
                assign(c.entity, c.type, c.fields);
            else {
                auto &record = records[index];
                for (const auto &other : record.components) {
                    if (other.type == c.type)
                        continue;
                    const auto &d = registry->component(other.type).descriptor;
                    for (uint32_t i = 0; i < d.requiresCount; ++i)
                        if (sdk::id(d.requiresTypes[i]) == c.type)
                            throw std::runtime_error(std::string(d.name) + " requires the removed component");
                }
                ecs_remove_id(world.c_ptr(), c.entity, types.at(c.type).id);
                record.components.erase(std::remove_if(record.components.begin(), record.components.end(),
                                                       [&](const auto &v) { return v.type == c.type; }),
                                        record.components.end());
                structureDirty = true;
            }
        }
        if (structureDirty)
            reorder();
    }
    void reorder() {
        order.clear();
        std::vector<uint8_t> visited(records.size());
        std::function<void(uint32_t, uint32_t)> visit = [&](uint32_t i, uint32_t depth) {
            if (!handles[i] || visited[i] == 2)
                return;
            if (depth > 128 || visited[i] == 1)
                throw std::runtime_error("Invalid runtime hierarchy");
            visited[i] = 1;
            auto parent = records[i].parent;
            if (parent != NoIndex && handles[parent])
                visit(parent, depth + 1);
            visited[i] = 2;
            order.push_back(i);
        };
        for (uint32_t i = 0; i < records.size(); ++i)
            visit(i, 0);
        structureDirty = false;
        ++structureRevision;
    }
    static void each(void *ptr, const sdk::Term *terms, uint32_t count, void *user,
                     void (*callback)(void *, const sdk::Batch *)) {
        auto &self = *static_cast<Impl *>(ptr);
        if (!count || count > 8 || !terms || !callback)
            throw std::runtime_error("Invalid native query");
        std::array<uint64_t, 17> key{};
        key[0] = count;
        for (uint32_t i = 0; i < count; ++i) {
            self.access(terms[i].type, terms[i].write != 0);
            key[1 + i * 2] = terms[i].type;
            key[2 + i * 2] = terms[i].write;
        }
        auto found = self.queries.find(key);
        if (found == self.queries.end()) {
            ecs_query_desc_t desc{};
            desc.cache_kind = EcsQueryCacheAll;
            for (uint32_t i = 0; i < count; ++i) {
                auto t = self.types.find(terms[i].type);
                if (t == self.types.end())
                    throw std::runtime_error("System queried an unregistered component");
                desc.terms[i].id = t->second.id;
                desc.terms[i].inout = int16_t(terms[i].write ? EcsInOut : EcsIn);
            }
            auto q = std::make_unique<Query>();
            q->value = ecs_query_init(self.world.c_ptr(), &desc);
            if (!q->value)
                throw std::runtime_error("Cannot create native query");
            found = self.queries.emplace(std::move(key), std::move(q)).first;
        }
        auto &q = *found->second;
        if (q.active)
            throw std::runtime_error("Recursive iteration of the same query is unsupported");
        q.active = true;
        auto it = ecs_query_iter(self.world.c_ptr(), q.value);
        try {
            while (ecs_query_next(&it)) {
                void *columns[8]{};
                uint32_t strides[8]{};
                for (uint32_t i = 0; i < count; ++i) {
                    strides[i] = self.types.at(terms[i].type).size;
                    columns[i] = ecs_field_w_size(&it, strides[i], int8_t(i));
                }
                sdk::Batch batch{uint32_t(it.count), it.entities, columns, strides};
                callback(user, &batch);
            }
            q.active = false;
        } catch (...) {
            ecs_iter_fini(&it);
            q.active = false;
            throw;
        }
    }
    explicit Impl(const CompiledScene &s, std::shared_ptr<GameRegistry> r, const RuntimeCheckpoint* saved=nullptr)
        : registry(std::move(r)), records(s.entities), authoredCount(uint32_t(s.entities.size())),
          imageCount(s.imageBank.images.size()) {
        children.resize(records.size());
        for (uint32_t i = 0; i < records.size(); ++i)
            if (records[i].parent != NoIndex)
                children[records[i].parent].push_back(i);
        auto registerType = [&](sdk::TypeId key, uint32_t size, uint32_t alignment) {
            ecs_component_desc_t d{};
            d.entity = world.entity().id();
            d.type.size = int32_t(size);
            d.type.alignment = int32_t(alignment);
            auto id = ecs_component_init(world.c_ptr(), &d);
            if (!id)
                throw std::runtime_error("Cannot register native component");
            types.emplace(key, Type{id, size});
        };
        for (const auto &c : registry->components())
            registerType(c.id, c.descriptor.size, c.descriptor.alignment);
        registerType(sdk::id(sdk::WorldPosition::gm_name), sizeof(sdk::WorldPosition),
                     alignof(sdk::WorldPosition));
        registerType(sdk::id(EntityIndex::gm_name), sizeof(EntityIndex), alignof(EntityIndex));
        for (uint32_t i = 0; i < records.size(); ++i) {
            auto e = ecs_new(world.c_ptr());
            handles.push_back(e);
            slots[e] = i;
            ids[records[i].id] = i;
            internal(i);
            for (const auto &c : s.entities[i].components)
                assign(e, c.type, c.fields);
        }
        reorder();
        if (saved) {
            frame.tick = saved->tick;
            runtimeSequence = saved->runtimeSequence;
        }
        frame.width = s.width;
        frame.height = s.height;
        transforms();
        try {
            auto c = context();
            for (const auto &sys : registry->systems()) {
                const auto &d = sys.descriptor;
                if (std::find(s.disabledSystems.begin(), s.disabledSystems.end(), d.name) !=
                    s.disabledSystems.end())
                    continue;
                void *state = nullptr;
                if (saved && registry->stateHooks(d.name)) {
                    auto hook = registry->stateHooks(d.name);
                    auto found = std::find_if(saved->systems.begin(), saved->systems.end(),
                                              [&](const SavedSystem &data) { return data.name == d.name; });
                    if (found == saved->systems.end())
                        throw std::runtime_error(std::string("Missing saved System state: ") + d.name);
                    sdk::Error error;
                    state = hook->restore(&c, found->version, found->bytes.data(),
                                          uint32_t(found->bytes.size()), &error);
                    error.message[511] = 0;
                    if (!state)
                        throw std::runtime_error(std::string(d.name) + ": restore failed: " + error.message);
                } else {
                    if (saved && d.create)
                        throw std::runtime_error(std::string(d.name) +
                                                 ": stateful System has no checkpoint extension");
                    state = d.create ? d.create(&c) : nullptr;
                }
                systems.push_back({d, state, {d.name}});
                commit();
            }
            transforms();
        } catch (...) {
            shutdown();
            throw;
        }
    }
    void shutdown() {
        active = nullptr;
        for (auto it = systems.rbegin(); it != systems.rend(); ++it)
            if (it->descriptor.destroy)
                it->descriptor.destroy(it->state);
        systems.clear();
        queries.clear();
    }
    ~Impl() {
        shutdown();
    }
    void transforms() {
        for (auto i : order) {
            auto e = handles[i];
            auto *local = static_cast<const Transform2D *>(raw(*this, e, sdk::id(Transform2D::gm_name)));
            auto *pos = const_cast<sdk::WorldPosition *>(
                static_cast<const sdk::WorldPosition *>(raw(*this, e, sdk::id(sdk::WorldPosition::gm_name))));
            if (!pos)
                continue;
            *pos = {local ? local->x : 0, local ? local->y : 0};
            if (auto *pose = static_cast<const sdk::Pose2D *>(raw(*this, e, sdk::id(sdk::Pose2D::gm_name)))) {
                float angle = pose->rotation * .01745329252f;
                float c = std::cos(angle), s = std::sin(angle);
                pos->a = c * pose->scaleX;
                pos->b = s * pose->scaleX;
                pos->c = -s * pose->scaleY;
                pos->d = c * pose->scaleY;
            }
            auto p = records[i].parent;
            if (p != NoIndex && handles[p]) {
                auto *parent = static_cast<const sdk::WorldPosition *>(
                    raw(*this, handles[p], sdk::id(sdk::WorldPosition::gm_name)));
                auto v = *pos;
                pos->x = parent->x + parent->a * v.x + parent->c * v.y;
                pos->y = parent->y + parent->b * v.x + parent->d * v.y;
                pos->a = parent->a * v.a + parent->c * v.b;
                pos->b = parent->b * v.a + parent->d * v.b;
                pos->c = parent->a * v.c + parent->c * v.d;
                pos->d = parent->b * v.c + parent->d * v.d;
            }
        }
    }
    static Rect box(const sdk::WorldPosition &p, float x, float y, float width, float height) {
        float cx = p.x + p.a * (x + width * .5f) + p.c * (y + height * .5f),
              cy = p.y + p.b * (x + width * .5f) + p.d * (y + height * .5f);
        float w = std::abs(p.a) * width + std::abs(p.c) * height,
              h = std::abs(p.b) * width + std::abs(p.d) * height;
        return {cx - w * .5f, cy - h * .5f, w, h};
    }
    void collectBodies() {
        bodies.clear();
        context().each<const sdk::WorldPosition, const sdk::Collider2D>(
            [&](sdk::Entity e, const auto &p, const auto &c) {
                if (c.enabled)
                    bodies.push_back({e, box(p, c.offsetX, c.offsetY, c.width, c.height), uint32_t(c.layer),
                                      uint32_t(c.mask)});
            });
        std::sort(bodies.begin(), bodies.end(),
                  [](const Body &a, const Body &b) { return a.box.x < b.box.x; });
    }
    void collisions() {
        collectBodies();
        std::set<std::pair<sdk::Entity, sdk::Entity>> next;
        auto notify = [&](auto pair, sdk::TypeId type) {
            sdk::Event event{type, pair.first, pair.second, 0, {}};
            emit(this, &event);
            std::swap(event.source, event.target);
            emit(this, &event);
        };
        for (size_t i = 0; i < bodies.size(); ++i) {
            const auto &a = bodies[i];
            for (size_t j = i + 1; j < bodies.size() && bodies[j].box.x < a.box.x + a.box.w; ++j) {
                const auto &b = bodies[j];
                if (!(a.mask & b.layer) || !(b.mask & a.layer) || a.box.y >= b.box.y + b.box.h ||
                    b.box.y >= a.box.y + a.box.h)
                    continue;
                auto pair = std::minmax(a.entity, b.entity);
                if (next.size() >= 30000)
                    throw std::runtime_error("Collision pair budget exceeded");
                next.insert(pair);
                notify(pair, contacts.count(pair) ? sdk::CollisionStay : sdk::CollisionEnter);
            }
        }
        for (auto pair : contacts)
            if (!next.count(pair))
                notify(pair, sdk::CollisionExit);
        contacts = std::move(next);
    }
    void phase(sdk::Phase phase) {
        auto c = context();
        for (auto &s : systems)
            if (s.descriptor.phase == phase) {
                auto start = Clock::now();
                active = &s.descriptor;
                try {
                    s.descriptor.update(s.state, &c);
                    active = nullptr;
                    commit();
                } catch (const std::exception &error) {
                    active = nullptr;
                    throw std::runtime_error(std::string(s.descriptor.name) + " at tick " +
                                             std::to_string(frame.tick) + ": " + error.what());
                }
                s.timing.milliseconds = ms(start);
                s.timing.maximum = std::max(s.timing.maximum, s.timing.milliseconds);
                ++s.timing.calls;
            }
    }
    bool bound(const SceneBinding &b) const {
        return b.entity < handles.size() && raw(const_cast<Impl &>(*this), handles[b.entity], b.component);
    }
    double number(uint32_t entity, sdk::TypeId type, uint32_t field) {
        const auto &f = registry->component(type).descriptor.fields[field];
        auto *data = static_cast<const unsigned char *>(raw(*this, handles.at(entity), type));
        if (!data)
            throw std::runtime_error("Runtime component missing");
        uint32_t word;
        std::memcpy(&word, data + f.offset, 4);
        return f.kind == sdk::Kind::Float ? double(std::bit_cast<float>(word))
                                          : double(std::bit_cast<int32_t>(word));
    }
};
SceneRuntime::SceneRuntime(CompiledScene scene) : registry_(gameRegistry()), asset_(std::move(scene)) {
    validateSceneBinary(asset_);
    reset();
}
SceneRuntime::~SceneRuntime() = default;
void SceneRuntime::reset() {
    auto next = std::make_unique<Impl>(asset_, registry_);
    auto hud = std::make_unique<RuntimeDocument>(asset_.hud);
    impl_ = std::move(next);
    hud_ = std::move(hud);
    clock_.reset();
    pendingInput_ = {};
    sprites_.clear();
    spriteRows_.clear();
    bound_.assign(asset_.bindings.size(), BoundValue{});
    extract();
    syncUi();
}
void SceneRuntime::restore(CompiledScene scene) {
    validateSceneBinary(scene);
    auto next = std::make_unique<Impl>(scene, registry_);
    auto hud = std::make_unique<RuntimeDocument>(scene.hud);
    asset_ = std::move(scene);
    spriteRevision_ = 0;
    spriteRows_.clear();
    sprites_.clear();
    impl_ = std::move(next);
    hud_ = std::move(hud);
    clock_.reset();
    pendingInput_ = {};
    bound_.assign(asset_.bindings.size(), BoundValue{});
    extract();
    syncUi();
}
uint32_t SceneRuntime::advance(double elapsed, SceneInput input) {
    auto edges = pendingInput_.pointer;
    pendingInput_ = input;
    auto &p = pendingInput_.pointer;
    p.pressed |= edges.pressed; p.released |= edges.released; p.wheel += edges.wheel;
    for (int i = 0; i < 8; ++i) p.keyPress[i] |= edges.keyPress[i];
    return clock_.advance(elapsed, [&](float dt) {
        tick(dt, pendingInput_);
        p.pressed = p.released = 0; p.wheel = 0;
        for (auto &key : p.keyPress) key = 0;
    });
}
const std::vector<sdk::CanvasCommand> &SceneRuntime::canvas() const { return impl_->canvas; }
void SceneRuntime::tick(float dt, SceneInput input) {
    if (!std::isfinite(dt) || dt < 0 || !std::isfinite(input.x) || !std::isfinite(input.y) ||
        !std::isfinite(input.pointer.mouseX) || !std::isfinite(input.pointer.mouseY) || !std::isfinite(input.pointer.wheel))
        throw std::runtime_error("Invalid simulation input");
    auto start = Clock::now();
    dt = std::min(dt, .1f);
    float n = std::hypot(input.x, input.y);
    if (n > 1) {
        input.x /= n;
        input.y /= n;
    }
    impl_->inbox.swap(impl_->outbox);
    impl_->outbox.clear();
    impl_->frame.dt = dt;
    impl_->frame.inputX = input.x;
    impl_->frame.inputY = input.y;
    impl_->frame.input = input.pointer;
    impl_->canvas.clear();
    ++impl_->frame.tick;
    impl_->phase(sdk::Phase::Input);
    impl_->phase(sdk::Phase::Simulation);
    impl_->transforms();
    impl_->phase(sdk::Phase::PostTransform);
    impl_->phase(sdk::Phase::Late);
    impl_->transforms();
    impl_->collisions();
    extract();
    syncUi();
    simulationMs_ = ms(start);
}
void SceneRuntime::extract() {
    if (spriteRevision_ != impl_->structureRevision || spriteRows_.size() != impl_->records.size()) {
        sprites_.clear();
        spriteRows_.assign(impl_->records.size(), NoIndex);
        spriteRevision_ = impl_->structureRevision;
    }
    bool reorder = false;
    impl_->context().each<const Impl::EntityIndex, const sdk::WorldPosition, const SpriteRenderer>(
        [&](sdk::Entity e, const Impl::EntityIndex &index, const sdk::WorldPosition &p,
            const SpriteRenderer &s) {
            auto &row = spriteRows_[index.value];
            if (row == NoIndex) {
                row = uint32_t(sprites_.size());
                sprites_.push_back({});
                reorder = true;
            }
            auto &out = sprites_[row];
            reorder |= out.order != s.order;
            out = {index.value, {p.x, p.y, s.width, s.height}, s.image, s.tint, s.order, p, {}};
            if (auto *region = static_cast<const sdk::SpriteRegion *>(
                    Impl::raw(*impl_, e, sdk::id(sdk::SpriteRegion::gm_name))))
                out.region = *region;
        });
    if (reorder) {
        std::sort(sprites_.begin(), sprites_.end(), [](const auto &a, const auto &b) {
            return a.order == b.order ? a.entity < b.entity : a.order < b.order;
        });
        for (uint32_t i = 0; i < sprites_.size(); ++i)
            spriteRows_[sprites_[i].entity] = i;
    }
}
void SceneRuntime::syncUi() {
    for (size_t i = 0; i < asset_.bindings.size(); ++i) {
        auto &b = asset_.bindings[i];
        if (b.kind == BindingKind::NumberChange)
            continue;
        if (!impl_->bound(b)) {
            if (b.kind == BindingKind::NumberText)
                hud_->setBoundText(b.node, "");
            else
                hud_->setBoundWidth(b.node, 0);
            bound_[i] = {};
            continue;
        }
        auto value = impl_->number(b.entity, b.component, b.field),
             maximum = b.maximum == NoIndex ? 0 : impl_->number(b.entity, b.component, b.maximum);
        auto &cached = bound_[i];
        if (value == cached.value && maximum == cached.maximum)
            continue;
        if (b.kind == BindingKind::NumberText) {
            if (std::ceil(value) != std::ceil(cached.value) ||
                std::ceil(maximum) != std::ceil(cached.maximum))
                hud_->setBoundText(b.node, std::to_string(int64_t(std::ceil(value))) +
                                               (b.maximum == NoIndex
                                                    ? std::string()
                                                    : " / " + std::to_string(int64_t(std::ceil(maximum)))));
        } else
            hud_->setBoundWidth(b.node, asset_.hud.nodes[b.node].bounds.w *
                                            float(maximum > 0 ? std::clamp(value / maximum, 0., 1.) : 0));
        cached = {value, maximum};
    }
}
bool SceneRuntime::activateUi(uint32_t node) {
    for (const auto &b : asset_.bindings)
        if (b.node == node && b.kind == BindingKind::NumberChange && impl_->bound(b)) {
            const auto &d = registry_->component(b.component).descriptor;
            const auto &f = d.fields[b.field];
            double hi = f.maximum;
            if (b.maximum != NoIndex)
                hi = std::min(hi, impl_->number(b.entity, b.component, b.maximum));
            hi = std::max(hi, f.minimum);
            auto value = std::clamp(impl_->number(b.entity, b.component, b.field) + b.amount, f.minimum, hi);
            auto words = component(b.entity, b.component);
            words[b.field] = f.kind == sdk::Kind::Float ? std::bit_cast<uint32_t>(float(value))
                                                        : std::bit_cast<uint32_t>(int32_t(value));
            set(entity(b.entity), b.component, words);
            return true;
        }
    return false;
}
Transform2D SceneRuntime::position(uint32_t index) const {
    auto *p = static_cast<const sdk::WorldPosition *>(
        Impl::raw(*impl_, entity(index), sdk::id(sdk::WorldPosition::gm_name)));
    return p ? Transform2D{p->x, p->y} : Transform2D{};
}
std::vector<uint32_t> SceneRuntime::component(uint32_t index, sdk::TypeId type) const {
    auto *data = Impl::raw(*impl_, entity(index), type);
    if (!data)
        throw std::runtime_error("Runtime component not found");
    return registry_->encode(type, data);
}
uint32_t SceneRuntime::find(const std::string &id) const {
    auto it = impl_->ids.find(id);
    return it == impl_->ids.end() ? NoIndex : it->second;
}
uint64_t SceneRuntime::ticks() const {
    return impl_->frame.tick;
}
std::string SceneRuntime::takeSceneRequest() {
    return std::exchange(impl_->requestedScene, {});
}
bool SceneRuntime::alive(uint32_t index) const {
    return index < impl_->handles.size() && Impl::alive(impl_.get(), impl_->handles[index]);
}
sdk::Entity SceneRuntime::entity(uint32_t index) const {
    return alive(index) ? impl_->handles[index] : 0;
}
const std::vector<SceneEntity> &SceneRuntime::entities() const {
    return impl_->records;
}
const std::string& SceneRuntime::entityId(sdk::Entity entity) const {
    auto found=impl_->slots.find(entity);
    if(found==impl_->slots.end())throw std::runtime_error("Runtime entity not found");
    return impl_->records[found->second].id;
}
sdk::Entity SceneRuntime::create(sdk::Entity parent) {
    auto e = Impl::create(impl_.get(), parent);
    impl_->commit();
    impl_->transforms();
    extract();
    return e;
}
void SceneRuntime::destroy(sdk::Entity e) {
    Impl::destroy(impl_.get(), e);
    impl_->commit();
    impl_->transforms();
    extract();
    syncUi();
}
void SceneRuntime::set(sdk::Entity e, sdk::TypeId type, std::span<const uint32_t> fields) {
    if (!Impl::alive(impl_.get(), e))
        throw std::runtime_error("Entity is not alive");
    impl_->validate(type, fields);
    impl_->queue({Impl::Op::Set, e, type, {fields.begin(), fields.end()}});
    impl_->commit();
    impl_->transforms();
    extract();
    syncUi();
}
void SceneRuntime::remove(sdk::Entity e, sdk::TypeId type) {
    Impl::remove(impl_.get(), e, type);
    impl_->commit();
    impl_->transforms();
    extract();
    syncUi();
}
sdk::Entity SceneRuntime::instantiate(uint32_t source, sdk::Entity parent) {
    if (source >= asset_.entities.size())
        throw std::runtime_error("Prefab source is not an authored entity");
    std::vector<std::vector<uint32_t>> children(asset_.entities.size());
    for (uint32_t i = 0; i < asset_.entities.size(); ++i)
        if (asset_.entities[i].parent != NoIndex)
            children[asset_.entities[i].parent].push_back(i);
    auto root = Impl::create(impl_.get(), parent);
    try {
        std::function<void(uint32_t, sdk::Entity)> copy = [&](uint32_t index, sdk::Entity target) {
            for (const auto &value : asset_.entities[index].components)
                impl_->queue({Impl::Op::Set, target, value.type, value.fields});
            for (auto child : children[index])
                copy(child, Impl::create(impl_.get(), target));
        };
        copy(source, root);
        impl_->commit();
        impl_->transforms();
        extract();
        syncUi();
    } catch (...) {
        impl_->commands.clear();
        destroy(root);
        throw;
    }
    return root;
}
RuntimeStats SceneRuntime::stats() const {
    RuntimeStats s;
    s.entities = uint32_t(impl_->slots.size());
    s.queries = uint32_t(impl_->queries.size());
    s.pendingCommands = uint32_t(impl_->commands.size());
    s.simulationMs = simulationMs_;
    s.droppedSeconds = clock_.droppedSeconds();
    for (uint32_t i = 0; i < impl_->records.size(); ++i)
        if (alive(i))
            for (const auto &c : impl_->records[i].components)
                s.componentBytes += registry_->component(c.type).descriptor.size;
    for (const auto &sys : impl_->systems)
        s.systems.push_back(sys.timing);
    return s;
}
SceneCamera SceneRuntime::camera() const {
    SceneCamera result{};
    int priority = INT32_MIN;
    impl_->context().each<const sdk::WorldPosition, const sdk::Camera2D>(
        [&](sdk::Entity, const auto &p, const auto &c) {
            if (c.enabled && c.priority > priority) {
                priority = c.priority;
                result = {p, c.zoom};
            }
        });
    return result;
}
std::vector<sdk::Entity> SceneRuntime::queryArea(Rect area, uint32_t mask) const {
    if (!std::isfinite(area.x) || !std::isfinite(area.y) || !std::isfinite(area.w) ||
        !std::isfinite(area.h) || area.w < 0 || area.h < 0)
        throw std::runtime_error("Invalid query rectangle");
    impl_->collectBodies();
    std::vector<sdk::Entity> hits;
    for (const auto &b : impl_->bodies) {
        if (b.box.x > area.x + area.w)
            break;
        if ((b.layer & mask) && b.box.x + b.box.w >= area.x && b.box.y + b.box.h >= area.y &&
            b.box.y <= area.y + area.h)
            hits.push_back(b.entity);
    }
    return hits;
}
RayHit SceneRuntime::raycast(float x, float y, float dx, float dy, float length, uint32_t mask) const {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(dx) || !std::isfinite(dy) ||
        !std::isfinite(length) || length < 0)
        throw std::runtime_error("Invalid ray");
    float norm = std::hypot(dx, dy);
    if (norm < 1e-7f)
        throw std::runtime_error("Ray direction is zero");
    dx /= norm;
    dy /= norm;
    impl_->collectBodies();
    RayHit hit{};
    hit.distance = length;
    for (const auto &b : impl_->bodies) {
        if (!(b.layer & mask))
            continue;
        float lo = 0, hi = hit.distance;
        auto slab = [&](float origin, float direction, float minimum, float maximum) {
            if (std::abs(direction) < 1e-7f)
                return origin >= minimum && origin <= maximum;
            float a = (minimum - origin) / direction, c = (maximum - origin) / direction;
            if (a > c)
                std::swap(a, c);
            lo = std::max(lo, a);
            hi = std::min(hi, c);
            return lo <= hi;
        };
        if (slab(x, dx, b.box.x, b.box.x + b.box.w) && slab(y, dy, b.box.y, b.box.y + b.box.h))
            hit = {b.entity, lo, x + dx * lo, y + dy * lo};
    }
    return hit;
}
CompiledScene SceneRuntime::snapshot() const {
    CompiledScene next = asset_;
    next.entities.clear();
    next.bindings.clear();
    std::vector<uint32_t> mapping(impl_->records.size(), NoIndex);
    for (uint32_t i = 0; i < impl_->records.size(); ++i)
        if (alive(i)) {
            mapping[i] = uint32_t(next.entities.size());
            auto record = impl_->records[i];
            for (auto &c : record.components)
                c.fields = component(i, c.type);
            next.entities.push_back(std::move(record));
        }
    for (auto &e : next.entities)
        if (e.parent != NoIndex)
            e.parent = mapping[e.parent];
    for (auto b : asset_.bindings)
        if (impl_->bound(b)) {
            b.entity = mapping[b.entity];
            next.bindings.push_back(b);
        }
    validateSceneBinary(next);
    return next;
}

std::vector<uint8_t> SceneRuntime::checkpoint() const {
    RuntimeCheckpoint saved;
    saved.scene = snapshot();
    saved.tick = ticks();
    saved.runtimeSequence = impl_->runtimeSequence;
    saved.remainder = clock_.remainder();
    saved.dropped = clock_.droppedSeconds();
    for (const auto &system : impl_->systems) {
        auto *hook = registry_->stateHooks(system.descriptor.name);
        if (!hook) {
            if (system.descriptor.create)
                throw std::runtime_error(std::string(system.descriptor.name) +
                                         ": register SystemState callbacks before saving private state");
            continue;
        }
        SavedSystem blob;
        blob.name = hook->system;
        blob.version = hook->version;
        sdk::ByteSink sink{&blob.bytes, [](void *ptr, const void *data, uint32_t size) {
                               auto &bytes = *static_cast<std::vector<uint8_t> *>(ptr);
                               if (size > 64 * 1024 * 1024 - bytes.size() || (!data && size))
                                   return false;
                               try {
                                   auto *first = static_cast<const uint8_t *>(data);
                                   if (size)
                                       bytes.insert(bytes.end(), first, first + size);
                                   return true;
                               } catch (...) {
                                   return false;
                               }
                           }};
        sdk::Error error;
        if (!hook->save(system.state, &sink, &error)) {
            error.message[511] = 0;
            throw std::runtime_error(blob.name + ": save failed: " + error.message);
        }
        saved.systems.push_back(std::move(blob));
    }
    auto name = [&](sdk::Entity entity) {
        auto found = impl_->slots.find(entity);
        return found == impl_->slots.end() ? std::string{} : impl_->records[found->second].id;
    };
    for (const auto &event : impl_->outbox)
        saved.events.push_back({event, name(event.source), name(event.target)});
    for (auto [a, b] : impl_->contacts) {
        auto first = name(a), second = name(b);
        if (!first.empty() && !second.empty())
            saved.contacts.emplace_back(first, second);
    }
    return encodeCheckpoint(saved);
}
void SceneRuntime::restoreCheckpoint(std::span<const uint8_t> bytes) {
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "GMEC", 4) == 0) {
        for (const auto &system : impl_->systems)
            if (system.descriptor.create)
                throw std::runtime_error("Legacy ECS-only save cannot restore private System state");
        restore(decodeScene(bytes));
        return;
    }
    auto saved = decodeCheckpoint(bytes);
    FixedStep clock;
    clock.restore(saved.remainder, saved.dropped);
    for (const auto &state : saved.systems) {
        auto hook = registry_->stateHooks(state.name);
        if (!hook || std::find(saved.scene.disabledSystems.begin(), saved.scene.disabledSystems.end(),
                               state.name) != saved.scene.disabledSystems.end())
            throw std::runtime_error("Checkpoint contains an unknown or disabled System: " + state.name);
    }
    auto next = std::make_unique<Impl>(saved.scene, registry_, &saved);
    auto hud = std::make_unique<RuntimeDocument>(saved.scene.hud);
    auto resolve = [&](const std::string &name) -> sdk::Entity {
        if (name.empty())
            return 0;
        auto found = next->ids.find(name);
        if (found == next->ids.end())
            throw std::runtime_error("Checkpoint event/contact entity not found");
        return next->handles[found->second];
    };
    for (auto event : saved.events) {
        event.event.source = resolve(event.source);
        event.event.target = resolve(event.target);
        next->outbox.push_back(event.event);
    }
    for (const auto &[a, b] : saved.contacts) {
        auto first = resolve(a), second = resolve(b);
        if (!first || !second)
            throw std::runtime_error("Invalid checkpoint contact");
        next->contacts.insert(std::minmax(first, second));
    }
    asset_ = std::move(saved.scene);
    impl_ = std::move(next);
    hud_ = std::move(hud);
    clock_ = clock;
    pendingInput_ = {};
    spriteRevision_ = 0;
    spriteRows_.clear();
    sprites_.clear();
    bound_.assign(asset_.bindings.size(), BoundValue{});
    extract();
    syncUi();
}
std::filesystem::path SceneRuntime::saveSlot(const std::string &slot) {
    auto bytes = checkpoint();
    if (!saveStorage_)
        saveStorage_ = std::make_unique<StorageSession>(registry_->name());
    saveStorage_->write(slot + ".gmsave", bytes);
    return saveStorage_->path(slot + ".gmsave");
}
std::filesystem::path SceneRuntime::loadSlot(const std::string &slot) {
    if (!saveStorage_)
        saveStorage_ = std::make_unique<StorageSession>(registry_->name());
    auto bytes = saveStorage_->read(slot + ".gmsave");
    restoreCheckpoint(bytes);
    return saveStorage_->path(slot + ".gmsave");
}
} // namespace gm
