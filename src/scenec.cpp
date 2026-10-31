#include "project.hpp"
#include "render_compiler.hpp"
#include <iostream>
namespace {
std::filesystem::path utfpath(const std::string &s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
} // namespace
int runCompiler(int argc, char **argv) {
    try {
        if (argc < 3 || (argc - 3) % 2) {
            std::cerr << "gm_scenec SOURCE.scene.json|SOURCE.gmscene|GAME.gmproject OUTPUT.gmscene [--game "
                         "GAME.gmproject] [--target windows|android]\n";
            return 2;
        }
        auto input = utfpath(argv[1]), source = input;
        std::filesystem::path project;
        std::string target;
        for (int i = 3; i < argc; i += 2) {
            if (std::string(argv[i]) == "--game")
                project = utfpath(argv[i + 1]);
            else if (std::string(argv[i]) == "--target")
                target = argv[i + 1];
            else
                throw std::runtime_error("Unknown compiler option");
        }
        if ((input.extension() == ".gmscene" || input.extension() == ".gmprefab")) {
            if (!project.empty()) {
                auto manifest = project;
                gm::activateProject(manifest, {});
            }
        } else
            gm::activateProject(source, project);
        auto asset = (input.extension() == ".gmscene" || input.extension() == ".gmprefab")
                         ? gm::loadScene(input)
                         : gm::compileScene(gm::readJson(source), source.parent_path());
        if (!target.empty())
            gm::targetRenderAssets(asset.rendering, target);
        gm::saveScene(utfpath(argv[2]), asset);
        std::cout << asset.entities.size() << " entities; module " << asset.module << "; binary v3\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t **wide) {
    std::vector<std::string> text;
    std::vector<char *> args;
    for (int i = 0; i < argc; ++i) {
        auto utf = std::filesystem::path(wide[i]).u8string();
        text.emplace_back(utf.begin(), utf.end());
    }
    for (auto &a : text)
        args.push_back(a.data());
    return runCompiler(argc, args.data());
}
#else
int main(int argc, char **argv) {
    return runCompiler(argc, argv);
}
#endif
