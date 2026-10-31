// Scene workspace methods, included inside Editor. UI and CLI share Document edits.
bool sceneMode() const {return gm::isScene(doc_.data());}
std::string rootId() const {return sceneMode()?std::string():doc_.data()["root"]["id"].get<std::string>();}
const Json* selectedNode() const {return sceneMode()?gm::findEntity(doc_.data(),doc_.selected()):gm::findNode(doc_.data()["root"],doc_.selected());}
bool playing() const {return scenePlay_!=nullptr||previewProcess_!=nullptr;}
gm::Transform2D sceneLocalDelta(float x,float y)const{std::vector<const Json*> parents;auto* e=gm::findEntity(doc_.data(),dragId_);while(e&&!e->at("parent").is_null()){e=gm::findEntity(doc_.data(),e->at("parent").get<std::string>());if(e)parents.push_back(e);}for(auto it=parents.rbegin();it!=parents.rend();++it)if(auto* pose=gm::findComponent(**it,"Pose2D")){auto& f=pose->at("fields");float angle=f["rotation"].get<float>()*.01745329252f,c=std::cos(angle),s=std::sin(angle),nextX=(c*x+s*y)/f["scaleX"].get<float>();y=(-s*x+c*y)/f["scaleY"].get<float>();x=nextX;}return {x,y};}
void setGameFocus(bool focused){if(focused==gameFocus_)return;gameFocus_=focused;if(!focused)gameInput_.clear();updateTextInput();}
void drawMainToolbar(Rect r) {
    renderer_->rect(r,T::panel);renderer_->rect({r.x,r.y+r.h-scale_,r.w,scale_},T::divider);
    icon({r.x+12*scale_,r.y+12*scale_,24*scale_,24*scale_},"folder-open",T::muted,20);
    text(r.x+44*scale_,r.y+15*scale_,gameProject_.valid()?gameProject_.name:sceneMode()?"场景工作区":"UI 工作区",13,T::text,230*scale_);
    const float x=r.x+r.w*.5f-87*scale_,y=r.y+7*scale_,h=34*scale_;
    renderer_->rect({x-3*scale_,y-2*scale_,180*scale_,h+4*scale_},T::field);
    renderer_->border({x-3*scale_,y-2*scale_,180*scale_,h+4*scale_},T::border);
    iconButton({x,y,42*scale_,h},"play",{{"op","preview.run"}},playing()?"正在运行 · F8 停止":"运行当前资源  F5",playing(),!playing()&&rootId()!="editor",T::green);
    iconButton({x+44*scale_,y,42*scale_,h},"pause",{{"op","preview.pause"}},scenePaused_?"继续运行  F7":"暂停运行  F7",scenePaused_,scenePlay_!=nullptr,scenePaused_?T::amber:0);
    iconButton({x+88*scale_,y,42*scale_,h},"step",{{"op","preview.step"}},"前进一帧  Shift+F7",false,scenePlay_&&scenePaused_);
    iconButton({x+132*scale_,y,42*scale_,h},"stop",{{"op","preview.stop"}},"停止运行，返回编辑状态  F8",false,playing(),T::red);
    const auto label=scenePlay_?(scenePaused_?"已暂停":"运行中"):previewProcess_?"Player 运行中":"编辑模式";
    const uint32_t stateColor=scenePaused_?T::amber:playing()?T::green:T::muted;
    icon({r.x+r.w-128*scale_,r.y+13*scale_,20*scale_,22*scale_},scenePaused_?"pause":"circle",stateColor,14);
    text(r.x+r.w-103*scale_,r.y+15*scale_,label,12,stateColor,94*scale_);
}
void runScenePreview() {
    auto compiled=gm::compileScene(doc_.data(),doc_.path().parent_path());const auto output=session_/"preview.gmscene";gm::saveScene(output,compiled);
    auto next=std::make_unique<gm::SceneRuntime>(gm::loadScene(output),gameProject_.valid()?gameProject_.root/"build":doc_.path().parent_path());stopPreview();scenePlay_=std::move(next);sceneView_=std::make_unique<gm::SceneView>(*renderer_);
    scenePaused_=false;sceneGameView_=true;gameFocus_=false;lastSceneTick_=std::chrono::steady_clock::now();
    message_="场景已运行 · 点击游戏视图后使用 WASD / 方向键";logs_.push_back("编译成功 · "+std::to_string(compiled.entities.size())+" 个实体 · 已启动独立运行实例");
}
Json sceneRuntimeState() {
    if(!scenePlay_)throw std::runtime_error("Scene is not running");return gm::runtimeState(*scenePlay_,scenePaused_,doc_.revision(),doc_.dirty());
}
void updateScenePlay() {
    auto now=std::chrono::steady_clock::now();float dt=std::chrono::duration<float>(now-lastSceneTick_).count();lastSceneTick_=now;
    if(!scenePlay_)return;auto requestedScene=scenePlay_->takeSceneRequest();if(!requestedScene.empty()){command({{"op","runtime.scene.switch"},{"scene",requestedScene}});dt=0;}if(!scenePaused_&&GetForegroundWindow()==window_&&!IsIconic(window_)){
        gm::SceneInput input;if(gameFocus_&&sceneGameView_&&!edit_){auto down=[](int k){return (GetAsyncKeyState(k)&0x8000)!=0;};input={float(down('D')||down(VK_RIGHT))-float(down('A')||down(VK_LEFT)),float(down('S')||down(VK_DOWN))-float(down('W')||down(VK_UP))};if(input.x==0)input.x=pendingSceneInput_.x;if(input.y==0)input.y=pendingSceneInput_.y;}input.pointer=gameInput_.sample(window_,canvasArea_,scenePlay_->asset().width,scenePlay_->asset().height,gameFocus_&&sceneGameView_&&!edit_);pendingSceneInput_={};try{scenePlay_->advance(dt,input);}catch(const std::exception& error){scenePaused_=true;error_=true;++errorCount_;message_=error.what();logs_.push_back(message_);}requestFrame();
    }
}
void sceneMenu(const std::string& name) {
    HMENU menu=CreatePopupMenu();std::vector<Json> actions;auto item=[&](const std::wstring& label,Json cmd,bool enabled=true){enabled=enabled&&(!projectBusy()||allowedDuringProjectTask(cmd.value("op",std::string())));actions.push_back(std::move(cmd));AppendMenuW(menu,MF_STRING|(enabled?0:MF_GRAYED),actions.size(),label.c_str());};auto sep=[&]{AppendMenuW(menu,MF_SEPARATOR,0,nullptr);};auto* selected=selectedNode();const bool editable=!scenePlay_;auto state=doc_.execute({{"op","query"}});
    if(name=="file"){item(L"新建场景...\tCtrl+N",{{"op","dialog.scene"}});item(L"新建 UI 资源...",{{"op","dialog.new"}});item(L"打开...\tCtrl+O",{{"op","dialog.open"}});sep();item(L"保存\tCtrl+S",{{"op","save"}});item(L"关闭标签\tCtrl+W",{{"op","tab.close"},{"interactive",true}},doc_.tabs().size()>1);item(L"从磁盘重新加载",{{"op","reload"}},editable&&!doc_.dirty());}
    else if(name=="edit"){item(L"撤销\tCtrl+Z",{{"op","undo"}},editable&&state["undo"].get<int>()>0);item(L"重做\tCtrl+Y",{{"op","redo"}},editable&&state["redo"].get<int>()>0);sep();item(L"复制实体\tCtrl+D",{{"op","duplicate"},{"id",doc_.selected()}},editable&&selected);item(L"删除实体\tDelete",{{"op","remove"},{"id",doc_.selected()}},editable&&selected);}
    else if(name=="node"||name=="create"){item(L"创建空实体",{{"op","entity.create"},{"name","Entity"}},editable);item(L"创建子实体",{{"op","entity.create"},{"name","Child"},{"parent",doc_.selected()}},editable&&selected);sep();item(L"复制实体\tCtrl+D",{{"op","duplicate"},{"id",doc_.selected()}},editable&&selected);item(L"删除实体\tDelete",{{"op","remove"},{"id",doc_.selected()}},editable&&selected);}
    else if(name=="component.add"){for(const auto& def:gm::sceneComponents())item(gm::wide(def.label),{{"op","component.add"},{"id",doc_.selected()},{"type",def.name}},editable&&selected&&!gm::findComponent(*selected,def.name));}
    else if(name.starts_with("component:")){if(!selected)throw std::runtime_error("Select an entity first");const auto type=name.substr(10);bool removable=editable&&selected;for(const auto& c:selected->at("components"))for(const auto& dep:gm::sceneComponent(c["type"].get<std::string>()).dependencies)if(dep==type)removable=false;if(doc_.data().contains("ui"))for(const auto& b:doc_.data()["ui"]["bindings"])if(b["entity"]==doc_.selected()&&b["component"]==type)removable=false;item(L"移除组件",{{"op","component.remove"},{"id",doc_.selected()},{"type",type}},removable);}
    else if(name=="scene.parent"){item(L"场景根级",{{"op","entity.reparent"},{"id",doc_.selected()},{"parent",nullptr}},editable);for(const auto& e:doc_.data()["entities"])if(e["id"]!=doc_.selected())item(gm::wide(e["name"].get<std::string>()),{{"op","entity.reparent"},{"id",doc_.selected()},{"parent",e["id"]}},editable);}
    else if(name=="tools"){item(L"运行场景\tF5",{{"op","preview.run"}},!playing());item(L"停止\tF8",{{"op","preview.stop"}},playing());sep();if(doc_.data().contains("ui"))item(L"编辑 HUD 界面",{{"op","file.open"},{"path",pathText(doc_.path().parent_path()/std::filesystem::path(gm::wide(doc_.data()["ui"]["source"].get<std::string>())))}});item(L"编辑编辑器界面...",{{"op","open-editor"}});}
    else if(name=="view"){item(L"聚焦所选\tF",{{"op","view.frame"}});item(L"查看全部",{{"op","view.fit"}});item(grid_?L"隐藏网格":L"显示网格",{{"op","view.grid"}});item(L"资源浏览器",{{"op","bottom.tab"},{"name","assets"}});item(L"控制台",{{"op","bottom.tab"},{"name","output"}});sep();item(L"重置布局",{{"op","layout.reset"}});}
    else if(name=="asset"){if(isImage(selectedFile_)){item(L"创建图片实体",{{"op","asset.add"}},editable);item(L"用于选中实体",{{"op","asset.apply"}},editable&&selected&&gm::findComponent(*selected,"SpriteRenderer"));}else item(L"打开资源",{{"op","file.open"},{"path",pathText(selectedFile_)}});}
    POINT point{LONG(mouseX_),LONG(mouseY_+12*scale_)};ClientToScreen(window_,&point);auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN,point.x,point.y,0,window_,nullptr);DestroyMenu(menu);if(choice>0&&choice<=actions.size())command(actions[choice-1]);
}
void drawSceneTree(Rect r) {
    titleBar(r,"层级");iconButton({r.x+r.w-34*scale_,r.y+2*scale_,28*scale_,26*scale_},"plus",{{"op","menu"},{"name","create"}},"创建实体",false,!scenePlay_);
    inputBox({r.x+10*scale_,r.y+38*scale_,r.w-20*scale_,26*scale_},"$tree",treeSearch_,"搜索实体",false);
    std::unordered_map<std::string,std::vector<const Json*>> children;for(const auto& e:doc_.data()["entities"])children[e["parent"].is_null()?"":e["parent"].get<std::string>()].push_back(&e);
    std::vector<std::pair<const Json*,int>> rows;std::function<void(const std::string&,int)> walk=[&](const std::string& parent,int depth){for(auto* e:children[parent]){auto id=e->at("id").get<std::string>();if(treeSearch_.empty()||matches(nameOf(*e),treeSearch_))rows.emplace_back(e,depth);if(!collapsed_.count(id)||!treeSearch_.empty())walk(id,depth+1);}};walk("",0);
    icon({r.x+10*scale_,r.y+74*scale_,18*scale_,20*scale_},"scene",T::accent,15);
    text(r.x+34*scale_,r.y+77*scale_,doc_.data().value("name",std::string("Scene"))+(doc_.dirty()?" *":""),12,T::text,r.w-44*scale_);
    Rect list{r.x,r.y+104*scale_,r.w,r.h-108*scale_};int visible=std::max(1,int(list.h/(28*scale_)));treeScroll_=std::clamp(treeScroll_,0,std::max(0,int(rows.size())-visible));renderer_->clip(list);
    for(int j=0;j<visible&&j+treeScroll_<int(rows.size());++j){auto [e,depth]=rows[j+treeScroll_];auto id=e->at("id").get<std::string>();Rect row{r.x,list.y+j*28*scale_,r.w,28*scale_};
        const bool selected=id==doc_.selected()&&!assetInspection_;if(selected){renderer_->rect(row,T::selection);renderer_->rect({row.x,row.y,2*scale_,row.h},T::accent);}else if(row.contains(mouseX_,mouseY_))renderer_->rect(row,T::hover);
        hits_.push_back({row,{{"op","select"},{"id",id}},nameOf(*e),true});float x=r.x+(8+depth*16)*scale_;
        if(!children[id].empty())iconButton({x,row.y,18*scale_,row.h},collapsed_.count(id)?"chevron-right":"chevron-down",{{"op","tree.toggle"},{"id",id}},"折叠 / 展开子级");
        const char *glyph=gm::findComponent(*e,"Camera3D")?"camera":gm::findComponent(*e,"DirectionalLight")?"sun":gm::findComponent(*e,"SpriteRenderer")?"image":"cube";
        icon({x+22*scale_,row.y,20*scale_,row.h},glyph,selected?T::accent:T::muted,16);
        text(x+48*scale_,row.y+6*scale_,nameOf(*e),13,T::text,r.w-(x-r.x)-56*scale_);
    }
    if(rows.empty())text(r.x+14*scale_,list.y+12*scale_,"使用 + 创建一个实体",12,muted(),r.w-24*scale_);scrollbar(list,int(rows.size()),visible,treeScroll_);renderer_->clip(r);
}
void drawSceneInspector(Rect r) {
    inspectorTabs(r);if(inspectorPage_=="systems"){drawSystemsInspector(r);return;}if(inspectorPage_=="project"){drawProjectInspector(r);return;}if(inspectorPage_=="profiler"){drawProfiler(r);return;}
    auto* selected=selectedNode();if(!selected){text(r.x+16*scale_,r.y+48*scale_,"选择实体以编辑组件",13,muted(),r.w-32*scale_);return;}Json entity=*selected;
    if(scenePlay_){auto i=scenePlay_->find(doc_.selected());if(i==gm::NoIndex){text(r.x+16*scale_,r.y+48*scale_,"此实体不在当前运行场景中。",13,muted(),r.w-32*scale_);return;}Json live=Json::array();for(const auto& c:scenePlay_->entities()[i].components){auto type=gm::gameRegistry()->component(c.type).descriptor.name;auto fields=gm::componentJson(c.type,scenePlay_->component(i,c.type));const auto& def=gm::sceneComponent(type);for(const auto& f:def.fields)if(gm::sdk::resourceKind(f.kind)){auto* original=gm::findComponent(entity,type);fields[f.key]=original?original->at("fields").at(f.key):Json("编译资源");}live.push_back({{"type",type},{"version",c.version},{"fields",fields}});}entity["components"]=std::move(live);}
    Rect body{r.x+12*scale_,r.y+42*scale_,r.w-24*scale_,r.h-50*scale_};
    float height=124;for(const auto& c:entity["components"]){const auto& d=gm::sceneComponent(c["type"].get<std::string>());height+=groups_.count(d.name)?40.f:50.f+30.f*(d.name=="Transform3D"?3.f:float(d.fields.size()));}height+=48;
    inspectorScroll_=std::clamp(inspectorScroll_,0.f,std::max(0.f,height-body.h/scale_));float y=body.y-inspectorScroll_*scale_;auto firstField=fields_.size(),firstHit=hits_.size();renderer_->clip(body);
    icon({body.x,y,32*scale_,32*scale_},"cube",T::accent,24);text(body.x+42*scale_,y+1*scale_,nameOf(entity),15,T::text,body.w-50*scale_);text(body.x+42*scale_,y+23*scale_,std::to_string(entity["components"].size())+" 个组件"+(scenePlay_?" · 运行时只读":""),11,T::muted,body.w-50*scale_);y+=50*scale_;
    property({body.x,y,body.w,25*scale_},entity,"name","名称",52);y+=33*scale_;
    text(body.x,y+5*scale_,"父级",12,T::muted);std::string parent="场景根级";if(!entity["parent"].is_null())if(auto* p=gm::findEntity(doc_.data(),entity["parent"].get<std::string>()))parent=nameOf(*p);
    Rect parentBox{body.x+52*scale_,y,body.w-52*scale_,25*scale_};renderer_->rect(parentBox,T::field);renderer_->border(parentBox,T::border);
    text(parentBox.x+8*scale_,y+5*scale_,parent,12,T::text,parentBox.w-32*scale_);icon({parentBox.x+parentBox.w-25*scale_,y,24*scale_,25*scale_},"chevron-down",T::muted,14);hits_.push_back({parentBox,{{"op","menu"},{"name","scene.parent"}},"更改父级，保持局部位置",!scenePlay_});y+=41*scale_;
    for(const auto& component:entity["components"]){auto type=component["type"].get<std::string>();const auto& def=gm::sceneComponent(type);Rect header{body.x,y,body.w,30*scale_};renderer_->rect(header,T::raised);
        icon({header.x+2*scale_,y,20*scale_,header.h},groups_.count(type)?"chevron-right":"chevron-down",T::muted,13);icon({header.x+22*scale_,y,22*scale_,header.h},componentIcon(type),T::muted,15);
        text(header.x+48*scale_,header.y+7*scale_,def.label,12,T::text,header.w-80*scale_);hits_.push_back({{header.x,header.y,header.w-28*scale_,header.h},{{"op","group.toggle"},{"name",type}},"折叠 / 展开组件",true});
        iconButton({header.x+header.w-28*scale_,y,28*scale_,header.h},"more",{{"op","menu"},{"name","component:"+type}},"组件操作",false,!scenePlay_);y+=40*scale_;
        if(groups_.count(type))continue;
        if(type=="Transform3D") {
            const char *labels[]={"位置","旋转","缩放"};const char *keys[][3]={{"x","y","z"},{"rotationX","rotationY","rotationZ"},{"scaleX","scaleY","scaleZ"}};
            const char *axes[]={"X","Y","Z"};const uint32_t colors[]={T::red,T::green,T::accent};
            float cell=(body.w-53*scale_)/3;
            for(int row=0;row<3;++row){text(body.x+4*scale_,y+5*scale_,labels[row],12,T::muted,44*scale_);for(int axis=0;axis<3;++axis){float x=body.x+52*scale_+cell*axis;text(x,y+5*scale_,axes[axis],11,colors[axis]);inputBox({x+14*scale_,y,cell-18*scale_,24*scale_},type+"."+keys[row][axis],displayValue(component["fields"][keys[row][axis]]));}y+=30*scale_;}
        } else for(const auto& f:def.fields){const auto key=type+"."+f.key;const bool resource=gm::sdk::resourceKind(f.kind);text(body.x+4*scale_,y+5*scale_,f.label,12,T::muted,83*scale_);
            if(f.kind==gm::sdk::Kind::Bool){bool checked=component["fields"][f.key].get<bool>();checkbox({body.x+92*scale_,y,28*scale_,24*scale_},checked,{{"op","component.set"},{"id",doc_.selected()},{"type",type},{"field",f.key},{"value",!checked}},"启用 / 停用",!scenePlay_);}
            else inputBox({body.x+92*scale_,y,body.w-99*scale_-(resource?29*scale_:0),24*scale_},key,displayValue(component["fields"][f.key]));
            if(resource)iconButton({body.x+body.w-29*scale_,y,26*scale_,24*scale_},"folder-open",{{"op","dialog.image"},{"type",type},{"field",f.key}},"选择资源（也可直接输入路径）",false,!scenePlay_);y+=30*scale_;
        }y+=10*scale_;
    }
    Rect add{body.x+8*scale_,y+4*scale_,body.w-16*scale_,32*scale_};renderer_->border(add,T::border);button(add,"添加组件",{{"op","menu"},{"name","component.add"}},"组件及其依赖会一起添加",false,!scenePlay_);icon({add.x+8*scale_,add.y,24*scale_,add.h},"plus",T::muted,16);
    for(size_t i=firstField;i<fields_.size();++i)fields_[i].rect=scenePlay_?Rect{}:gm::intersect(fields_[i].rect,body);for(size_t i=firstHit;i<hits_.size();++i)hits_[i].rect=gm::intersect(hits_[i].rect,body);scrollbar(body,int(std::ceil(height)),int(body.h/scale_),int(inspectorScroll_));renderer_->clip(r);
}

