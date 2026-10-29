#include "project.hpp"
#include <iostream>
namespace {std::filesystem::path utfpath(const std::string& s){return std::filesystem::path(std::u8string(s.begin(),s.end()));}}
int runCompiler(int argc,char** argv){try{if(argc!=3&&argc!=5){std::cerr<<"gm_scenec SOURCE.scene.json|GAME.gmproject OUTPUT.gmscene [--game GAME.gmproject]\n";return 2;}auto source=utfpath(argv[1]);std::filesystem::path project;if(argc==5){if(std::string(argv[3])!="--game")throw std::runtime_error("Expected --game");project=utfpath(argv[4]);}gm::activateProject(source,project);auto asset=gm::compileScene(gm::readJson(source),source.parent_path());gm::saveScene(utfpath(argv[2]),asset);std::cout<<asset.entities.size()<<" entities; module "<<asset.module<<"; binary v2\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#ifdef _WIN32
int wmain(int argc,wchar_t** wide){std::vector<std::string> text;std::vector<char*> args;for(int i=0;i<argc;++i){auto utf=std::filesystem::path(wide[i]).u8string();text.emplace_back(utf.begin(),utf.end());}for(auto& a:text)args.push_back(a.data());return runCompiler(argc,args.data());}
#else
int main(int argc,char** argv){return runCompiler(argc,argv);}
#endif
