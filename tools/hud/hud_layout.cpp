// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud_layout.h"
#include <boost/json/src.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>
#include <windows.h>
#include <objbase.h>

namespace So5Hud {
namespace j=boost::json;
const std::array<Spec,GroupCount> Specs={{
    {"party_status","Party portraits / HP / MP",Anchor::BottomLeft},
    {"minimap","Minimap",Anchor::TopRight},
    {"enemy_info","Enemy information",Anchor::TopLeft},
    {"battle_results","Battle results / obtained items",Anchor::Right},
    {"combo_counter","Hit combo",Anchor::Original},
    {"battle_info_banner","Battle information",Anchor::Center},
    {"field_info_banner","Field information",Anchor::Center},
    {"battle_radial_menu","Battle radial menu",Anchor::Center},
    {"battle_target_selection","Target selection (visibility)",Anchor::Original,false,false,true,true},
    {"character_change","Character change",Anchor::Center},
    {"location_banner","Location name",Anchor::Center},
    {"battle_transition","Battle start / end",Anchor::Center},
    {"dialogue","Dialogue",Anchor::Center,false,false,false,true},
    {"world_markers","World targets / locked chests (visibility)",Anchor::World,false,true},
    {"damage_numbers","Damage numbers (white / other)",Anchor::World,false,true},
    {"skill_banner","Battle skill / spell name",Anchor::Center},
    {"combat_challenges","Combat challenges",Anchor::Left},
    {"level_up","Level up",Anchor::Center},
    {"action_prompts","Action buttons / prompts",Anchor::Right},
    {"loading_indicator","Loading indicator / frame / effects",Anchor::BottomRight},
    {"menu_background","Menu blue background",Anchor::Original,false,false,true,true},
    {"menu_animated_background","Menu animated background",Anchor::Original,false,false,true,true},
    {"battle_menu_background","Battle menu blue overlay",Anchor::Original,false,false,true,true},
    {"enemy_awareness","Enemy awareness (!)",Anchor::World,false,true},
    {"damage_received","Damage numbers (red)",Anchor::World,false,true},
    {"healing_numbers","Healing numbers (green)",Anchor::World,false,true},
    {"all_native_ui","All native UI (visibility)",Anchor::Original,false,false,true,true},
    {"private_action","Private action (PA)",Anchor::BottomRight},
    {"subtitles","Subtitles (unverified)",Anchor::Original},
    {"establishment_background","Establishment / shop background",Anchor::Original,false,false,true,true},
    {"quest_background","Quest menu background",Anchor::Original,false,false,true,true},
    {"pause_background","Pause menu background",Anchor::Original,false,false,true,true},
    {"pause_controls","Pause: Options / Return / Skip",Anchor::BottomRight}
}};
bool IsBackgroundGroup(size_t group){return group==20||group==21||group==22||group==29||group==30||group==31;}
namespace {
[[noreturn]] void Fail(const char* text) {throw std::runtime_error(text);}
bool Space(char c) {return c==' '||c=='\n'||c=='\r'||c=='\t';}
// Run after the JSON grammar/UTF-8 parser. DOM parsers can replace duplicate
// members silently, so check decoded member names before accepting the DOM.
class UniqueMembers {
    std::string_view s; size_t at=0;
    void White() {while(at<s.size()&&Space(s[at]))++at;}
    std::string String() {
        const auto begin=at++;
        while(at<s.size()) {char c=s[at++];if(c=='\\')++at;else if(c=='"')break;}
        auto v=j::parse(s.substr(begin,at-begin));return std::string(v.as_string());
    }
    void Value(unsigned depth) {
        if(depth>16)Fail("JSON nesting limit exceeded.");White();
        if(s[at]=='{') {
            ++at;White();std::set<std::string> names;
            if(s[at]=='}') {++at;return;}
            for(;;) {White();if(!names.insert(String()).second)Fail("Duplicate JSON member.");
                White();++at;Value(depth+1);White();if(s[at++]=='}')break;}
        } else if(s[at]=='[') {
            ++at;White();if(s[at]==']'){++at;return;}
            for(;;){Value(depth+1);White();if(s[at++]==']')break;}
        } else if(s[at]=='"')String();
        else while(at<s.size()&&s[at]!=','&&s[at]!=']'&&s[at]!='}'&&!Space(s[at]))++at;
    }
public:
    explicit UniqueMembers(std::string_view text):s(text){}
    void Check(){Value(0);}
};
j::value Json(std::string_view text,size_t limit) {
    if(text.empty()||text.size()>limit)Fail("JSON file is empty or too large.");
    j::parse_options options;options.max_depth=16;
    auto v=j::parse(text,{},options);UniqueMembers(text).Check();return v;
}
const j::object& Obj(const j::value& v) {if(!v.is_object())Fail("Expected a JSON object.");return v.as_object();}
const j::array& Arr(const j::value& v) {if(!v.is_array())Fail("Expected a JSON array.");return v.as_array();}
std::string Str(const j::value& v,size_t max=256) {
    if(!v.is_string()||v.as_string().size()>max)Fail("Invalid text field.");return std::string(v.as_string());
}
double Num(const j::value& v) {if(!v.is_number())Fail("Expected a number.");return j::value_to<double>(v);}
void Keys(const j::object& o,std::initializer_list<std::string_view> keys) {
    if(o.size()!=keys.size())Fail("Missing or unknown preset property.");
    for(auto key:keys)if(!o.contains(key))Fail("Missing preset property.");
}
size_t Group(std::string_view id) {
    for(size_t n=0;n<Specs.size();++n)if(Specs[n].id==id)return n;Fail("Unknown HUD group.");
}
Aspect ParseAspect(std::string_view s) {
    for(auto a:{Aspect::Standard,Aspect::Wide,Aspect::Superwide,Aspect::FourThree,Aspect::SixteenTen})if(AspectName(a)==s)return a;
    Fail("Unsupported aspect base.");
}
Anchor ParseAnchor(std::string_view s) {
    for(int a=0;a<=int(Anchor::World);++a)if(AnchorName(Anchor(a))==s)return Anchor(a);
    Fail("Unsupported anchor.");
}
}
std::string_view AspectName(Aspect a) {
    switch(a){case Aspect::Standard:return "16:9";case Aspect::Wide:return "21:9";case Aspect::Superwide:return "32:9";case Aspect::FourThree:return "4:3";case Aspect::SixteenTen:return "16:10";}
    Fail("Invalid aspect.");
}
std::string_view AnchorName(Anchor a) {
    constexpr std::string_view names[]={"original","top_left","top_right","bottom_left","bottom_right","left","right","center","world_attached"};
    if(unsigned(a)>=std::size(names))Fail("Invalid anchor.");return names[int(a)];
}
Aspect OutputAspect(double width,double height) {
    if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0)Fail("Invalid output dimensions.");
    const double ratio=width/height;
    if(std::abs(ratio-4./3)<.01)return Aspect::FourThree;
    if(std::abs(ratio-16./10)<.01)return Aspect::SixteenTen;
    if(std::abs(ratio-32./9)<.01)return Aspect::Superwide;
    if(ratio>=2.2&&ratio<=2.5)return Aspect::Wide;
    return Aspect::Standard;
}
Rect Canvas(Aspect a) {
    switch(a){case Aspect::Standard:return {0,0,1920,1080};case Aspect::Wide:return {0,0,2520,1080};
        case Aspect::Superwide:return {0,0,3840,1080};case Aspect::FourThree:return {0,0,1440,1080};
        case Aspect::SixteenTen:return {0,0,1728,1080};}Fail("Invalid aspect.");
}
Rect Canvas(const Preset& p) {
    if(!p.reference_width&&!p.reference_height)return Canvas(p.aspect);
    if(p.reference_width<64||p.reference_width>16384||p.reference_height<64||p.reference_height>16384)Fail("Invalid reference output size.");
    return {0,0,1080.*p.reference_width/p.reference_height,1080};
}
Rect PartySlot(Rect first,size_t slot) {if(slot>=PartySlots)Fail("Invalid party slot.");first.x+=slot*PartySlotStep;return first;}
Preset Base(Aspect a) {
    // Safety bases restore the original 16:9 composition inside each output
    // canvas. Edge anchoring belongs exclusively to custom layouts.
    Preset p;p.aspect=a;p.name="Original "+std::string(AspectName(a));
    return p;
}
void Validate(const Preset& p) {
    AspectName(p.aspect);
    if(p.reference_width||p.reference_height) {
        if(p.reference_width<64||p.reference_width>16384||p.reference_height<64||p.reference_height>16384)Fail("Invalid reference output size.");
        const double ratio=double(p.reference_width)/p.reference_height;
        if((p.aspect==Aspect::Standard&&std::abs(ratio-16./9)>.01)||
           (p.aspect==Aspect::Wide&&(ratio<2.2||ratio>2.5))||
           (p.aspect==Aspect::Superwide&&std::abs(ratio-32./9)>.01)||
           (p.aspect==Aspect::FourThree&&std::abs(ratio-4./3)>.01)||
           (p.aspect==Aspect::SixteenTen&&std::abs(ratio-16./10)>.01))Fail("Reference output does not match the aspect family.");
    }
    size_t codepoints=0;
    for(unsigned char c:p.name) {if(c<32||c==127)Fail("Preset name contains control characters.");if((c&0xc0)!=0x80)++codepoints;}
    if(codepoints<1||codepoints>64)Fail("Preset name must contain 1 to 64 characters.");
    for(size_t i=0;i<GroupCount;++i) {
        const auto& v=p.groups[i];const auto& s=Specs[i];AnchorName(v.anchor);
        if(unsigned(p.backgrounds[i])>unsigned(BackgroundFit::Extended)||
           (!IsBackgroundGroup(i)&&p.backgrounds[i]!=BackgroundFit::Automatic))Fail("Invalid background sizing override.");
        if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.scale)||
            std::abs(v.x)>3840||std::abs(v.y)>1080||v.scale<.5||v.scale>2)Fail("Layout value is outside its allowed range.");
        if(s.centered&&(v.x!=0||(v.anchor!=Anchor::Original&&v.anchor!=Anchor::Center)))Fail("This HUD group must remain horizontally centered.");
        if(s.world&&(v.x||v.y||v.scale!=1||(v.anchor!=Anchor::Original&&v.anchor!=Anchor::World)))Fail("World markers must stay attached to their targets.");
        if(!s.hide&&!v.visible)Fail("This interface group cannot be hidden.");
        if(s.locked&&(v.x||v.y||v.scale!=1||v.anchor!=Anchor::Original))Fail("This mixed HUD group must be split before it can be moved.");
    }
    if(p.elements.size()>4096)Fail("Too many UI element overrides.");
    std::set<std::pair<std::string,std::string>> seen;
    auto identity=[](std::string_view name,bool empty){return (empty||!name.empty())&&name.size()<=96&&
        std::ranges::all_of(name,[](unsigned char c){return c>=32&&c<127&&c!='/'&&c!='\\';});};
    for(const auto& e:p.elements) {
        if(!identity(e.resource,false)||!identity(e.object,true)||!seen.emplace(e.resource,e.object).second)
            Fail("Invalid or duplicate UI element identity.");
        const auto& v=e.value;
        if(v.anchor!=Anchor::Original||!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.scale)||
           std::abs(v.x)>3840||std::abs(v.y)>1080||v.scale<.5||v.scale>2)Fail("UI element transform is outside its allowed range.");
    }
}
Preset Parse(std::string_view text) {
    const auto doc=Json(text,1048576);const auto& o=Obj(doc);
    if(o.contains("backgrounds")) {
        if(o.contains("reference_output"))Keys(o,{"schema_version","layout_map","title_id","app_ver","name","aspect","units","groups","elements","backgrounds","reference_output"});
        else Keys(o,{"schema_version","layout_map","title_id","app_ver","name","aspect","units","groups","elements","backgrounds"});
    }
    else if(o.contains("reference_output"))Keys(o,{"schema_version","layout_map","title_id","app_ver","name","aspect","units","groups","elements","reference_output"});
    else if(o.contains("elements"))Keys(o,{"schema_version","layout_map","title_id","app_ver","name","aspect","units","groups","elements"});
    else Keys(o,{"schema_version","layout_map","title_id","app_ver","name","aspect","units","groups"});
    const auto schema=o.at("schema_version").is_int64()?o.at("schema_version").as_int64():0;
    if((schema<1||schema>4)||(schema>=2)!=o.contains("elements")||(schema==4)!=o.contains("backgrounds")||
        (schema!=4&&(schema==3)!=o.contains("reference_output"))||Str(o.at("layout_map"))!=MapId||
        Str(o.at("title_id"))!="CUSA03219"||Str(o.at("app_ver"))!="01.00"||Str(o.at("units"))!="logical_1080_anchor_relative")
        Fail("Preset version or game revision is incompatible.");
    auto p=Base(ParseAspect(Str(o.at("aspect"))));p.name=Str(o.at("name"));
    if(o.contains("reference_output")){const auto& output=Obj(o.at("reference_output"));Keys(output,{"width","height"});
        const double width=Num(output.at("width")),height=Num(output.at("height"));
        if(!std::isfinite(width)||!std::isfinite(height)||width<64||width>16384||height<64||height>16384||std::floor(width)!=width||std::floor(height)!=height)Fail("Invalid reference output size.");
        p.reference_width=int(width);p.reference_height=int(height);}
    const auto& rows=Arr(o.at("groups"));if(rows.size()>GroupCount)Fail("Too many HUD groups.");
    std::array<bool,GroupCount> seen{};
    for(const auto& row:rows) {
        const auto& g=Obj(row);Keys(g,{"id","anchor","offset_x","offset_y","scale","visible"});
        const auto id=Group(Str(g.at("id")));if(seen[id])Fail("Duplicate HUD group.");seen[id]=true;
        if(!g.at("visible").is_bool())Fail("Visibility must be a boolean.");
        p.groups[id]={ParseAnchor(Str(g.at("anchor"))),Num(g.at("offset_x")),Num(g.at("offset_y")),Num(g.at("scale")),g.at("visible").as_bool()};
    }
    if(schema>=2)for(const auto& entry:Arr(o.at("elements"))) {
        const auto& e=Obj(entry);Keys(e,{"resource","object","offset_x","offset_y","scale","visible"});
        if(!e.at("visible").is_bool())Fail("Element visibility must be a boolean.");
        p.elements.push_back({Str(e.at("resource"),96),Str(e.at("object"),96),
            {Anchor::Original,Num(e.at("offset_x")),Num(e.at("offset_y")),Num(e.at("scale")),e.at("visible").as_bool()}});
    }
    if(schema==4)for(const auto& entry:Obj(o.at("backgrounds"))) {
        const auto group=Group(entry.key());const auto value=Str(entry.value());
        if(!IsBackgroundGroup(group)||(value!="original"&&value!="extended"))Fail("Invalid background sizing override.");
        p.backgrounds[group]=value=="original"?BackgroundFit::Original:BackgroundFit::Extended;
    }
    Validate(p);
    // Migrate only untouched historical built-ins. Never discard custom edits,
    // visibility, object overrides or a user-named layout.
    if(p.aspect!=Aspect::Standard&&p.name=="Edge-anchored "+std::string(AspectName(p.aspect))&&p.elements.empty()&&schema!=4) {
        bool legacy=true;for(size_t n=0;n<GroupCount;++n)legacy&=p.groups[n]==Setting{Specs[n].preferred,0,0,1,true};
        if(legacy){const auto w=p.reference_width,h=p.reference_height;p=Base(p.aspect);p.reference_width=w;p.reference_height=h;}
    }
    return p;
}
void CheckJson(std::string_view text,size_t limit){(void)Json(text,limit);}
std::string Serialize(const Preset& p) {
    Validate(p);j::array groups;
    for(size_t n=0;n<GroupCount;++n) {
        const auto& s=p.groups[n];groups.emplace_back(j::object{{"id",Specs[n].id},{"anchor",AnchorName(s.anchor)},
            {"offset_x",s.x},{"offset_y",s.y},{"scale",s.scale},{"visible",s.visible}});
    }
    const bool backgrounds=std::ranges::any_of(p.backgrounds,[](auto fit){return fit!=BackgroundFit::Automatic;});
    j::object doc{{"schema_version",backgrounds?4:p.reference_width?3:p.elements.empty()?1:2},{"layout_map",MapId},{"title_id","CUSA03219"},{"app_ver","01.00"},
        {"name",p.name},{"aspect",AspectName(p.aspect)},{"units","logical_1080_anchor_relative"},{"groups",std::move(groups)}};
    if(!p.elements.empty()||p.reference_width||backgrounds) {
        j::array rows;for(const auto& e:p.elements)rows.emplace_back(j::object{{"resource",e.resource},{"object",e.object},
            {"offset_x",e.value.x},{"offset_y",e.value.y},{"scale",e.value.scale},{"visible",e.value.visible}});
        doc["elements"]=std::move(rows);
    }
    if(p.reference_width)doc["reference_output"]=j::object{{"width",p.reference_width},{"height",p.reference_height}};
    if(backgrounds) {
        j::object modes;for(size_t n=0;n<GroupCount;++n)if(p.backgrounds[n]!=BackgroundFit::Automatic)
            modes[Specs[n].id]=p.backgrounds[n]==BackgroundFit::Original?"original":"extended";
        doc["backgrounds"]=std::move(modes);
    }
    return j::serialize(doc)+"\n";
}
std::string SerializeReadable(const Preset& p) {
    const auto doc=j::parse(Serialize(p));std::string out;out.reserve(8192);
    auto write=[&](auto&& self,const j::value& value,size_t depth)->void {
        auto indent=[&](size_t level){out.append(level*2,' ');};
        if(value.is_object()) {
            const auto& object=value.as_object();out+="{";size_t n=0;
            for(const auto& member:object){out+='\n';indent(depth+1);out+=j::serialize(j::value(member.key()));out+=": ";
                self(self,member.value(),depth+1);if(++n<object.size())out+=',';}
            if(!object.empty()){out+='\n';indent(depth);}out+='}';
        }else if(value.is_array()) {
            const auto& array=value.as_array();out+='[';
            for(size_t n=0;n<array.size();++n){out+='\n';indent(depth+1);self(self,array[n],depth+1);if(n+1<array.size())out+=',';}
            if(!array.empty()){out+='\n';indent(depth);}out+=']';
        }else if(value.is_double()) {
            // Shortest round-trippable decimal, without needless exponent
            // notation for ordinary coordinates and scale values.
            char number[64];const auto result=std::to_chars(number,number+sizeof number,value.as_double(),std::chars_format::general);
            if(result.ec!=std::errc{})Fail("Cannot format a preset number.");out.append(number,result.ptr);
        }else out+=j::serialize(value);
    };
    write(write,doc,0);out+='\n';if(out.size()>1048576)Fail("Readable preset exceeds the file size limit.");return out;
}
Rect Place(Rect r,const Setting& s,double width,double height) {
    if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||width>32768||height>32768)
        Fail("Invalid output dimensions.");
    for(double value:{r.x,r.y,r.w,r.h,s.x,s.y,s.scale})if(!std::isfinite(value))Fail("Non-finite layout coordinate.");
    if(r.w<0||r.h<0||s.scale<.5||s.scale>2)Fail("Invalid layout size.");
    const double unit=std::min(width/1920.,height/1080.);
    const double dx=(width-1920*unit)/2,dy=(height-1080*unit)/2;
    double x=dx+r.x*unit,y=dy+r.y*unit;
    switch(s.anchor) {
        case Anchor::TopLeft:case Anchor::BottomLeft:case Anchor::Left:x=r.x*unit;break;
        case Anchor::TopRight:case Anchor::BottomRight:case Anchor::Right:x=width-(1920-r.x)*unit;break;
        default:break;
    }
    if(s.anchor==Anchor::TopLeft||s.anchor==Anchor::TopRight)y=r.y*unit;
    if(s.anchor==Anchor::BottomLeft||s.anchor==Anchor::BottomRight)y=height-(1080-r.y)*unit;
    // Uniform scaling holds the selected edge, or the center for centered groups.
    const double dw=r.w*unit*(s.scale-1),dh=r.h*unit*(s.scale-1);
    if(s.anchor==Anchor::Right||s.anchor==Anchor::TopRight||s.anchor==Anchor::BottomRight)x-=dw;
    else if(s.anchor==Anchor::Center||s.anchor==Anchor::Original)x-=dw/2;
    if(s.anchor==Anchor::BottomLeft||s.anchor==Anchor::BottomRight)y-=dh;
    else if(s.anchor==Anchor::Center||s.anchor==Anchor::Original)y-=dh/2;
    return {x+s.x*unit,y+s.y*unit,r.w*unit*s.scale,r.h*unit*s.scale};
}
std::string Read(const std::filesystem::path& path,size_t limit) {
    std::ifstream f(path,std::ios::binary);if(!f)Fail("Cannot open the file.");
    f.seekg(0,std::ios::end);const auto n=f.tellg();if(n<=0||n>std::streamoff(limit))Fail("File is empty or exceeds the size limit.");
    f.seekg(0);std::string text(size_t(n),'\0');if(!f.read(text.data(),n))Fail("Cannot read the complete file.");return text;
}
void Save(const std::filesystem::path& path,const Preset& p) {
    const auto data=SerializeReadable(p);GUID id{};if(FAILED(CoCreateGuid(&id)))Fail("Cannot create a unique temporary file.");
    wchar_t guid[40]{};StringFromGUID2(id,guid,40);auto temp=path;temp+=std::wstring(L".")+guid+L".tmp";
    HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)Fail("Cannot create the preset file.");
    DWORD written=0;const bool ok=WriteFile(f,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(f);
    CloseHandle(f);
    if(!ok||!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());Fail("Preset export failed; the previous file was preserved.");
    }
}
Catalog ParseCatalog(std::string_view text) {
    const auto doc=Json(text,524288);const auto& o=Obj(doc);
    if(Str(o.at("profile_id"))!=MapId)Fail("Incompatible HUD map.");Catalog c;
    std::set<std::string> ids;
    const auto& rows=Arr(o.at("objects"));if(rows.size()>1024)Fail("Too many mapped objects.");
    for(const auto& row:rows) {
        const auto& v=Obj(row);Object item;item.id=Str(v.at("id"));item.resource=Str(v.at("resource"));
        if(!ids.insert(item.id).second)Fail("Duplicate mapped object.");
        item.parent=v.at("parent").is_null()?"":Str(v.at("parent"));item.type=Str(v.at("type"));
        item.texture=v.at("texture_name").is_null()?"":Str(v.at("texture_name"));
        item.part=v.at("texture_part").is_null()?"":Str(v.at("texture_part"));
        const auto& r=Arr(v.at("authored_rect"));if(r.size()!=4)Fail("Invalid mapped bounds.");
        item.rect={Num(r[0]),Num(r[1]),Num(r[2]),Num(r[3])};
        for(const auto& n:r)if(!std::isfinite(Num(n))||std::abs(Num(n))>32768)Fail("Invalid mapped coordinate.");
        if(item.rect.w<0||item.rect.h<0)Fail("Negative mapped size.");c.objects.push_back(std::move(item));
    }
    for(const auto& item:c.objects) {
        std::set<std::string> visited;auto parent=item.parent;
        while(!parent.empty()) {
            if(!visited.insert(parent).second)Fail("Cyclic HUD hierarchy.");
            auto found=std::find_if(c.objects.begin(),c.objects.end(),[&](auto& x){return x.id==parent;});
            if(found==c.objects.end())Fail("Missing HUD parent.");parent=found->parent;
        }
    }
    std::array<bool,GroupCount> seen{};
    for(const auto& row:Arr(o.at("groups"))) {
        const auto& g=Obj(row);size_t n=Group(Str(g.at("id")));if(seen[n])Fail("Duplicate mapped group.");seen[n]=true;
        for(const auto& r:Arr(g.at("resources")))c.resources[n].push_back(Str(r));
        for(const auto& r:Arr(g.at("root_objects"))) {auto id=Str(r);if(!ids.contains(id))Fail("Missing group root.");c.roots[n].push_back(id);}
        if(const auto* value=g.if_contains("reference_bounds")) {
            const auto& r=Arr(*value);if(r.size()!=4)Fail("Invalid group reference bounds.");
            for(const auto& v:r)if(!std::isfinite(Num(v))||std::abs(Num(v))>32768)Fail("Invalid group reference coordinate.");
            if(Num(r[2])<=0||Num(r[3])<=0)Fail("Invalid group reference size.");
            c.reference_bounds[n]=Rect{Num(r[0]),Num(r[1]),Num(r[2]),Num(r[3])};
        }
    }
    if(std::find(seen.begin(),seen.end(),false)!=seen.end())Fail("Incomplete HUD map.");return c;
}
bool Catalog::Descends(const Object& obj,std::string_view root) const {
    const Object* p=&obj;
    for(size_t n=0;n<=objects.size();++n) {
        if(p->id==root)return true;if(p->parent.empty())return false;
        auto it=std::find_if(objects.begin(),objects.end(),[&](const auto& v){return v.id==p->parent;});
        if(it==objects.end())return false;p=&*it;
    }
    return false;
}
std::vector<const Object*> Catalog::Members(size_t group) const {
    std::vector<const Object*> result;
    for(const auto& o:objects) {
        bool match=false;
        if(!roots.at(group).empty())for(const auto& root:roots[group])match|=Descends(o,root);
        else match=std::find(resources.at(group).begin(),resources.at(group).end(),o.resource)!=resources.at(group).end();
        if(match&&o.type!="Null"&&o.rect.w>0&&o.rect.h>0)result.push_back(&o);
    }
    return result;
}
Rect Catalog::Bounds(size_t group) const {
    if(reference_bounds.at(group))return *reference_bounds[group];
    auto rows=Members(group);if(rows.empty())return {};
    double x=rows[0]->rect.x,y=rows[0]->rect.y,x1=x+rows[0]->rect.w,y1=y+rows[0]->rect.h;
    for(auto* o:rows) {x=std::min(x,o->rect.x);y=std::min(y,o->rect.y);x1=std::max(x1,o->rect.x+o->rect.w);y1=std::max(y1,o->rect.y+o->rect.h);}
    if(group==0)x1+=PartySlotStep*(PartySlots-1);
    return {x,y,x1-x,y1-y};
}
}
