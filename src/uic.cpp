#include "ui_compiler.hpp"
#include <iostream>

int compile(const std::filesystem::path& input,const std::filesystem::path& output) {
    try {
        if(output.extension()!=".gmui" || std::filesystem::absolute(input).lexically_normal()==std::filesystem::absolute(output).lexically_normal()) throw std::runtime_error("Output must be a separate .gmui file");
        auto asset=gm::compileUi(gm::readJson(input),std::filesystem::absolute(input).parent_path());gm::saveUi(output,asset);
        std::cout<<"Compiled "<<asset.nodes.size()<<" nodes, "<<asset.actions.size()<<" actions, "<<asset.images.size()<<" images, "<<std::filesystem::file_size(output)<<" bytes\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"UI compile failed: "<<error.what()<<'\n';return 1;}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    if(argc!=3) {std::cerr<<"Usage: gm_uic INPUT.ui.json OUTPUT.gmui\n";return 2;}
    return compile(argv[1],argv[2]);
}
