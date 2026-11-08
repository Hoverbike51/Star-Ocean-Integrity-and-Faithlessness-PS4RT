// Native Cockpit menu. Included inside the overlay's private namespace so all
// controls use the existing synchronized ImGui context and live settings atomics.
ImTextureData* menu_logo_texture = nullptr;
ImVec2 menu_logo_size{};

const std::wstring& MenuLanguage() {
    static const std::wstring language = [] {
        wchar_t text[64]{};
        const auto length=GetEnvironmentVariableW(L"SO5_UI_LANGUAGE",text,64);
        return length && length<64 ? std::wstring(text) : std::wstring(L"English");
    }();
    return language;
}

const char* Tr(const wchar_t* english, const wchar_t* french) {
    static std::unordered_map<std::wstring,std::string> translations;
    auto [it,inserted]=translations.try_emplace(english);
    if(inserted) {
        const wchar_t* text=MenuLanguage()==L"Français" ? french : ps4rt_localize(MenuLanguage(),english);
        const int size=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
        it->second.resize(size>0 ? size : 1);
        if(size>0) WideCharToMultiByte(CP_UTF8,0,text,-1,it->second.data(),size,nullptr,nullptr);
        it->second.pop_back();
    }
    return it->second.c_str();
}

void CockpitField(const char* label,std::atomic<int>& value,const char* const* choices,const int* values,int count) {
    ImGui::PushID(label);ImGui::TextWrapped("%s",label);
    ImGui::SetNextItemWidth(-FLT_MIN);Choice("##choice",value,choices,values,count);
    ImGui::Spacing();ImGui::PopID();
}

void CockpitSwitch(const char* label,std::atomic<bool>& value) {
    Row(label);
    const ImVec2 start=ImGui::GetCursorScreenPos(),size(54*base_scale,30*base_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,size.y*.5f);
    ImGui::PushStyleColor(ImGuiCol_Button,value?ImVec4(.10f,.48f,.60f,1):ImVec4(.13f,.22f,.28f,1));
    if(ImGui::Button("##switch",size)) Store(value,!value.load(),true);
    ImGui::PopStyleColor();ImGui::PopStyleVar();
    const float radius=size.y*.32f;
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(start.x+(value?size.x-size.y*.5f:size.y*.5f),start.y+size.y*.5f),radius,value?IM_COL32(94,221,250,255):IM_COL32(150,170,183,255));
    ImGui::PopID();
}

void CockpitHudPresets() {
    if(!Ps4RtHud::Active()) return;
    static std::unique_ptr<So5Hud::PresetLibrary> library;
    static std::string selection;
    if(!library) {
        const char* config=std::getenv("PS4RT_CONFIG");
        if(!config||!*config) return;
        const int output=Ps4RtUltrawide::session_output.load();
        const auto aspect=Ps4RtUltrawide::Enabled()?
            (Ps4RtOutput::Group(output)==Ps4RtOutput::Ratio32_9?So5Hud::Aspect::Superwide:So5Hud::Aspect::Wide):So5Hud::Aspect::Standard;
        library=std::make_unique<So5Hud::PresetLibrary>(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(config))).parent_path(),aspect);
        library->Refresh();
    }
    const auto snapshot=library->Snapshot();
    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
    ImGui::TextWrapped("%s",Tr(L"HUD layout",L"Disposition du HUD"));
    ImGui::BeginDisabled(snapshot.busy);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::BeginCombo("##hud-layout",selection.empty()?Tr(L"Select a HUD preset",L"Choisir un preset HUD"):selection.c_str())) {
        for(size_t i=0;i<snapshot.entries.size();++i) {
            const auto& entry=snapshot.entries[i];ImGui::PushID(int(i));
            ImGui::BeginDisabled(!entry.compatible);
            if(ImGui::Selectable(entry.name.c_str(),selection==entry.name)&&entry.compatible){selection=entry.name;library->Apply(i);}
            ImGui::EndDisabled();ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if(ImGui::Button(Tr(L"Refresh presets",L"Actualiser les presets"),ImVec2(-FLT_MIN,0))) library->Refresh();
    if(ImGui::Button(Tr(L"Open HUD presets folder",L"Ouvrir le dossier des presets HUD"),ImVec2(-FLT_MIN,0))) {
        const char* config=std::getenv("PS4RT_CONFIG");
        bool opened=false;
        if(config&&*config) {
            const auto folder=std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(config))).parent_path()/L"hud-presets";
            std::error_code error;std::filesystem::create_directories(folder,error);
            if(!error)opened=reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32;
        }
        if(!opened)Ps4RtOverlay::ShowNotice(Tr(L"Could not open the HUD presets folder.",L"Impossible d’ouvrir le dossier des presets HUD."));
    }
    ImGui::EndDisabled();
    const wchar_t* status=nullptr;
    switch(snapshot.status) {
    case So5Hud::LibraryStatus::Scanning:status=L"Scanning HUD presets…";break;
    case So5Hud::LibraryStatus::Applying:status=L"Applying HUD layout…";break;
    case So5Hud::LibraryStatus::Queued:status=L"HUD layout queued for the next frame.";break;
    case So5Hud::LibraryStatus::Failed:status=L"HUD layout could not be changed. See Logs and diagnostics.";break;
    default:break;
    }
    if(status) ImGui::TextWrapped("%s",Tr(status,ps4rt_localize(L"Français",status)));
    ImGui::TextWrapped("%s",Tr(L"Place exported JSON presets in the game’s hud-presets folder. Incompatible presets are disabled.",
        L"Placez les presets JSON exportés dans le dossier hud-presets du jeu. Les presets incompatibles sont désactivés."));
}

