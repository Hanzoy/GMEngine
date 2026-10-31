std::filesystem::path contentRoot()const{
    if(gameProject_.valid())return gameProject_.root;
    for(auto folder=doc_.path().parent_path();!folder.empty();){if(folder.filename()=="assets")return folder.parent_path();auto parent=folder.parent_path();if(parent==folder)break;folder=parent;}
    return doc_.path().parent_path()==root_/"editor"?root_:doc_.path().parent_path();
}
bool allSaved()const{for(const auto& tab:doc_.tabs())if(tab->dirty())return false;return true;}
Json projectCommand(const Json& request){const auto op=request.at("op").get<std::string>();
    if(op=="project.query")return {{"ok",true},{"result",gm::projectDescription(gameProject_)}};
    if(op=="project.recent")return {{"ok",true},{"result",gm::recentProjects()}};
    if(op=="project.task.status")return {{"ok",true},{"result",projectTaskStatus()}};
    if(op=="project.task.cancel"){if(projectBusy()){projectTask_.cancel();cancellingProjectTask_=true;}return {{"ok",true},{"result",projectTaskStatus()}};}
    if(op=="project.build"||op=="project.package"||op=="project.assets")return beginProjectTask(request);
    if(op=="project.forget"){gm::forgetProject(std::filesystem::path(gm::wide(request.at("path").get<std::string>())));return {{"ok",true}};}
    if(op=="project.create")return {{"ok",true},{"result",gm::createProject(std::filesystem::path(gm::wide(request.at("directory").get<std::string>())),request.at("name").get<std::string>())}};
    if(op=="project.sources"){if(!gameProject_.valid())throw std::runtime_error("请先打开游戏项目。");auto path=gameProject_.root/"Scripts";if(request.value("open",false))ShellExecuteW(window_,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return {{"ok",true},{"result",{{"directory",pathText(path)}}}};}
    if(playing()||!allSaved())throw std::runtime_error("请先停止运行并保存所有资源，再切换、构建或重载项目。");
    if(op=="project.open"){auto path=std::filesystem::path(gm::wide(request.at("path").get<std::string>()));if(path.extension()!=".gmproject")throw std::runtime_error("请选择 .gmproject 文件。");auto previous=gm::gameRegistry();try{auto project=gm::activateProject(path);gm::EditorDocuments next(path);doc_=std::move(next);gameProject_=std::move(project);}catch(...){gm::useGameRegistry(previous);throw;}gm::rememberProject(gameProject_);selectedFolder_=gameProject_.entry.parent_path();inspectorPage_="components";resetView();refreshFiles();message_="已打开项目 "+gameProject_.name;return {{"ok",true},{"result",gm::projectDescription(gameProject_)}};}
    if(op=="project.reload"){
        if(!gameProject_.valid())throw std::runtime_error("请先打开游戏项目。");
        auto previous=gm::gameRegistry();auto next=gm::loadProjectModule(gameProject_);gm::useGameRegistry(next);try{for(const auto& tab:doc_.tabs())gm::validateDocument(tab->data());}catch(...){gm::useGameRegistry(previous);throw;}
        for(const auto& tab:doc_.tabs())tab->resetHistory();Json result=gm::projectDescription(gameProject_);
        message_="项目模块已重载";logs_.push_back(message_);images_->clear();refreshFiles();return {{"ok",true},{"result",result}};
    }
    throw std::runtime_error("Unknown project command");
}
void projectDialog(bool create){if(!commit())return;wchar_t buffer[4096]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window_;dialog.lpstrFile=buffer;dialog.nMaxFile=4096;dialog.lpstrFilter=create?L"新项目目录名称\0*.*\0\0":L"GM 游戏项目\0*.gmproject\0\0";dialog.lpstrTitle=create?L"创建项目：输入新项目目录的名称":L"打开游戏项目";auto initial=contentRoot().parent_path().wstring();dialog.lpstrInitialDir=initial.c_str();dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(create?0:OFN_FILEMUSTEXIST);if(!(create?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog)))return;auto path=std::filesystem::path(buffer);if(create){auto result=command({{"op","project.create"},{"directory",pathText(path)},{"name",pathText(path.filename())}});if(!result.value("ok",false))return;path/=L"game.gmproject";}command({{"op","project.open"},{"path",pathText(path)}});}
void projectMenu(){HMENU menu=CreatePopupMenu();std::vector<Json> commands;auto item=[&](const wchar_t* name,Json command,bool enabled=true){commands.push_back(std::move(command));AppendMenuW(menu,MF_STRING|(enabled?0:MF_GRAYED),commands.size(),name);};
    item(L"新建游戏项目...",{{"op","dialog.project.new"}},!projectBusy()&&!playing()&&allSaved());item(L"打开游戏项目...",{{"op","dialog.project.open"}},!projectBusy()&&!playing()&&allSaved());AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    item(L"项目信息",{{"op","view.inspector"},{"page","project"}});item(L"场景 System 执行列表",{{"op","view.inspector"},{"page","systems"}},sceneMode());item(L"打开原生脚本目录",{{"op","project.sources"},{"open",true}},gameProject_.valid());AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    bool ready=gameProject_.valid()&&!projectBusy()&&!playing()&&allSaved();item(L"构建项目模块与场景",{{"op","project.build"}},ready);item(L"重载已构建模块",{{"op","project.reload"}},ready);item(L"生成 Windows 运行包",{{"op","project.package"}},ready);
    item(L"取消当前任务",{{"op","project.task.cancel"}},projectBusy()&&!cancellingProjectTask_);
    POINT p{LONG(mouseX_),LONG(mouseY_+12*scale_)};ClientToScreen(window_,&p);auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN,p.x,p.y,0,window_,nullptr);DestroyMenu(menu);if(choice>0&&choice<=commands.size())command(commands[choice-1]);
}
void inspectorTabs(Rect r){renderer_->rect({r.x,r.y,r.w,32*scale_},T::header);float x=r.x+4*scale_,width=(r.w-8*scale_)/4;for(const auto& [name,label]:std::vector<std::pair<std::string,std::string>>{{"components","检查器"},{"systems","系统"},{"project","项目"},{"profiler","性能"}}){tabButton({x,r.y,width,32*scale_},label,{{"op","view.inspector"},{"page",name}},inspectorPage_==name,sceneMode()||name!="systems");x+=width;}}
void drawProfiler(Rect r) {
    Rect body{r.x+14*scale_,r.y+40*scale_,r.w-28*scale_,r.h-48*scale_};
    if(!scenePlay_){text(body.x,body.y,"运行场景后查看每步模拟与 System 耗时。",12,muted(),body.w);return;}
    auto stats=scenePlay_->stats();const auto& render=renderer_->stats();
    auto number=[](double v){char buffer[48];std::snprintf(buffer,sizeof(buffer),"%.3f ms",v);return std::string(buffer);};
    std::vector<std::string> lines{
        "模拟: "+number(stats.simulationMs),
        "P50: "+number(stats.p50Ms)+"   P95: "+number(stats.p95Ms),
        "采样: "+std::to_string(stats.sampledFrames)+" / 120 帧",
        "碰撞: "+number(stats.collisionMs),
        "提取: "+number(stats.extractionMs)+"   UI: "+number(stats.uiMs),
        "实体: "+std::to_string(stats.entities)+"   查询缓存: "+std::to_string(stats.queries),
        "组件有效数据: "+std::to_string(stats.componentBytes)+" B",
        "积压丢弃: "+std::to_string(stats.droppedSeconds)+" s"};
    auto time=[&](double v){return v<0?std::string("不可用"):number(v);};
    auto memory=[](uint64_t b){char s[48];std::snprintf(s,sizeof(s),"%.2f MiB",double(b)/1048576);return std::string(s);};
    lines.insert(lines.end(),{
        "渲染后端: "+render.backend,
        "渲染前端 CPU: "+time(render.frontendMs),
        "提交 / 等待: "+time(render.presentWaitMs),
        "渲染线程 CPU: "+time(render.renderCpuMs),
        "GPU: "+time(render.gpuMs)+"  帧 "+std::to_string(render.gpuFrame),
        "GPU 结果延迟返回；非当前模拟帧。",
        "绘制: "+std::to_string(render.drawCalls)+"  缓存: "+std::to_string(render.cachedDraws),
        "3D 网格 / 可见: "+std::to_string(render.totalMeshes)+" / "+std::to_string(render.visibleMeshes),
        "3D 提交 / 实例: "+std::to_string(render.meshDraws)+" / "+std::to_string(render.instances),
        "三角形: "+std::to_string(render.triangles)+"  后处理: "+std::to_string(render.postPasses),
        "材质纹理: "+memory(render.materialTextureBytes)+"  压缩: "+std::to_string(render.compressedTextures),
        "网格: "+memory(render.meshBytes)+"  渲染目标: "+memory(render.targetBytes),
        "精灵总数: "+std::to_string(render.totalSprites),
        "候选 / 可见: "+std::to_string(render.spriteCandidates)+" / "+std::to_string(render.visibleSprites),
        "图片: "+memory(render.imageTextureBytes)+"  纹理 "+std::to_string(render.imageTextures),
        "文字: "+memory(render.textTextureBytes),
        "几何缓存: "+memory(render.geometryBytes),
        "本帧顶点上传: "+std::to_string(render.vertexUploadBytes)+" B",
        "本帧纹理上传: "+std::to_string(render.uploadBytes)+" B"});
    float height=34+float(lines.size())*25+14+float(stats.systems.size())*78+28;
    inspectorScroll_=std::clamp(inspectorScroll_,0.f,std::max(0.f,height-body.h/scale_));
    float y=body.y-inspectorScroll_*scale_;renderer_->clip(body);
    text(body.x,y,"运行时性能",16,ink(),body.w);y+=34*scale_;
    for(const auto& line:lines){text(body.x,y,line,12,ink(),body.w);y+=25*scale_;}
    y+=14*scale_;
    for(const auto& system:stats.systems){
        text(body.x,y,system.name,12,ink(),body.w);y+=22*scale_;
        text(body.x,y,"当前 "+number(system.milliseconds)+"   峰值 "+number(system.maximum),11,muted(),body.w);y+=21*scale_;
        text(body.x,y,"平均 "+number(system.average)+"   次数 "+std::to_string(system.calls),11,muted(),body.w);y+=35*scale_;
    }
    text(body.x,y,"组件数据不包含 Flecs 分配器与纹理内存。",11,muted(),body.w);
    scrollbar(body,int(height),int(body.h/scale_),int(inspectorScroll_));renderer_->clip(r);
}
void drawProjectInspector(Rect r){float x=r.x+14*scale_,y=r.y+44*scale_,w=r.w-28*scale_;text(x,y,gameProject_.valid()?gameProject_.name:"尚未打开游戏项目",16,ink(),w);y+=36*scale_;for(const auto& [label,value]:std::vector<std::pair<std::string,std::string>>{{"项目目录",pathText(gameProject_.root)},{"入口场景",gameProject_.valid()?pathText(gameProject_.entry.lexically_relative(gameProject_.root)):""},{"原生模块",gm::gameRegistry()->name()},{"组件 / System",std::to_string(gm::sceneComponents().size())+" / "+std::to_string(gm::gameRegistry()->systems().size())}}){text(x,y,label,12,muted(),w);y+=21*scale_;text(x,y,value,12,ink(),w);y+=35*scale_;}
    bool ready=gameProject_.valid()&&!projectBusy()&&!playing()&&allSaved();button({x,y,w,30*scale_},"构建项目",{{"op","project.build"}},"构建 C++ 模块和已保存的入口场景",false,ready);y+=38*scale_;button({x,y,w,30*scale_},"生成运行包",{{"op","project.package"}},"输出到项目的 dist 目录",false,ready);y+=43*scale_;if(projectBusy()){text(x,y,projectTaskText(),12,ink(),w);y+=30*scale_;button({x,y,w,30*scale_},"取消当前任务",{{"op","project.task.cancel"}},"停止构建进程及其子进程",false,!cancellingProjectTask_);}else text(x,y,"修改脚本后，停止并保存，再构建项目。",12,muted(),w);}