void drawSceneViewport(Rect r) {
    const bool has3D=scene3D();
    const float y=r.y+33*scale_,h=28*scale_;
    tabButton({r.x+6*scale_,y,52*scale_,h},"场景",{{"op","view.scene"},{"game",false}},!sceneGameView_);
    tabButton({r.x+60*scale_,y,52*scale_,h},"游戏",{{"op","view.scene"},{"game",true}},sceneGameView_,scenePlay_!=nullptr);
    if(!scenePlay_||!sceneGameView_){renderer_->rect({r.x+122*scale_,y+5*scale_,scale_,h-10*scale_},T::border);
        iconButton({r.x+132*scale_,y,28*scale_,h},"select",{{"op","view.tool"},{"tool","select"}},"选择对象",tool_=="select");
        iconButton({r.x+164*scale_,y,28*scale_,h},"hand",{{"op","view.tool"},{"tool","hand"}},"视图平移  Q",tool_=="hand");
        if(!has3D&&!scenePlay_){iconButton({r.x+196*scale_,y,28*scale_,h},"move",{{"op","view.tool"},{"tool","move"}},"移动对象  W",tool_=="move");iconButton({r.x+228*scale_,y,28*scale_,h},"grid",{{"op","view.grid"}},"显示 / 隐藏网格",grid_);}
        iconButton({r.x+r.w-38*scale_,y,28*scale_,h},"frame",{{"op","view.frame"}},"聚焦选中对象  F");
    } else text(r.x+136*scale_,y+7*scale_,scenePaused_?"已暂停 · 可逐帧检查":gameFocus_?"键盘输入已启用 · Esc 释放":"点击画面启用键盘输入",12,T::muted,r.w-144*scale_);
    canvasArea_={r.x+1,r.y+65*scale_,r.w-2,r.h-89*scale_};renderer_->clip(canvasArea_);renderer_->rect(canvasArea_,T::background);float w=doc_.data()["width"].get<float>(),hScene=doc_.data()["height"].get<float>();previewScale_=std::max(.01f,std::min((canvasArea_.w-32*scale_)/w,(canvasArea_.h-32*scale_)/hScene))*zoom_;previewRect_={canvasArea_.x+(canvasArea_.w-w*previewScale_)*.5f+panX_,canvasArea_.y+(canvasArea_.h-hScene*previewScale_)*.5f+panY_,w*previewScale_,hScene*previewScale_};
    if(scenePlay_){if(has3D&&!sceneGameView_){auto camera=sceneCamera().view();sceneView_->render(*scenePlay_,canvasArea_,false,&camera);drawSceneSelection();}else sceneView_->render(*scenePlay_,canvasArea_,sceneGameView_);renderer_->clip(r);renderer_->border(canvasArea_,scenePaused_?C("#BFAC7C"):C("#659F8A"),2*scale_);}
    else if(has3D){
        ensureEdit3D();
        if(edit3D_){auto camera=sceneCamera().view();edit3DView_->render(*edit3D_,canvasArea_,false,&camera);drawSceneSelection();renderer_->clip(r);}

    }
    else {renderer_->rect(gm::intersect(previewRect_,canvasArea_),gm::color(doc_.data()["background"].get<std::string>()));
        if(grid_){float step=64*previewScale_;while(step<16*scale_)step*=2;for(float x=previewRect_.x;x<previewRect_.x+previewRect_.w;x+=step)renderer_->rect({x,previewRect_.y,1,previewRect_.h},C("#273340"));for(float sy=previewRect_.y;sy<previewRect_.y+previewRect_.h;sy+=step)renderer_->rect({previewRect_.x,sy,previewRect_.w,1},C("#273340"));}
        const auto& entities=doc_.data()["entities"];std::unordered_map<std::string,size_t> ids;for(size_t i=0;i<entities.size();++i)ids[entities[i]["id"].get<std::string>()]=i;std::vector<gm::sdk::WorldPosition> poses(entities.size());std::vector<bool> ready(entities.size());std::function<void(size_t)> pose=[&](size_t i){if(ready[i])return;auto& p=poses[i];const auto& e=entities[i];if(auto* c=gm::findComponent(e,"Transform2D")){p={c->at("fields")["x"].get<float>(),c->at("fields")["y"].get<float>()};if(dragMode_=="scene-node"&&e["id"]==dragId_){p.x+=dx_;p.y+=dy_;}}if(auto* c=gm::findComponent(e,"Pose2D")){auto& f=c->at("fields");float radians=f["rotation"].get<float>()*.01745329252f;p.a=std::cos(radians)*f["scaleX"].get<float>();p.b=std::sin(radians)*f["scaleX"].get<float>();p.c=-std::sin(radians)*f["scaleY"].get<float>();p.d=std::cos(radians)*f["scaleY"].get<float>();}if(!e["parent"].is_null()){auto index=ids.at(e["parent"].get<std::string>());pose(index);auto v=p;const auto& q=poses[index];p={q.x+q.a*v.x+q.c*v.y,q.y+q.b*v.x+q.d*v.y,q.a*v.a+q.c*v.b,q.b*v.a+q.d*v.b,q.a*v.c+q.c*v.d,q.b*v.c+q.d*v.d};}ready[i]=true;};
        std::vector<size_t> order;for(size_t i=0;i<entities.size();++i){pose(i);order.push_back(i);}auto layer=[&](size_t i){auto* c=gm::findComponent(entities[i],"SpriteRenderer");return c?c->at("fields")["order"].get<float>():-2000.f;};std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return layer(a)<layer(b);});
        for(auto i:order){const auto& e=entities[i];const auto* sprite=gm::findComponent(e,"SpriteRenderer");const auto& p=poses[i];Rect box{previewRect_.x+p.x*previewScale_,previewRect_.y+p.y*previewScale_,12*scale_,12*scale_};if(sprite){const auto& f=sprite->at("fields");float width=f["width"].get<float>(),height=f["height"].get<float>();std::array<float,8> points{};float minX=1e30f,minY=1e30f,maxX=-1e30f,maxY=-1e30f;for(int j=0;j<4;++j){float u=(j==1||j==2)?width:0,v=j>=2?height:0;float sx=previewRect_.x+(p.x+p.a*u+p.c*v)*previewScale_,sy=previewRect_.y+(p.y+p.b*u+p.d*v)*previewScale_;points[j*2]=sx;points[j*2+1]=sy;minX=std::min(minX,sx);minY=std::min(minY,sy);maxX=std::max(maxX,sx);maxY=std::max(maxY,sy);}box={minX,minY,maxX-minX,maxY-minY};Rect uv{0,0,1,1};if(auto* c=gm::findComponent(e,"SpriteRegion")){auto& region=c->at("fields");uv={region["u0"].get<float>(),region["v0"].get<float>(),region["u1"].get<float>()-region["u0"].get<float>(),region["v1"].get<float>()-region["v0"].get<float>()};}if(f["image"]!="")images_->sprite({{"source",f["image"]},{"tint",f["tint"]}},points,uv,doc_.path().parent_path());else renderer_->sprite(points,BGFX_INVALID_HANDLE,gm::color(f["tint"].get<std::string>()),uv);}else renderer_->border(box,C("#73879D"));nodes_.push_back({box,e["id"].get<std::string>()});if(e["id"]==doc_.selected()&&!assetInspection_){renderer_->border(box,C("#95C8ED"),2*scale_);text(box.x,box.y-20*scale_,nameOf(e),11,C("#C3DDF0"),160*scale_);}}
        renderer_->border(previewRect_,C("#607080"));renderer_->clip(r);
    }
    renderer_->rect({r.x,r.y+r.h-23*scale_,r.w,23*scale_},T::panel);text(r.x+12*scale_,r.y+r.h-18*scale_,has3D&&!sceneGameView_?"右键 + WASD/QE 飞行 · Shift 加速 · 中键平移 · Alt+左键环绕 · 滚轮缩放 · F 聚焦":std::to_string(entitiesCount())+" 个实体   ·   "+std::to_string(int(w))+" × "+std::to_string(int(hScene)),11,muted(),r.w-24*scale_);
}
size_t entitiesCount()const{return doc_.data()["entities"].size();}
void sceneImageDialog(const std::string& type="SpriteRenderer",const std::string& field="image"){const auto* e=selectedNode();if(!e||!gm::findComponent(*e,type))return;wchar_t buffer[4096]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window_;dialog.lpstrFilter=L"图片 (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0\0";if(type=="MeshRenderer")dialog.lpstrFilter=field=="mesh"?L"网格 (*.obj)\0*.obj\0\0":L"材质 (*.material.json)\0*.material.json\0\0";dialog.lpstrFile=buffer;dialog.nMaxFile=4096;auto initial=(contentRoot()/"assets/images").wstring();dialog.lpstrInitialDir=initial.c_str();dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;if(GetOpenFileNameW(&dialog)){std::error_code ec;auto relative=std::filesystem::relative(buffer,doc_.path().parent_path(),ec);command({{"op","component.set"},{"id",doc_.selected()},{"type",type},{"field",field},{"value",pathText(ec?std::filesystem::path(buffer):relative)}});images_->clear();}}