void CockpitDisplay() {
    auto& s=Ps4RtSettings::Get();
    const bool wide=ImGui::GetContentRegionAvail().x>700*base_scale;
    if(!ImGui::BeginTable("##display-live",wide?2:1,ImGuiTableFlags_SizingStretchSame)) return;
    ImGui::TableSetupColumn("controls",ImGuiTableColumnFlags_WidthStretch,1);
    if(wide) ImGui::TableSetupColumn("preview",ImGuiTableColumnFlags_WidthStretch,1);
    ImGui::TableNextColumn();
    const char* modes[]={Tr(L"Windowed",L"Fenêtré"),Tr(L"Borderless",L"Sans bordure"),Tr(L"Fullscreen",L"Plein écran")};
    CockpitField(Tr(L"Display mode",L"Mode d’affichage"),s.display_mode,modes,nullptr,3);
    const auto supported=Ps4RtUltrawide::SessionModes(s.display_mode==Ps4RtGraphics::Windowed?s.display_window_modes.load():s.display_fullscreen_modes.load());
    const char* aspects[Ps4RtOutput::AspectCount]{}; int aspect_values[Ps4RtOutput::AspectCount]{},aspect_count=0;
    for(int aspect=0;aspect<Ps4RtOutput::AspectCount;++aspect) if(Ps4RtOutput::Filter(supported,aspect)) {
        aspects[aspect_count]=aspect==0?Tr(L"All",L"Tous"):Ps4RtOutput::AspectNames[aspect];
        aspect_values[aspect_count++]=aspect;
    }
    if(aspect_count) CockpitField(Tr(L"Aspect ratio",L"Format d’image"),s.output_aspect,aspects,aspect_values,aspect_count);
    if(!Ps4RtOutput::Filter(supported,s.output_aspect)) s.output_aspect=Ps4RtOutput::All;
    const int selected=Ps4RtOutput::Select(s.output_res,supported,s.output_aspect);
    if(selected>=0 && selected!=s.output_res) {s.output_res=selected; dirty=true;}
    const char* sizes[Ps4RtOutput::Count]{}; int values[Ps4RtOutput::Count]{},count=0;
    for(int index:Ps4RtOutput::Order) if(Ps4RtOutput::Filter(supported,s.output_aspect)&(1ull<<index)) {
        sizes[count]=Ps4RtOutput::Names[index]; values[count++]=index;
    }
    if(count) CockpitField(Tr(L"Output resolution",L"Résolution de sortie"),s.output_res,sizes,values,count);
    else ImGui::TextWrapped("%s",Tr(L"No verified display mode is available.",L"Aucun mode d’affichage vérifié n’est disponible."));
    ImGui::BeginDisabled(s.display_mode!=Ps4RtGraphics::Windowed);
    const char* fits[]={Tr(L"Original",L"Original"),Tr(L"Stretch to window",L"Étirer à la fenêtre")};
    std::atomic<int> stretch{int(s.window_stretch.load())};
    CockpitField(Tr(L"Image fitting",L"Ajustement de l’image"),stretch,fits,nullptr,2);
    s.window_stretch=stretch!=0;
    ImGui::EndDisabled();
    ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.5f,.75f,.87f,1));
    ImGui::TextWrapped("%s",Tr(L"Proportions preview",L"Aperçu des proportions"));ImGui::PopStyleColor();
    const ImVec2 pos=ImGui::GetCursorScreenPos();
    const auto surface=Ps4RtOutput::Size{Ps4RtOutput::Widths[s.output_res],Ps4RtOutput::Heights[s.output_res]};
    const float width=ImGui::GetContentRegionAvail().x,height=std::min(width*float(surface.height)/surface.width,240*base_scale);
    const float fitted_width=std::min(width,height*float(surface.width)/surface.height);
    const ImVec2 a(pos.x+(width-fitted_width)*.5f,pos.y),b(a.x+fitted_width,a.y+height);
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(a,b,IM_COL32(2,9,16,255),7*base_scale);
    const bool stretch_image=Ps4RtGraphics::StretchWindow(s.display_mode,s.window_stretch);
    const float image_width=stretch_image?fitted_width:std::min(fitted_width,height*16/9);
    const float image_height=stretch_image?height:std::min(height,fitted_width*9/16);
    const ImVec2 image_a(a.x+(fitted_width-image_width)*.5f,a.y+(height-image_height)*.5f),image_b(image_a.x+image_width,image_a.y+image_height);
    if(menu_art_texture) draw->AddImage(menu_art_texture->GetTexRef(),image_a,image_b);
    else draw->AddRectFilledMultiColor(image_a,image_b,IM_COL32(14,40,61,255),IM_COL32(9,27,43,255),IM_COL32(20,99,120,255),IM_COL32(13,59,81,255));
    draw->AddRect(a,b,IM_COL32(47,89,111,255),7*base_scale);
    ImGui::Dummy(ImVec2(width,height));ImGui::Spacing();
    ImGui::TextWrapped("%s",Tr(L"Output resolution",L"Résolution de sortie"));
    ImGui::Text("%d × %d",surface.width,surface.height);
    if(Ps4RtUltrawide::Enabled()) ImGui::TextWrapped("%s",Tr(
        L"Experimental ultrawide: restart the game to change aspect ratio. HUD and movies keep their original proportions.",
        L"Ultralarge expérimental : redémarrez le jeu pour changer de format. Le HUD et les vidéos conservent leurs proportions d’origine."));
    ImGui::EndTable();
}