void drawSystemsInspector(Rect r){Rect body{r.x+12*scale_,r.y+40*scale_,r.w-24*scale_,r.h-48*scale_};auto systems=gm::systemSchema(doc_.data());float height=58+float(systems.size())*94;inspectorScroll_=std::clamp(inspectorScroll_,0.f,std::max(0.f,height-body.h/scale_));float y=body.y-inspectorScroll_*scale_;auto start=hits_.size();renderer_->clip(body);text(body.x,y,"执行顺序 · 当前场景",14,ink(),body.w);y+=25*scale_;text(body.x,y,"开关写入场景，支持撤销。",11,muted(),body.w);y+=32*scale_;
    for(const auto& s:systems){renderer_->rect({body.x,y,body.w,82*scale_},T::raised);bool enabled=s["enabled"].get<bool>();checkbox({body.x+6*scale_,y+7*scale_,27*scale_,25*scale_},enabled,{{"op","system.set"},{"name",s["name"]},{"enabled",!enabled}},"启用 / 停用",!scenePlay_);text(body.x+40*scale_,y+10*scale_,s["label"].get<std::string>(),13,ink(),body.w-45*scale_);text(body.x+9*scale_,y+38*scale_,s["name"].get<std::string>(),11,muted(),body.w-18*scale_);text(body.x+9*scale_,y+59*scale_,s["phase"].get<std::string>()+" · "+(s["owner"]=="engine"?"引擎":"项目"),11,C("#93B6D4"),body.w-18*scale_);y+=94*scale_;}
    for(size_t i=start;i<hits_.size();++i)hits_[i].rect=gm::intersect(hits_[i].rect,body);scrollbar(body,int(height),int(body.h/scale_),int(inspectorScroll_));renderer_->clip(r);
}
