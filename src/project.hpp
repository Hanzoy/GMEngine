#pragma once
#include "scene_compiler.hpp"
namespace gm {
struct GameProject {std::filesystem::path manifest,root,entry,module;std::string name;bool valid()const{return !manifest.empty();}};
std::filesystem::path engineRoot();
GameProject readProject(const std::filesystem::path& path);
GameProject activateProject(std::filesystem::path& resource,const std::filesystem::path& explicitProject={});
std::shared_ptr<GameRegistry> loadProjectModule(const GameProject& project);
Json projectDescription(const GameProject& project);
void buildProjectModule(const GameProject& project);
void compileProject(const GameProject& project);
Json packageProject(const GameProject& project);
Json createProject(const std::filesystem::path& directory,const std::string& name);
Json runtimeState(SceneRuntime& runtime,bool paused,uint64_t revision=0,bool dirty=false);
void addProjectCommands(Json& result);
}
