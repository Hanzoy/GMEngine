#include "render_compiler.hpp"
#include <set>
namespace gm {
Json normalizedMaterial(const Json &source) {
    if (!source.is_object() || source.value("format", std::string{}) != "gm.material" ||
        source.value("version", 0) != 1)
        throw std::runtime_error("Expected gm.material version 1");
    Json out = source;
    for (auto [key, value] :
         std::initializer_list<std::pair<const char *, Json>>{{"shader", "lit"},
                                                              {"blend", "opaque"},
                                                              {"cull", "back"},
                                                              {"depthTest", true},
                                                              {"depthWrite", true},
                                                              {"parameters", Json::object()},
                                                              {"textures", Json::object()}})
        if (!out.contains(key))
            out[key] = value;
    if (!out["shader"].is_string() || out["shader"].get<std::string>().size() > 2048 ||
        !out["depthTest"].is_boolean() || !out["depthWrite"].is_boolean())
        throw std::runtime_error("Invalid material shader/depth state");
    if (out["blend"] != "opaque" && out["blend"] != "alpha" && out["blend"] != "additive")
        throw std::runtime_error("Material blend must be opaque, alpha or additive");
    if (out["cull"] != "back" && out["cull"] != "front" && out["cull"] != "none")
        throw std::runtime_error("Material cull must be back, front or none");
    if (!out["parameters"].is_object() || out["parameters"].size() > 32 || !out["textures"].is_object() ||
        out["textures"].size() > 8)
        throw std::runtime_error("Material parameter/texture limits exceeded");
    CookedMaterial m;
    RenderAssets a;
    a.textures.push_back({1, 1, 1, true, true, true, {{TextureFormat::Rgba8, {255, 255, 255, 255}}}});
    for (auto &[name, value] : out["parameters"].items()) {
        if (!value.is_array() || value.size() != 4)
            throw std::runtime_error("Material parameters must be vec4 arrays");
        MaterialParameter p;
        p.name = name;
        for (int i = 0; i < 4; ++i) {
            if (!value[i].is_number())
                throw std::runtime_error("Material parameter must be numeric");
            p.value[i] = value[i].get<float>();
        }
        m.parameters.push_back(p);
    }
    for (auto &[name, value] : out["textures"].items()) {
        if (!value.is_object() || !value.at("source").is_string() ||
            value["source"].get<std::string>().empty() || value["source"].get<std::string>().size() > 2048 ||
            !value.at("stage").is_number_integer() || value.at("stage").get<int64_t>() < 0 ||
            value.at("stage").get<int64_t>() >= 8)
            throw std::runtime_error("Material texture needs source and unsigned stage");
        m.samplers.push_back({name, 0, value["stage"].get<uint32_t>()});
    }
    if (out["shader"] == "lit" || out["shader"] == "unlit") {
        const std::set<std::string> builtin = {"u_baseColor", "u_surface", "u_emission", "u_unlit",
                                               "u_uvTransform", "u_planar", "u_specular"};
        for (auto &p : m.parameters)
            if (!builtin.count(p.name))
                throw std::runtime_error("Unknown builtin material parameter: " + p.name);
        for (auto &t : m.samplers)
            if (!((t.name == "s_baseColor" && t.stage == 0) || (t.name == "s_normal" && t.stage == 1)))
                throw std::runtime_error(
                    "Builtin material samplers are s_baseColor stage 0 and s_normal stage 1");
    }
    a.materials.push_back(m);
    validateRenderAssets(a);
    return out;
}
} // namespace gm