void CockpitImage() {
    auto& s=Ps4RtSettings::Get();
    if(!BeginRows("##image-live")) return;
    const char* providers[]={Tr(L"Disabled",L"Désactivé"),"FSR 3.1","FSR 4 v07","DLSS Super Resolution"};
    const int values[]={Ps4RtSettings::UpscalerOff,Ps4RtSettings::UpscalerFsr3,Ps4RtSettings::UpscalerFsr4,Ps4RtSettings::UpscalerDlss};
    const bool temporal=Ps4RtOutput::TemporalAllowed(s.output_res);
    const bool available[]={true,temporal&&s.fsr3_available,temporal&&s.fsr4_supported,temporal&&s.dlss_supported};
    std::atomic<int> provider{s.upscaler.load()};
    Row(Tr(L"Upscaling",L"Reconstruction"));
    const char* preview=providers[0];for(int i=0;i<4;++i)if(values[i]==provider)preview=providers[i];
    if(ImGui::BeginCombo("##provider",preview)) {
        for(int i=0;i<4;++i) {
            ImGui::BeginDisabled(!available[i]);
            if(ImGui::Selectable(providers[i],values[i]==provider)&&available[i]){provider=values[i];dirty=true;}
            ImGui::EndDisabled();
            if(!available[i]&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s",Tr(L"Unavailable for this GPU, installed components or output mode.",L"Indisponible pour ce GPU, les composants installés ou le mode de sortie."));
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    if(provider!=s.upscaler) Ps4RtSettings::SetSceneUpscaler(provider,false);
    const bool manual=s.upscaler==Ps4RtSettings::UpscalerOff;
    if(manual) {
        const auto output=Ps4RtUltrawide::Content(s.output_res);
        const auto selected=Ps4RtScene::NativeInput(s.native_render_height,output.width,output.height,Ps4RtUltrawide::Enabled());
        char label[80]{};std::snprintf(label,sizeof(label),"%u × %u",selected.width,selected.height);
        Row(Tr(L"Render resolution",L"Résolution de rendu"));
        if(ImGui::BeginCombo("##native-render",label)) {
            for(int height:Ps4RtScene::NativeHeights) {
                const auto size=Ps4RtScene::NativeInput(height,output.width,output.height,Ps4RtUltrawide::Enabled());
                const bool fits=size.width<=u32(s.raster_max_width.load()) && size.height<=u32(s.raster_max_height.load());
                std::snprintf(label,sizeof(label),"%u × %u",size.width,size.height);
                ImGui::BeginDisabled(!fits);
                if(ImGui::Selectable(label,s.native_render_height==height) && fits){s.native_render_height=height;dirty=true;}
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
    if(!manual) {
    const auto input_output=Ps4RtUltrawide::Content(s.output_res);
    pending_scene_preset=Ps4RtScene::ForOutput(pending_scene_preset,input_output.width,input_output.height);
    const int first_preset=(input_output.width<1920 || input_output.height<1080)?1:0;
    const char* presets[]={Tr(L"Original 1080p",L"1080p d’origine"),Tr(L"Native (100%)",L"Natif (100 %)"),Tr(L"Quality",L"Qualité"),
        Tr(L"Balanced",L"Équilibré"),Tr(L"Performance",L"Performances"),Tr(L"Ultra Performance",L"Ultra Performance")};
    const int preset_values[]={-1,0,1,2,3,4};std::atomic<int> requested{pending_scene_preset};
    // The shared size choices select the CURRENT provider's model/mode.
    // Keep its identity visible; NVIDIA hardware alone does not select DLSS.
    const char* preset_provider=nullptr;
    for(int i=1;i<4;++i) if(values[i]==s.upscaler) preset_provider=providers[i];
    char preset_label[256]{};
    const char* preset_title=Tr(L"Quality preset",L"Preset de reconstruction");
    std::snprintf(preset_label,sizeof(preset_label),"%s%s%s",preset_title,
        preset_provider?" · ":"",preset_provider?preset_provider:"");
    ChoiceRow(preset_label,requested,presets+first_preset,preset_values+first_preset,6-first_preset);
    pending_scene_preset=requested;
    }
    char dimensions[64]{};
    std::snprintf(dimensions,sizeof(dimensions),"%d × %d",s.active_render_width.load(),s.active_render_height.load());
    InfoRow(Tr(L"Applied render resolution",L"Résolution de rendu appliquée"),dimensions);
    const auto content=Ps4RtUltrawide::Content(s.output_res);
    const auto wanted=Ps4RtScene::RequestedInput(!manual,pending_scene_preset,s.native_render_height,content.width,content.height,Ps4RtUltrawide::Enabled());
    std::snprintf(dimensions,sizeof(dimensions),"%u × %u",wanted.width,wanted.height);
    InfoRow(Tr(L"Requested input resolution",L"Résolution de rendu demandée"),dimensions);
    const char* anisotropy_labels[]={Tr(L"Game setting",L"Réglage du jeu"),"×2","×4","×8","×16"};
    ChoiceRow(Tr(L"Anisotropic filtering",L"Filtrage anisotrope"),s.anisotropy,anisotropy_labels,
              Ps4RtGraphics::Anisotropies.data(),int(Ps4RtGraphics::Anisotropies.size()),
              Tr(L"Sharpens textures viewed at an angle. Changes apply after restarting the game.",L"Améliore les textures vues en biais. Les changements s’appliquent après redémarrage du jeu."));
    char active_anisotropy[24]{};
    if(s.startup_anisotropy) std::snprintf(active_anisotropy,sizeof(active_anisotropy),"×%d",s.startup_anisotropy);
    else std::snprintf(active_anisotropy,sizeof(active_anisotropy),"%s",Tr(L"Game setting",L"Réglage du jeu"));
    InfoRow(Tr(L"Filtering for this session",L"Filtrage de cette session"),active_anisotropy);
    Row(Tr(L"Sharpening",L"Netteté"));
    int strength=s.sharpen?int(s.sharpness*100.0f+0.5f):0;
    if(ImGui::SliderInt("##value",&strength,0,100,"%d%%",ImGuiSliderFlags_AlwaysClamp)) {
        s.sharpness=strength/100.0f; s.sharpen=strength!=0;
        s.rcas=s.upscaler==Ps4RtSettings::UpscalerOff && strength!=0;
        s.rcas_strength=strength; dirty=true;
    }
    ImGui::PopID();
    ImGui::EndTable();
    ImGui::Spacing();
    const auto requested_input=wanted;
    const double x=double(requested_input.width)/content.width,y=double(requested_input.height)/content.height;
    ImGui::TextWrapped("%s: %.1f%% × %.1f%% · %.1f%%",Tr(L"Input scale / pixel count",L"Échelle de rendu / nombre de pixels"),x*100,y*100,x*y*100);
    if(manual) ImGui::TextWrapped("%s",Tr(
        L"Without reconstruction, choose the internal resolution manually. The UI keeps the output resolution. With FSR or DLSS, quality presets control the internal resolution.",
        L"Sans reconstruction, choisissez manuellement la résolution interne. L’UI conserve la résolution de sortie. Avec FSR ou DLSS, les presets gèrent la résolution interne."));
    else switch(pending_scene_preset) {
    case -1:ImGui::TextWrapped("%s",Tr(L"Fixed original 1080p input. Its reconstruction ratio depends on the output resolution.",L"Rendu d’origine fixe en 1080p. Son taux de reconstruction dépend de la résolution de sortie."));break;
    case 0:ImGui::TextWrapped("%s",Tr(L"Input matches output. Temporal anti-aliasing is active without reducing render resolution. Highest GPU workload among these presets.",L"Le rendu correspond à la sortie. L’anticrénelage temporel est actif sans réduire la résolution de rendu. C’est le preset le plus exigeant pour le GPU."));break;
    case 1:ImGui::TextWrapped("%s",Tr(L"Prioritizes fine detail with a moderate reduction in GPU workload.",L"Privilégie les détails fins avec une réduction modérée de la charge GPU."));break;
    case 2:ImGui::TextWrapped("%s",Tr(L"Balances input detail and GPU workload.",L"Équilibre les détails du rendu et la charge GPU."));break;
    case 3:ImGui::TextWrapped("%s",Tr(L"Prioritizes lower GPU workload, with more reconstruction and less fine detail.",L"Privilégie une charge GPU réduite, avec davantage de reconstruction et moins de détails fins."));break;
    case 4:ImGui::TextWrapped("%s",Tr(L"Lowest input resolution and GPU workload. Detail loss and reconstruction artifacts are more likely.",L"Résolution de rendu et charge GPU minimales. Les pertes de détails et les artefacts de reconstruction sont plus probables."));break;
    }
    ImGui::Spacing();
    ImGui::TextWrapped("%s",Tr(L"The preset is applied when this menu closes.",L"Le preset s’applique à la fermeture de ce menu."));
    ImGui::TextWrapped("%s",s.anisotropy!=s.startup_anisotropy?
        Tr(L"Anisotropic filtering change saved for the next game launch.",L"Le changement de filtrage anisotrope est enregistré pour le prochain lancement."):
        Tr(L"Anisotropic filtering changes require a game restart.",L"Les changements de filtrage anisotrope nécessitent un redémarrage du jeu."));
}


const char* CheatText(const wchar_t* english){return Tr(english,ps4rt_localize(L"Français",english));}
bool CheatSwitch(bool on) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,15*base_scale);
    ImGui::PushStyleColor(ImGuiCol_Button,on?ImVec4(.1f,.48f,.60f,1):ImVec4(.13f,.22f,.28f,1));
    const ImVec2 start=ImGui::GetCursorScreenPos(),size(54*base_scale,30*base_scale);
    const bool clicked=ImGui::Button("##switch",size);
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(start.x+(on?39:15)*base_scale,start.y+15*base_scale),9*base_scale,IM_COL32(94,221,250,255));
    ImGui::PopStyleColor();ImGui::PopStyleVar();return clicked;
}
bool CheatValueSelector(int& choice,int count,float width,const char* value) {
    ImGui::TextUnformatted(value);
    const float available=ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(std::max(1.f,std::min(width,available-6*base_scale)));
    // Float slider keeps a small thumb; a two-value integer slider fills half the track.
    float position=float(choice)/float(count-1);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(6*base_scale,2*base_scale));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize,10*base_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,4*base_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,3*base_scale);
    const int previous=choice;
    if(ImGui::SliderFloat("##value",&position,0.f,1.f,"",ImGuiSliderFlags_AlwaysClamp|ImGuiSliderFlags_NoInput))
        choice=std::clamp(int(std::lround(position*float(count-1))),0,count-1);
    if(ImGui::IsItemFocused()) {
        const bool right=ImGui::IsKeyPressed(ImGuiKey_RightArrow,true)||ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight,true)||ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight,true);
        const bool left=ImGui::IsKeyPressed(ImGuiKey_LeftArrow,true)||ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft,true)||ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft,true);
        if(right!=left)choice=std::clamp(previous+(right?1:-1),0,count-1);
    }
    ImGui::PopStyleVar(4);return choice!=previous;
}
bool CheatMovementSwitch(int& position,float width) {
    const int previous=position;
    const bool comma=MenuLanguage()!=L"English"&&MenuLanguage()!=L"简体中文"&&MenuLanguage()!=L"繁體中文"&&MenuLanguage()!=L"한국어"&&MenuLanguage()!=L"日本語";
    const char* labels[]={"×1",comma?"×1,5":"×1.5","×2"};
    const float available=std::max(3.f,std::min(width,ImGui::GetContentRegionAvail().x-6*base_scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(3*base_scale,2*base_scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,5*base_scale);
    ImGui::BeginGroup();
    for(int tick=0;tick<3;++tick) {
        if(tick)ImGui::SameLine();ImGui::PushID(tick);
        const bool selected=position==tick;
        ImGui::PushStyleColor(ImGuiCol_Button,selected?ImVec4(.30f,.79f,.94f,1):ImVec4(.08f,.17f,.23f,1));
        ImGui::PushStyleColor(ImGuiCol_Text,selected?ImVec4(.02f,.08f,.12f,1):ImVec4(.86f,.94f,.98f,1));
        if(ImGui::Button(labels[tick],ImVec2(available/3,30*base_scale)))position=tick;
        if(ImGui::IsItemFocused()) {
            const bool right=ImGui::IsKeyPressed(ImGuiKey_RightArrow,true)||ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight,true)||ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight,true);
            const bool left=ImGui::IsKeyPressed(ImGuiKey_LeftArrow,true)||ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft,true)||ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft,true);
            if(right!=left)position=std::clamp(previous+(right?1:-1),0,2);
        }
        ImGui::PopStyleColor(2);ImGui::PopID();
    }
    ImGui::EndGroup();ImGui::PopStyleVar(3);return position!=previous;
}
void CockpitCheats() {
    static std::array<int,64> pending_choices{};
    ImGui::TextWrapped("%s",CheatText(L"Cheats start off each session. Changes apply immediately."));
    const int count=runtime_cheat_count();
    {
        const bool builtin=true;
        ImGui::PushID(int(builtin));
        ImGui::SeparatorText(CheatText(L"Built-in cheats"));
        bool any=false;
        for(int i=0;i<count;++i)if(bool(runtime_cheat_builtin(i))==builtin) {
            any=true;ImGui::PushID(i);
            const auto* definition=builtin?So5Cheats::Catalog(runtime_cheat_catalog_id(i)):nullptr;
            const int state=runtime_cheat_state(i),choices=runtime_cheat_choice_count(i);
            int selected=runtime_cheat_choice(i);if(selected>=0&&i<64)pending_choices[size_t(i)]=selected;
            const float width=ImGui::GetContentRegionAvail().x,control=std::min(width*.30f,148*base_scale),gap=18*base_scale;
            const ImVec2 start=ImGui::GetCursorPos();
            ImGui::BeginGroup();ImGui::PushTextWrapPos(start.x+width-control-gap);
            ImGui::TextWrapped("%s",definition?CheatText(definition->title):runtime_cheat_name(i));
            ImGui::PushStyleColor(ImGuiCol_Text,definition&&definition->id==8?ImVec4(.98f,.75f,.42f,1):ImVec4(.67f,.79f,.85f,1));
            const char* description=definition?CheatText(definition->detail):runtime_cheat_description(i);
            ImGui::TextWrapped("%s",description&&description[0]?description:CheatText(L"No description supplied by the profile."));
            if(definition&&definition->warning)ImGui::TextWrapped("%s",CheatText(definition->warning));
            ImGui::PopStyleColor();
            if(state<0)ImGui::TextWrapped("%s",CheatText(L"Unavailable for this executable"));
            ImGui::PopTextWrapPos();ImGui::EndGroup();
            ImGui::SameLine(start.x+width-control);ImGui::BeginGroup();ImGui::BeginDisabled(state<0);
            char result[512]{};
            if(runtime_cheat_is_action(i)) {
                if(ImGui::Button(Tr(L"Add",L"Ajouter"))&&runtime_cheat_add(i,result,sizeof(result)))Ps4RtOverlay::ShowNotice(CheatText(L"Code change refused. See Logs and diagnostics."));
            } else if(definition&&definition->id==9&&choices==2) {
                int position=state>0?std::clamp(selected+1,1,2):0;
                if(CheatMovementSwitch(position,control)) {
                    const int code=position==0?runtime_cheat_set(i,0,result,sizeof(result)):runtime_cheat_select(i,position-1,result,sizeof(result));
                    if(code)Ps4RtOverlay::ShowNotice(CheatText(L"Code change refused. See Logs and diagnostics."));
                }
                ImGui::TextWrapped("%s",CheatText(position?L"Enabled":L"Disabled"));
            } else {
                if(CheatSwitch(state>0)) {
                    const int code=state>0?runtime_cheat_set(i,0,result,sizeof(result)):choices&&i<64?runtime_cheat_select(i,pending_choices[size_t(i)],result,sizeof(result)):runtime_cheat_set(i,1,result,sizeof(result));
                    if(code)Ps4RtOverlay::ShowNotice(CheatText(L"Code change refused. See Logs and diagnostics."));
                }
                ImGui::TextWrapped("%s",CheatText(state>0?L"Enabled":L"Disabled"));
                if(choices>1&&i<64) {
                    int choice=std::clamp(pending_choices[size_t(i)],0,choices-1);char label[32];
                    std::snprintf(label,sizeof(label),definition&&definition->id==9?"×%.2g":"%.2g",runtime_cheat_choice_value(i,choice));
                    if(MenuLanguage()!=L"English"&&MenuLanguage()!=L"简体中文"&&MenuLanguage()!=L"繁體中文"&&MenuLanguage()!=L"한국어"&&MenuLanguage()!=L"日本語")for(char* at=label;*at;++at)if(*at=='.')*at=',';
                    if(CheatValueSelector(choice,choices,control,label)) {
                        pending_choices[size_t(i)]=choice;
                        if(state>0&&runtime_cheat_select(i,choice,result,sizeof(result)))Ps4RtOverlay::ShowNotice(CheatText(L"Code change refused. See Logs and diagnostics."));
                    }
                }
            }
            ImGui::EndDisabled();ImGui::EndGroup();ImGui::Spacing();ImGui::Separator();ImGui::PopID();
        }
        if(!any)ImGui::TextWrapped("%s",CheatText(L"No compatible built-in cheats."));
        ImGui::PopID();
    }
    ImGui::TextWrapped("%s",CheatText(L"Disabling a code stops its effect. Items, levels and stats already obtained are kept."));
    if(runtime_cheat_file_problem_count())ImGui::TextWrapped("%s",CheatText(L"Code change refused. See Logs and diagnostics."));
}

