#pragma once
#include "document.hpp"
#include "game_registry.hpp"
#include <span>
namespace gm {
struct ComponentField {std::string key,label;Json initial;double minimum=0,maximum=0;sdk::Kind kind=sdk::Kind::Float;};
struct ComponentDefinition {uint64_t id;std::string name,label;std::vector<ComponentField> fields;std::vector<std::string> dependencies;uint32_t version=1;};
const std::vector<ComponentDefinition>& sceneComponents();
const ComponentDefinition& sceneComponent(const std::string& type);
bool isScene(const Json& data);
Json* findEntity(Json& data,const std::string& id);
const Json* findEntity(const Json& data,const std::string& id);
Json* findComponent(Json& entity,const std::string& type);
const Json* findComponent(const Json& entity,const std::string& type);
void validateScene(const Json& data);
Json sceneSchema();
Json systemSchema(const Json& scene);
std::vector<uint32_t> componentWords(const std::string& type,const Json& fields);
Json componentJson(sdk::TypeId type,std::span<const uint32_t> fields);
Json editScene(Json& data,std::string& selected,const Json& request);
Json newScene();
std::string newEntityId();
}
