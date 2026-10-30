#include "project.hpp"
#include "scene_catalog.hpp"
#include <thread>
#include <cctype>
namespace gm {
Json hubCommand(const Json &request) {
    auto op = request.at("op").get<std::string>();
    auto path = [&](const char *key) {
        auto s = request.at(key).get<std::string>();
        return std::filesystem::path(std::u8string(s.begin(), s.end()));
    };
    if (op == "project.recent")
        return {{"ok", true}, {"result", recentProjects()}};
    if (op == "project.forget") {
        forgetProject(path("path"));
        return {{"ok", true}};
    }
    if (op == "project.add") {
        auto project = readProject(path("path"));
        rememberProject(project);
        return {{"ok", true}, {"result", recentProjects()}};
    }
    if (op == "project.create")
        return {{"ok", true},
                {"result", createProject(path("directory"), request.at("name").get<std::string>())}};
    if (op == "describe")
        return {{"ok", true},
                {"result",
                 {{"project.recent", "List recent projects"},
                  {"project.add", "path: register an existing .gmproject"},
                  {"project.forget", "path: forget without deleting"},
                  {"project.create", "directory, name: create from the installed template"}}}};
    throw std::runtime_error("Unknown project launcher command");
}
Json runtimeCommand(SceneRuntime &run, const Json &request, const GameProject &project) {
    auto op = request.at("op").get<std::string>();
    if (op == "runtime.input") {
        SceneInput input; input.x=request.value("x",0.f);input.y=request.value("y",0.f);
        auto &p=input.pointer;p.mouseX=request.value("mouseX",-1.f);p.mouseY=request.value("mouseY",-1.f);
        p.wheel=request.value("wheel",0.f);p.buttons=request.value("buttons",0u);
        p.pressed=request.value("pressed",0u);p.released=request.value("released",0u);p.focused=1;
        for(const char* field:{"keys","keyPress"}) if(request.contains(field)) for(auto key:request.at(field)) {
            auto code=key.get<int>();if(code<0||code>255)throw std::runtime_error("Key code must be 0..255");
            (std::string(field)=="keys"?p.keys:p.keyPress)[code/32] |= 1u<<(code%32);
        }
        auto frames=request.value("frames",1);if(frames<1||frames>10000)throw std::runtime_error("frames must be 1..10000");
        for(int i=0;i<frames;++i) {run.tick(1.f/60,input);p.pressed=p.released=0;p.wheel=0;for(auto& key:p.keyPress)key=0;}
        return {{"ok",true},{"result",{{"ticks",run.ticks()},{"canvasCommands",run.canvas().size()}}}};
    }
    if (op == "runtime.canvas") {
        Json commands=Json::array();for(const auto& c:run.canvas())commands.push_back({{"kind",uint32_t(c.kind)},{"x",c.x},{"y",c.y},{"w",c.w},{"h",c.h},{"size",c.size},{"color",c.color},{"text",c.text}});
        return {{"ok",true},{"result",commands}};
    }
    auto entity = [&](const char *key) {
        auto index = run.find(request.at(key).get<std::string>());
        if (index == NoIndex)
            throw std::runtime_error("Runtime entity not found");
        return run.entity(index);
    };
    if (op == "runtime.stats") {
        auto stats = run.stats();
        Json systems = Json::array();
        for (const auto &s : stats.systems)
            systems.push_back({{"name", s.name},
                               {"milliseconds", s.milliseconds},
                               {"maximum", s.maximum},
                               {"calls", s.calls}});
        return {{"ok", true},
                {"result",
                 {{"entities", stats.entities},
                  {"queries", stats.queries},
                  {"componentBytes", stats.componentBytes},
                  {"pendingCommands", stats.pendingCommands},
                  {"simulationMs", stats.simulationMs},
                  {"droppedSeconds", stats.droppedSeconds},
                  {"systems", systems}}}};
    }
    if (op == "runtime.entity.create" || op == "runtime.prefab.instantiate") {
        sdk::Entity parent = request.contains("parent") ? entity("parent") : 0;
        auto handle = op == "runtime.entity.create"
                          ? run.create(parent)
                          : run.instantiate(run.find(request.at("source").get<std::string>()), parent);
        return {{"ok", true}, {"result", {{"id", "runtime-" + std::to_string(handle)}}}};
    }
    if (op == "runtime.entity.destroy") {
        run.destroy(entity("id"));
        return {{"ok", true}};
    }
    if (op == "runtime.component.set") {
        auto target = entity("id");
        auto name = request.at("type").get<std::string>();
        const auto &d = gameRegistry()->component(name);
        auto input = request.at("fields");
        std::vector<std::pair<uint32_t, uint32_t>> images;
        for (uint32_t i = 0; i < d.descriptor.fieldCount; ++i)
            if (d.descriptor.fields[i].kind == sdk::Kind::Image) {
                auto key = d.descriptor.fields[i].name;
                const auto &value = input.at(key);
                if (value.is_number_unsigned() || (value.is_number_integer() && value.get<int64_t>() >= 0)) {
                    auto image = value.get<uint64_t>();
                    if (image > UINT32_MAX)
                        throw std::runtime_error("Image index exceeds uint32");
                    images.push_back({i, uint32_t(image)});
                    input[key] = "";
                }
            }
        auto fields = componentWords(name, input);
        for (auto [field, image] : images)
            fields[field] = image;
        run.set(target, d.id, fields);
        return {{"ok", true}};
    }
    if (op == "runtime.component.remove") {
        run.remove(entity("id"), gameRegistry()->component(request.at("type").get<std::string>()).id);
        return {{"ok", true}};
    }
    if (op == "runtime.scene.switch") {
        if (!project.valid())
            throw std::runtime_error("Scene switching requires a built project catalog");
        SceneLoader loader(project.root / "build/scenes.gmcatalog");
        loader.request(request.at("scene").get<std::string>());
        while (loader.pending()) {
            if (auto scene = loader.poll()) {
                run.restore(std::move(*scene));
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return {{"ok", true}};
    }
    if (op == "runtime.query.area") {
        auto hits = run.queryArea({request.at("x").get<float>(), request.at("y").get<float>(),
                                   request.at("w").get<float>(), request.at("h").get<float>()},
                                  request.value("mask", 65535u));
        return {{"ok", true}, {"result", hits}};
    }
    if (op == "runtime.query.ray") {
        auto hit = run.raycast(request.at("x").get<float>(), request.at("y").get<float>(),
                               request.at("dx").get<float>(), request.at("dy").get<float>(),
                               request.at("length").get<float>(), request.value("mask", 65535u));
        return {{"ok", true},
                {"result", {{"entity", hit.entity}, {"distance", hit.distance}, {"x", hit.x}, {"y", hit.y}}}};
    }
    if (op == "runtime.save" || op == "runtime.load") {
        auto slot = request.value("slot", std::string("default"));
        if (slot.empty() || slot.size() > 64 || !std::all_of(slot.begin(), slot.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '_' || c == '-';
            }))
            throw std::runtime_error("Save slot must contain 1..64 letters, digits, underscores or hyphens");
        auto path = userDataRoot() / "saves" / gameRegistry()->name() / (slot + ".gmsave");
        if (op == "runtime.save")
            saveScene(path, run.snapshot());
        else
            run.restore(loadScene(path));
        auto u = path.u8string();
        return {{"ok", true}, {"result", {{"path", std::string(u.begin(), u.end())}}}};
    }
    throw std::runtime_error("Unknown runtime command: " + op);
}
} // namespace gm