void CenteredMenuText(const char* text) {
    const float left=ImGui::GetCursorPosX(),width=ImGui::GetContentRegionAvail().x;
    const char* end=text+std::strlen(text);
    while(text<end) {
        const char* next=ImGui::GetFont()->CalcWordWrapPosition(ImGui::GetFontSize(),text,end,width);
        if(next<=text) next=end;
        ImGui::SetCursorPosX(left+std::max(0.0f,(width-ImGui::CalcTextSize(text,next).x)*.5f));
        ImGui::TextUnformatted(text,next);
        text=next;while(text<end && (*text==' ' || *text=='\n' || *text=='\r')) ++text;
    }
    ImGui::SetCursorPosX(left);
}

void ExitConfirmation(const ImGuiViewport* viewport) {
    if(cockpit_exit_confirmation && !ImGui::IsPopupOpen("##so5-exit")) ImGui::OpenPopup("##so5-exit");
    const float desired=560*base_scale;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x+viewport->WorkSize.x*.5f,viewport->WorkPos.y+viewport->WorkSize.y*.5f),ImGuiCond_Always,ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(desired,viewport->WorkSize.x*.88f),0),ImGuiCond_Always);
    if(!ImGui::BeginPopupModal("##so5-exit",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove)) return;
    if(!cockpit_exit_confirmation) {ImGui::CloseCurrentPopup();ImGui::EndPopup();return;}
    ImGui::Spacing();CenteredMenuText(Tr(L"Do you want to exit the game?",L"Voulez-vous quitter le jeu ?"));
    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
    CenteredMenuText(Tr(L"Unsaved game progress will be lost.",L"La progression non sauvegardée sera perdue."));
    ImGui::Dummy(ImVec2(0,12*base_scale));
    const char* cancel=Tr(L"Cancel",L"Annuler"),*exit=Tr(L"Exit to desktop",L"Quitter vers le bureau");
    const float width=ImGui::GetContentRegionAvail().x,gap=ImGui::GetStyle().ItemSpacing.x;
    const float wanted=std::max(ImGui::CalcTextSize(cancel).x,ImGui::CalcTextSize(exit).x)+36*base_scale;
    const bool stacked=wanted*2+gap>width;
    const float button_width=stacked?width:(width-gap)*.5f;
    if(ImGui::Button(cancel,ImVec2(button_width,42*base_scale)) || ImGui::IsKeyPressed(ImGuiKey_Escape,false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight,false)) {
        cockpit_exit_confirmation=false;ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();if(!stacked) ImGui::SameLine();
    if(ImGui::Button(exit,ImVec2(button_width,42*base_scale))) {
        CommitPendingPreset();Ps4RtSettings::Save();dirty=false;
        SDL_Event quit{};quit.type=SDL_EVENT_QUIT;
        if(SDL_PushEvent(&quit)) {cockpit_exit_confirmation=false;ImGui::CloseCurrentPopup();}
    }
    ImGui::EndPopup();
}

