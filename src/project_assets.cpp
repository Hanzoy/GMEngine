#include "project.hpp"
#include "scene_catalog.hpp"
#include "render_compiler.hpp"
#include <fstream>
#include <set>
#include <sstream>
namespace gm {
namespace {
std::string text(const std::filesystem::path &p) {
    auto s = p.generic_u8string();
    return {s.begin(), s.end()};
}
std::string hashFile(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot read asset: " + text(path));
    uint64_t hash = 14695981039346656037ull;
    char block[65536];
    while (in) {
        in.read(block, sizeof(block));
        for (std::streamsize i = 0; i < in.gcount(); ++i) {
            hash ^= uint8_t(block[i]);
            hash *= 1099511628211ull;
        }
    }
    if (!in.eof())
        throw std::runtime_error("Asset read failed");
    return std::to_string(hash);
}
std::string stableId(const std::filesystem::path &path) {
    auto meta = path;
    meta += ".gmmeta";
    if (std::filesystem::exists(meta)) {
        auto data = readJson(meta);
        auto id = data.at("id").get<std::string>();
        if (id.size() != 36 ||
            !std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isxdigit(c) || c == '-'; }))
            throw std::runtime_error("Invalid asset ID: " + text(meta));
        return id;
    }
    auto id = newEntityId();
    writeJsonAtomic(meta, {{"format", "gm.asset"}, {"version", 1}, {"id", id}});
    return id;
}
void references(const Json &node, std::set<std::string> &out) {
    if (node.is_object()) {
        if (node.value("type", std::string()) == "image" && node.contains("source"))
            out.insert(node["source"].get<std::string>());
        if (node.contains("image") && node["image"].is_object() && node["image"].contains("source"))
            out.insert(node["image"]["source"].get<std::string>());
        for (const auto &v : node.items())
            references(v.value(), out);
    } else if (node.is_array())
        for (const auto &v : node)
            references(v, out);
}
} // namespace
Json importProjectAssets(const GameProject &p) {
    auto operation = lockProjectOperation(p);
    if (!p.valid())
        throw std::runtime_error("Open a game project first");
    Json assets = Json::array();
    std::set<std::string> ids;
    auto root = std::filesystem::canonical(p.root);
    if (!std::filesystem::exists(root / "assets"))
        return assets;
    for (const auto &item : std::filesystem::recursive_directory_iterator(root / "assets")) {
        if (!item.is_regular_file())
            continue;
        auto path = item.path();
        auto ext = text(path.extension());
        if (ext != ".json" && ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".wav" && ext != ".mp3" && ext != ".flac" && ext != ".obj" && ext != ".sc" && ext != ".sh" && ext != ".bin")
            continue;
        auto relative = std::filesystem::canonical(path).lexically_relative(root);
        if (relative.empty() || *relative.begin() == "..")
            throw std::runtime_error("Asset symlink escapes project root");
        auto id = stableId(path);
        if (!ids.insert(id).second)
            throw std::runtime_error("Duplicate asset ID; copied assets need a new .gmmeta ID");
        assets.push_back({{"id", id}, {"path", text(relative)}, {"hash", hashFile(path)}});
    }
    std::sort(assets.begin(), assets.end(),
              [](const auto &a, const auto &b) { return a.at("path") < b.at("path"); });
    writeJsonAtomic(root / ".gm/assets.json", {{"version", 1}, {"assets", assets}});
    return assets;
}
Json buildProjectAssets(const GameProject &p) {
    auto operation = lockProjectOperation(p);
    auto assets = importProjectAssets(p);
    Json previous = Json::object();
    auto cache = p.root / ".gm/import-cache.json";
    try {
        previous = readJson(cache);
    } catch (...) {
    }
    Json next = Json::object();
    Json report = Json::array();
    std::vector<SceneEntry> catalog;
    std::vector<SceneEntry> prefabs;
    for (auto &asset : assets) {
        auto relative = asset.at("path").get<std::string>();
        if(relative.ends_with(".wav")||relative.ends_with(".mp3")||relative.ends_with(".flac")){
            auto file=std::filesystem::path(std::u8string(relative.begin(),relative.end()));auto output=p.root/"build"/file;std::filesystem::create_directories(output.parent_path());
            if(!std::filesystem::exists(output)||hashFile(output)!=asset.at("hash").get<std::string>())std::filesystem::copy_file(p.root/file,output,std::filesystem::copy_options::overwrite_existing);
            continue;
        }
        bool prefab=relative.ends_with(".prefab.json");
        if (!relative.ends_with(".scene.json")&&!prefab)
            continue;
        auto path = std::filesystem::canonical(
            p.root / std::filesystem::path(std::u8string(relative.begin(), relative.end())));
        auto source = readJson(path);
        std::set<std::filesystem::path> dependencies{path};
        auto dependency = [&](const std::filesystem::path &base, const std::string &value) {
            auto full = std::filesystem::canonical(resolveResource(base, value));
            auto rel = full.lexically_relative(std::filesystem::canonical(p.root));
            if (rel.empty() || *rel.begin() == "..")
                throw std::runtime_error("Resource dependency escapes project root");
            dependencies.insert(full);
            return full;
        };
        for (const auto &e : source.at("entities"))
            for (const auto &component : e.at("components")) {
                auto &descriptor =
                    gameRegistry()->component(component.at("type").get<std::string>()).descriptor;
                for (uint32_t i = 0; i < descriptor.fieldCount; ++i)
                    if (descriptor.fields[i].kind == sdk::Kind::Image) {
                        auto value = component.at("fields").at(descriptor.fields[i].name).get<std::string>();
                        if (!value.empty())
                            dependency(path.parent_path(), value);
                    }
            }
        for(const auto& e:source["entities"])for(const auto& c:e["components"]){const auto& d=gameRegistry()->component(c["type"].get<std::string>()).descriptor;for(uint32_t i=0;i<d.fieldCount;++i)if(d.fields[i].kind==sdk::Kind::Mesh||d.fields[i].kind==sdk::Kind::Material||d.fields[i].kind==sdk::Kind::Texture)renderDependencies(path.parent_path(),c["fields"][d.fields[i].name].get<std::string>(),dependencies);}
        for(const auto& file:dependencies){auto dependencyRelative=file.lexically_relative(std::filesystem::canonical(p.root));if(dependencyRelative.empty()||*dependencyRelative.begin()=="..")throw std::runtime_error("Render resource dependency escapes project root");}
        if (source.contains("ui")) {
            auto uiPath = dependency(path.parent_path(), source["ui"]["source"].get<std::string>());
            std::set<std::string> images;
            references(readJson(uiPath), images);
            for (const auto &value : images)
                dependency(uiPath.parent_path(), value);
        }
        Json signature = {
            {"compiler", 16}, {"renderResourceBudget", RenderResourceBudget},
            {"schema", gameRegistry()->fingerprint()}, {"dependencies", Json::object()}};
        for (const auto &file : dependencies)
            signature["dependencies"][text(file.lexically_relative(p.root))] = hashFile(file);
        auto id = asset.at("id").get<std::string>();
        auto file = prefab?"prefabs/"+id+".gmprefab":"scenes/" + id + ".gmscene";
        auto output = p.root / "build" / file;
        bool exists = std::filesystem::exists(output);
        if (exists)
            signature["compiledHash"] = hashFile(output);
        bool reused = exists && previous.contains(id) && previous[id] == signature;
        if (!reused) {
            auto compiled=compileScene(source,path.parent_path());
            if(prefab&&(compiled.entities.empty()||compiled.entities[0].parent!=NoIndex||compiled.hud.nodes.size()!=1||!compiled.bindings.empty()||std::count_if(compiled.entities.begin(),compiled.entities.end(),[](const SceneEntity& e){return e.parent==NoIndex;})!=1))throw std::runtime_error("Prefab must have one root at index zero and no HUD");
            saveScene(output, compiled);
            signature["compiledHash"] = hashFile(output);
        }
        next[id] = signature;
        auto name = text(path.filename());
        name.resize(name.size() - (prefab?12:11));
        (prefab?prefabs:catalog).push_back({id, name, file});
        report.push_back({{"id", id},
                          {"path", relative},
                          {"file",file},{"kind",prefab?"prefab":"scene"},
                          {"reused", reused},
                          {"dependencies", signature["dependencies"]}});
        if (path == p.entry)
            std::filesystem::copy_file(output, p.root / "build/game.gmscene",
                                       std::filesystem::copy_options::overwrite_existing);
    }
    if (std::none_of(report.begin(), report.end(), [&](const Json &entry) {
            return entry.at("path") == text(p.entry.lexically_relative(p.root));
        }))
        throw std::runtime_error("Project entry must be under assets and end with .scene.json");
    saveCatalog(p.root / "build/scenes.gmcatalog", catalog);
    saveCatalog(p.root / "build/prefabs.gmcatalog", prefabs);
    writeJsonAtomic(cache, next);
    writeJsonAtomic(p.root / "build/import-report.json", report);
    return report;
}
} // namespace gm
