#include "editor_icons.hpp"
#include <iostream>
#include <set>
#include <stdexcept>

int main() {
    try {
        std::set<std::string_view> names;
        std::set<std::vector<uint8_t>> masks;
        for (auto name : gm::editorIconNames()) {
            if (!names.insert(name).second)
                throw std::runtime_error("Duplicate icon name");
            for (int pixels : {18, 27, 36, 48}) {
                auto icon = gm::rasterEditorIcon(name, pixels);
                if (icon.width != pixels || icon.height != pixels ||
                    icon.rgba.size() != size_t(pixels * pixels * 4))
                    throw std::runtime_error("Icon has incorrect DPI dimensions");
                unsigned coverage = 0, fractional = 0;
                std::vector<uint8_t> mask;
                for (size_t p = 0; p < icon.rgba.size(); p += 4) {
                    auto alpha = icon.rgba[p + 3];
                    coverage += alpha;
                    fractional += alpha > 0 && alpha < 255;
                    mask.push_back(alpha);
                    if (alpha && (icon.rgba[p] != 255 || icon.rgba[p + 1] != 255 || icon.rgba[p + 2] != 255))
                        throw std::runtime_error("Icons must support neutral tinting");
                }
                if (coverage < pixels * pixels || coverage > pixels * pixels * 230 || !fractional)
                    throw std::runtime_error("Empty, opaque or un-antialiased icon: " + std::string(name));
                if (pixels == 36 && !masks.insert(mask).second)
                    throw std::runtime_error("Different icon names produced identical shapes");
            }
        }
        for (auto required : {"play", "pause", "step", "stop", "select", "hand", "frame", "search"})
            if (!names.contains(required))
                throw std::runtime_error("Required toolbar icon is missing");
        bool badName = false, badSize = false;
        try {
            gm::rasterEditorIcon("not-an-icon", 18);
        } catch (const std::runtime_error &) {
            badName = true;
        }
        try {
            gm::rasterEditorIcon("play", 0);
        } catch (const std::runtime_error &) {
            badSize = true;
        }
        if (!badName || !badSize)
            throw std::runtime_error("Invalid icon request was accepted");
        std::cout << "PASS: " << names.size()
                  << " distinct SVGs, four DPI sizes, transparency, antialiasing and invalid requests\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