void CockpitSmoothness() {
    auto& s=Ps4RtSettings::Get();
    if(BeginRows("##smooth-live")) {
        CockpitSwitch(Tr(L"Vertical synchronization",L"Synchronisation verticale"),s.vsync);
        const char* fps[]={"30 FPS","60 FPS",Tr(L"Unlimited",L"Illimité")}; const int limits[]={30,60,0};
        ChoiceRow(Tr(L"Frame-rate limit",L"Limite d’IPS"),s.fps_limit,fps,limits,3);
        CockpitSwitch(Tr(L"Asynchronous shaders",L"Shaders asynchrones"),s.async_shaders);
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::TextWrapped("%s",s.async_shaders?Tr(L"Smoother gameplay with fewer shader compilation stutters. Brief visual flicker may occasionally appear.",L"Moins de saccades lors de la compilation des shaders. De brefs clignotements visuels peuvent apparaître."):
        Tr(L"First-use shader compilation may briefly interrupt gameplay.",L"La première compilation d’un shader peut brièvement interrompre la fluidité du jeu."));
}

void CockpitController() {
    if(BeginRows("##pad-live")) {
        SliderRow(Tr(L"Vibration strength",L"Intensité des vibrations"),Ps4RtSettings::Get().vibration,0,100,"%d%%",nullptr);
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::TextWrapped("%s",Tr(L"Controller selection and bindings are available in the launcher.",L"La sélection des périphériques et le mappage sont disponibles dans le lanceur."));
}

void TweakDescription(const wchar_t* text) {
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.67f,.79f,.85f,1));
    ImGui::TextWrapped("%s",Tr(text,ps4rt_localize(L"Français",text)));
    ImGui::PopStyleColor();ImGui::Spacing();
}
void CockpitTweaks() {
    auto& s=Ps4RtSettings::Get();
    ImGui::TextColored(ImVec4(.3f,.85f,1,1),"%s",Tr(L"Tweaks",L"Ajustements"));
    ImGui::Spacing();
    ImGui::SeparatorText((std::string(Tr(L"Change leader",L"Changer de leader"))+" (Let Me Lead! Fidel!)").c_str());
    TweakDescription(L"During exploration, use D-pad left/right or O/P to change leader. Your choice is saved.");
    auto& enabled=Ps4RtSettings::Get().exploration_leader_enabled;
    ImGui::BeginDisabled(!Ps4RtLeader::Available());
    ImGui::PushID("leader-tweak");
    const float width=ImGui::GetContentRegionAvail().x;
    const ImVec2 start=ImGui::GetCursorPos();
    ImGui::BeginGroup();ImGui::PushTextWrapPos(start.x+width-88*base_scale);
    ImGui::TextWrapped("%s",Tr(L"Change leader during exploration",L"Changer de leader en exploration"));
    ImGui::PopTextWrapPos();ImGui::EndGroup();
    ImGui::SameLine(start.x+width-72*base_scale);
    if(CheatSwitch(enabled.load())) Store(enabled,!enabled.load(),true);
    ImGui::PopID();ImGui::Spacing();
    ImGui::EndDisabled();
    if(Ps4RtLeader::Available()) {
        ImGui::BeginDisabled(!enabled);
        if(enabled)Ps4RtLeader::Inspect();
        const auto leader=Ps4RtLeader::GetView();
        ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
        const char* names[]={"Fidel","Miki","Victor","Fiore","Emmerson","Anne"};
        const char* choices[6]{};int ids[6]{},count=0;
        for(int id=0;id<6;++id)if(leader.available_mask&(1u<<id)) {choices[count]=names[id];ids[count++]=id;}
        if(count) {
            std::atomic<int> selection{leader.current};
            ImGui::BeginDisabled(!leader.ready || leader.result==Ps4RtLeader::Result::Pending);
            CockpitField(Tr(L"Exploration leader",L"Leader en exploration"),selection,choices,ids,count);
            ImGui::EndDisabled();
            if(selection!=leader.current && !Ps4RtLeader::Request(selection))
                Ps4RtOverlay::ShowNotice(Tr(L"Leader change refused. See Logs and diagnostics.",L"Changement de leader refusé. Consultez le journal et le diagnostic."));
        } else {
            ImGui::TextWrapped("%s",Tr(L"Exploration leader unavailable in the current scene.",L"Changement de leader indisponible dans la scène actuelle."));
        }
        ImGui::TextWrapped("%s",Tr(L"During exploration, use D-pad left/right or O/P to change leader. Your choice is saved and restored after loading or battle. Menu choices apply after Resume.",
            L"En exploration, utilisez la croix gauche/droite ou O/P pour changer de leader. Votre choix est mémorisé et restauré après un chargement ou un combat. Un choix dans ce menu s’applique après Reprendre."));
          ImGui::Spacing();
          ImGui::TextWrapped("%s",Tr(L"After changing leader, move clear of a zone entrance before using it. Story transitions remain unchanged.",
              L"Après un changement de leader, éloignez-vous de l’entrée d’une zone avant de la franchir. Les transitions du scénario restent inchangées."));
        if(leader.result==Ps4RtLeader::Result::Pending)
            ImGui::TextWrapped("%s",Tr(L"Resume to apply the selected exploration leader.",L"Reprenez le jeu pour appliquer le leader sélectionné."));
        else if(leader.result==Ps4RtLeader::Result::Applied)
            ImGui::TextWrapped("%s",Tr(L"Leader changed.",L"Leader changé."));
        else if(leader.result==Ps4RtLeader::Result::Refused)
            ImGui::TextWrapped("%s",Tr(L"Leader change refused. See Logs and diagnostics.",L"Changement de leader refusé. Consultez le journal et le diagnostic."));
        ImGui::EndDisabled();
    } else {
        ImGui::TextWrapped("%s",Tr(L"This patch is unavailable for the prepared game version.",L"Ce patch est indisponible pour la version du jeu préparée."));
    }
    ImGui::Spacing();ImGui::SeparatorText(Tr(L"Camera fixes",L"Réglages de la caméra"));
    TweakDescription(L"Softens positional camera follow. Original restores the game setting.");
    ImGui::BeginDisabled(!Ps4RtCameraComfort::Available());
    const char* follow_modes[]={Tr(L"Original",L"Original"),
        Tr(L"Light damping",L"Amortissement léger"),Tr(L"Damped follow",L"Suivi amorti")};
    const int previous=s.camera_follow.load();
    CockpitField(Tr(L"Camera follow",L"Suivi de la caméra"),s.camera_follow,follow_modes,nullptr,3);
    if(previous!=s.camera_follow.load()&&!Ps4RtCameraComfort::Select(s.camera_follow.load()))s.camera_follow=previous;
    ImGui::EndDisabled();
    if(!Ps4RtCameraComfort::Available())TweakDescription(L"This patch is unavailable for the prepared game version.");
    ImGui::Spacing();ImGui::SeparatorText(Tr(L"HUD adjust",L"Disposition du HUD"));
    if(Ps4RtHud::Active())CockpitHudPresets();
    else TweakDescription(L"This patch is unavailable for the prepared game version.");

}

void CockpitPerformance() {
    auto& s=Ps4RtSettings::Get();
    if(BeginRows("##stats-live")) {
        CockpitSwitch(Tr(L"Show in game",L"Afficher en jeu"),s.show_fps);
        ImGui::EndTable();
    }
    ImGui::BeginDisabled(!s.show_fps);
    const char* metrics[]={Tr(L"Frames per second (FPS)",L"Images par seconde (IPS)"),Tr(L"Frame time",L"Temps par image"),
        Tr(L"Active upscaler",L"Reconstruction active"),Tr(L"GPU name and load",L"Nom et charge du GPU"),Tr(L"CPU usage",L"Utilisation du CPU"),
        Tr(L"System memory",L"Mémoire système"),Tr(L"Video memory",L"Mémoire vidéo"),Tr(L"Application version",L"Version de l’application")};
    if(ImGui::BeginTable("##metrics",ImGui::GetContentRegionAvail().x>640*base_scale?2:1)) {
        for(unsigned i=0;i<std::size(metrics);++i) {
            ImGui::TableNextColumn(); bool value=(s.overlay_items&(1u<<i))!=0;
            if(ImGui::Checkbox(metrics[i],&value)) {s.overlay_items=value?(s.overlay_items|(1u<<i)):(s.overlay_items&~(1u<<i)); dirty=true;}
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if(BeginRows("##stats-models")) {
        ImGui::BeginDisabled(!(s.overlay_items & Ps4RtPerfStats::Gpu));
        CockpitSwitch(Tr(L"Show GPU model",L"Afficher le modèle du GPU"),s.overlay_gpu_model);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!(s.overlay_items & Ps4RtPerfStats::Cpu));
        CockpitSwitch(Tr(L"Show CPU model",L"Afficher le modèle du CPU"),s.overlay_cpu_model);
        ImGui::EndDisabled();
        ImGui::EndTable();
    }
    if(BeginRows("##stats-style")) {
        const char* corners[]={Tr(L"Top left",L"En haut à gauche"),Tr(L"Top right",L"En haut à droite"),Tr(L"Bottom left",L"En bas à gauche"),Tr(L"Bottom right",L"En bas à droite"),Tr(L"Top center",L"En haut au milieu"),Tr(L"Bottom center",L"En bas au milieu")};
        ChoiceRow(Tr(L"Position",L"Position"),s.overlay_corner,corners,nullptr,6);
        const char* layouts[]={Tr(L"One line",L"Une seule ligne"),Tr(L"One item per line",L"Un indicateur par ligne")};
        ChoiceRow(Tr(L"Layout",L"Disposition"),s.overlay_layout,layouts,nullptr,2);
        SliderRow(Tr(L"Text size",L"Taille du texte"),s.overlay_scale,50,300,"%d%%",nullptr);
        SliderRow(Tr(L"Background opacity",L"Opacité du fond"),s.overlay_opacity,0,100,"%d%%",nullptr);
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
}

void CockpitMenu() {
    auto& s=Ps4RtSettings::Get();
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x+viewport->WorkSize.x*.5f,viewport->WorkPos.y+viewport->WorkSize.y*.5f),ImGuiCond_Always,ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(1080.0f*base_scale,viewport->WorkSize.x*.9f),std::min(790.0f*base_scale,viewport->WorkSize.y*.86f)),ImGuiCond_Always);
    if(!ImGui::Begin("##so5-cockpit",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoMove)) {ImGui::End();return;}
    bool keep_open=true;
    if(menu_logo_texture) {
        const float h=90*base_scale,w=h*menu_logo_size.x/menu_logo_size.y;
        ImGui::Image(menu_logo_texture->GetTexRef(),ImVec2(w,h)); ImGui::SameLine(0,24*base_scale);
    }
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(.3f,.85f,1,1),"%s",Tr(L"IN-GAME SETTINGS",L"RÉGLAGES EN JEU"));
    ImGui::TextWrapped("%s",Tr(L"Your game, your settings.",L"Votre partie, vos réglages."));
    ImGui::TextDisabled("Windows · %s",PS4RT_VERSION_STRING);
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth()-ImGui::GetStyle().WindowPadding.x-ImGui::GetFrameHeight());
    if(ImGui::Button("×##close",ImVec2(ImGui::GetFrameHeight(),ImGui::GetFrameHeight()))) keep_open=false;
    ImGui::Separator();
    // Keep saved IDs stable; Tweaks has its own ID between Cheats and Smoothness.
    constexpr int order[]={0,1,2,6,3,4,5};
    constexpr int count=std::size(order);
    if(!ImGui::GetIO().WantTextInput && !cockpit_exit_confirmation) {
        const int step=ImGui::IsKeyPressed(ImGuiKey_GamepadR1,false)||ImGui::IsKeyPressed(ImGuiKey_E,false)?1:
            ImGui::IsKeyPressed(ImGuiKey_GamepadL1,false)||ImGui::IsKeyPressed(ImGuiKey_Q,false)?-1:0;
        if(step) {
            const auto found=std::find(std::begin(order),std::end(order),s.menu_tab.load());
            const int at=found==std::end(order)?0:int(found-std::begin(order));
            requested_tab=order[(at+step+count)%count];
        }
    }
    const float footer=ImGui::GetTextLineHeightWithSpacing()*2+ImGui::GetFrameHeightWithSpacing();
    const char* tabs[]={Tr(L"Display",L"Affichage"),Tr(L"Image",L"Image"),Tr(L"Cheats",L"Triches"),Tr(L"Smoothness",L"Fluidité"),Tr(L"Controller",L"Manette"),Tr(L"Performance",L"Performances"),Tr(L"Tweaks",L"Ajustements")};
    if(requested_tab>=0) {s.menu_tab=std::clamp(requested_tab,0,count-1);requested_tab=-1;dirty=true;}
    const int columns=ImGui::GetContentRegionAvail().x<850*base_scale?4:count;
    if(ImGui::BeginTable("##cockpit-tabs",columns,ImGuiTableFlags_SizingStretchSame)) {
        for(int tab:order) {
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button,s.menu_tab==tab?ImVec4(.07f,.22f,.29f,1):ImVec4(.03f,.10f,.15f,1));
            ImGui::PushStyleColor(ImGuiCol_Text,s.menu_tab==tab?ImVec4(.35f,.88f,1,1):ImVec4(.68f,.80f,.87f,1));
            auto label=std::string(tabs[tab]);
            const float text_width=ImGui::CalcTextSize(label.c_str()).x;
            if(text_width>ImGui::GetContentRegionAvail().x-ImGui::GetStyle().FramePadding.x*2) {
                size_t split=0;
                for(size_t at=1;at<label.size();++at) {
                    if((static_cast<unsigned char>(label[at])&0xc0)==0x80) continue;
                    if(ImGui::CalcTextSize(label.c_str(),label.c_str()+at).x>=text_width*.5f) {split=at;break;}
                }
                if(split) label.insert(split,"\n");
            }
            label+="###cockpit-tab";
            ImGui::PushID(tab);
            if(ImGui::Button(label.c_str(),ImVec2(-FLT_MIN,56*base_scale))) {s.menu_tab=tab;dirty=true;}
            ImGui::PopID();ImGui::PopStyleColor(2);
        }
        ImGui::EndTable();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(12*base_scale,10*base_scale));
    ImGui::PushID(s.menu_tab.load());
    if(ImGui::BeginChild("##live-controls",ImVec2(0,-footer))) {
        switch(s.menu_tab.load()) {case 0:CockpitDisplay();break;case 1:CockpitImage();break;case 2:CockpitCheats();break;case 3:CockpitSmoothness();break;case 4:CockpitController();break;case 6:CockpitTweaks();break;default:CockpitPerformance();break;}
    }
    ImGui::EndChild();ImGui::PopID();ImGui::PopStyleVar();
    ImGui::Separator();
    ImGui::TextWrapped("%s",Tr(L"Tabs: L1 / R1 or Q / E. Insert: close.",L"Onglets : L1 / R1 ou Q / E. Inser : fermer."));
    const float buttons=ImGui::CalcTextSize(Tr(L"Exit game",L"Quitter le jeu")).x+ImGui::CalcTextSize(Tr(L"Resume",L"Reprendre")).x+90*base_scale;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+std::max(0.0f,ImGui::GetContentRegionAvail().x-buttons));
    if(ImGui::Button(Tr(L"Exit game",L"Quitter le jeu"))) {cockpit_exit_confirmation=true;ImGui::OpenPopup("##so5-exit");}
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.24f,.79f,.94f,1));
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.02f,.07f,.12f,1));
    if(ImGui::Button(Tr(L"Resume",L"Reprendre"))) keep_open=false;
    ImGui::PopStyleColor(2);
    const bool confirming=cockpit_exit_confirmation;
    ExitConfirmation(viewport);
    if(ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight,false)&&!cancel_was_editing&&!confirming) keep_open=false;
    ImGui::End(); if(!keep_open) {cockpit_exit_confirmation=false;SetOpen(false);}
}
