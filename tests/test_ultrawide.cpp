// SPDX-License-Identifier: GPL-2.0-or-later
// Owned image fixtures; optional inspection of a prepared image is read-only.
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include <sstream>
#include <limits>
#include <chrono>
#include "ps4rt_presentation_trace.h"
#include "ps4rt_ui_layout_trace.h"
#include "ps4rt_ultrawide.h"
#include "ps4rt_scene_preset.h"
#include "video_core/renderer_vulkan/ui_composition.h"
#include "video_core/renderer_vulkan/scene_resolution.h"
int main(int argc,char** argv) {
    Vulkan::SceneResolution::InputReadiness readiness;
    for(unsigned i=1;i<Vulkan::SceneResolution::InputReadiness::Limit;++i) {
        assert(!readiness.Observe(false));assert(!readiness.Exhausted());
    }
    assert(!readiness.Observe(false) && readiness.Exhausted());
    assert(readiness.Observe(true) && !readiness.Exhausted() && readiness.misses==0);
    assert(!readiness.Observe(false) && readiness.misses==1);
    readiness.Reset();assert(!readiness.Exhausted() && readiness.misses==0);
    using namespace Ps4RtUltrawide;
    std::vector<unsigned char> image(AspectAddress+4);
    std::memcpy(image.data()+0x4b0,Constructor,sizeof Constructor);
    std::memcpy(image.data()+0x1eef4,Consumer,sizeof Consumer);
    const std::uint32_t original=0x3fe38e39;
    std::memcpy(image.data()+AspectAddress,&original,4);
    if(argc==2) {
        std::ifstream file(argv[1],std::ios::binary);
        assert(file);
        std::vector<unsigned char> boot{std::istreambuf_iterator<char>(file),{}};
        assert(boot.size()>16 && !std::memcmp(boot.data(),"PS4RTB",6));
        std::uint64_t size=0;std::memcpy(&size,boot.data()+8,8);
        assert(size<=boot.size() && size>AspectAddress+4);
        image.assign(boot.end()-size,boot.end());
        assert(Matches(image,"CUSA03219","01.00"));
        std::puts("PASS: actual prepared executable matches the guarded aspect profile (read-only)");
    }
    const auto pristine=image;
    assert(!Matches(std::span(image).first(512),"CUSA03219","01.00"));
    assert(!Matches(image,"CUSA99999","01.00"));
    assert(!Matches(image,"CUSA03219","01.01"));
    assert(!Patch(image,"CUSA03219","01.00",1) && image==pristine);
    image[0x1eef4]^=1;
    assert(!Patch(image,"CUSA03219","01.00",Ps4RtOutput::Find(3440,1440)));
    image=pristine;
    for(const int output:{Ps4RtOutput::Find(3440,1440),Ps4RtOutput::Find(5120,1440),
                          Ps4RtOutput::Find(1600,686),Ps4RtOutput::Find(1600,450)}) {
        assert(output>=0 && Wide(output));
        image=pristine;
        assert(Patch(image,"CUSA03219","01.00",output));
        assert(!Patch(image,"CUSA03219","01.00",output)); // cannot compound a patch
        assert(std::equal(image.begin(),image.begin()+AspectAddress,pristine.begin()));
        assert(std::equal(image.begin()+AspectAddress+4,image.end(),pristine.begin()+AspectAddress+4));
        float ratio=0;std::memcpy(&ratio,image.data()+AspectAddress,4);
        assert(std::abs(ratio-Ratio(output))<1e-6);
        session_output=output;
        const auto content=Content(output);
        const auto hud=Hud(content.width,content.height);
        assert(content.width==Ps4RtOutput::Widths[output]);
        assert(hud.scale_x==hud.scale_y && std::abs(hud.offset_y)<.001);
        assert(std::abs(hud.offset_x*2+1920*hud.scale_x-content.width)<.001);
        const auto input=Ps4RtScene::Input(-1,content.width,content.height,true);
        assert(input.height==1080 && std::abs(float(input.width)/1080-ratio)<.002);
        assert(CameraMatches(1.7f/ratio,1.7f,output));
        assert(!CameraMatches(1.7f/(16.f/9),1.7f,output));
        // Same vertical FOV, more visible horizontal content; no vertical crop.
        const float side_point=(16.f/9+ratio)*.5f/1.7f;
        assert(side_point*1.7f/(16.f/9)>1.f && side_point*1.7f/ratio<1.f);
        const auto allowed=SessionModes(~0ull);
        assert(allowed&(1ull<<output));assert(!(allowed&(1ull<<1)));
        for(int i=0;i<Ps4RtOutput::Count;++i) if(allowed&(1ull<<i)) assert(std::abs(Ratio(i)-ratio)<.001f);
    }
    // Narrower formats change camera projection and use the entire 3D target.
    // HUD/movie fit remains uniform and centered, with no wide-preset substitution.
    for(const int output:{Ps4RtOutput::Find(1440,900),Ps4RtOutput::Find(1920,1200),
                          Ps4RtOutput::Find(1024,768),Ps4RtOutput::Find(1600,1200)}) {
        assert(output>=0 && Adjusted(output) && !Wide(output));
        image=pristine;assert(Patch(image,"CUSA03219","01.00",output));
        session_output=output;assert(Enabled() && !WideEnabled());
        const auto content=Content(output);const auto fit=Hud(content.width,content.height);
        assert(content.width==Ps4RtOutput::Widths[output] && content.height==Ps4RtOutput::Heights[output]);
        assert(fit.scale_x==fit.scale_y && std::abs(fit.offset_x)<.001f && fit.offset_y>0);
        assert(std::abs(fit.offset_y*2+1080*fit.scale_y-content.height)<.001f);
        assert(CameraMatches(1.7f/Ratio(output),1.7f,output));
        assert(!CameraMatches(1.7f/(16.f/9),1.7f,output));
        const auto modes=SessionModes(~0ull);
        assert(modes&(1ull<<output));assert(!(modes&(1ull<<1)));
        assert(Vulkan::UiComposition::WideDisplayScene(true,Ratio(output),Ratio(output)));
        assert(!Vulkan::UiComposition::WideDisplayScene(false,Ratio(output),Ratio(output)));
        assert(Vulkan::UiComposition::PerspectiveMenuBackground(true,true,true,Ratio(output),Ratio(output)));
        assert(!Vulkan::UiComposition::PerspectiveMenuBackground(true,true,true,16.f/9,Ratio(output)));
    }
    session_output=1;
    assert(!(SessionModes(~0ull)&(1ull<<Ps4RtOutput::Find(1600,1200))));
    assert(!Enabled() && !(SessionModes(~0ull)&(1ull<<Ps4RtOutput::Find(3440,1440))));
    assert(Content(Ps4RtOutput::Find(3440,1440)).width==2560);
    session_output=-1;
    const auto normal=Hud(1920,1080);
    assert(normal.scale_x==1 && normal.offset_x==0 && normal.offset_y==0);
    // Regression: the camera already reported 32:9, but an early UI snapshot
    // or an invalidated proxy wrongly sent the 3D background through 16:9 fit.
    using namespace Vulkan::UiComposition;
    assert(!NativeMenuBackground(true,false)); // first camera before history commit
    assert(!NativeMenuBackground(false,true)); // tonemap remains authoritative
    assert(NativeMenuBackground(false,false)); // actual title/movie/menu frame
    Vulkan::SceneResolution::Coherence pixels;
    pixels.ProxyWrite();pixels.NativeWrite();assert(!pixels.valid);
    assert(WideSceneBackground(true,true)); // native pixels still contain a wide projection
    assert(WideSceneBackground(false,true)); // transition: tonemap present, camera history absent
    assert(!WideSceneBackground(true,false)); // no matching scene target, never stretch movies
    assert(!WideSceneBackground(false,false));
    for(const float aspect:{21.f/9,32.f/9}) {
        assert(PerspectiveMenuBackground(true,true,true,aspect,aspect));
        assert(!PerspectiveMenuBackground(false,true,true,aspect,aspect)); // video/unknown shader
        assert(!PerspectiveMenuBackground(true,false,true,aspect,aspect)); // already includes display UI
        assert(!PerspectiveMenuBackground(true,true,false,aspect,aspect)); // shadow/other viewport
        assert(!PerspectiveMenuBackground(true,true,true,0,aspect)); // no current camera
        assert(!PerspectiveMenuBackground(true,true,true,16.f/9,aspect)); // never stretch a narrow projection
        assert(!PerspectiveMenuBackground(true,true,true,std::numeric_limits<float>::quiet_NaN(),aspect));
    }
    assert(!PerspectiveMenuBackground(true,true,true,16.f/9,16.f/9));
    // Actual census: 484 transition frames had a 32:9 camera but no usual
    // tonemap target. Do not pillarbox these frames or sample the old UI image.
    for(const float aspect:{21.f/9,32.f/9}) {
        assert(WideDisplayScene(true,aspect,aspect));
        assert(!WideDisplayScene(false,aspect,aspect)); // stale history is insufficient
        assert(!WideDisplayScene(true,16.f/9,aspect));
        assert(!WideDisplayScene(true,0,aspect));
        assert(!WideDisplayScene(true,std::numeric_limits<float>::quiet_NaN(),aspect));
    }
    assert(!WideDisplayScene(true,16.f/9,16.f/9));
    assert(!WideDisplayScene(true,-4.f/3,-4.f/3));
    assert(!WideDisplayScene(true,0,0));
    assert(!PerspectiveMenuBackground(true,true,true,-1,-1));
    const auto transition=RouteDisplay(false,true,0,0,3200,900);
    assert(transition.redirect&&transition.width==3200&&transition.height==900&&!transition.sample_composed);
    const auto reconstructed=RouteDisplay(true,true,3440,1440,3200,900);
    assert(reconstructed.redirect&&reconstructed.width==3440&&reconstructed.height==1440&&reconstructed.sample_composed);
    const auto original_movie=RouteDisplay(false,false,3200,900,3200,900);
    assert(!original_movie.redirect&&!original_movie.sample_composed);
    const auto full=Ps4RtUltrawide::Background(2560,720,WideSceneBackground(true,true));
    assert(full.left==0 && full.right==2560 && full.top==0 && full.bottom==720);
    const auto movie=Ps4RtUltrawide::Background(2560,720,false);
    assert(movie.left==640 && movie.right==1920 && movie.top==0 && movie.bottom==720);
    const auto map=Hud(2560,720);
    assert(map.scale_x==map.scale_y && map.offset_x==640); // circles must remain circular
    Ps4RtUiLayout::Collector trace;
    Ps4RtUiLayout::Draw draw{};
    draw.vertex=123;draw.width=2560;draw.height=720;draw.display=true;
    draw.combined={1600,0,1920,320};draw.transform={2.f/3,2.f/3,640,0};
    assert(trace.Note(draw) && trace.Note(draw));
    std::ostringstream json;trace.Write(json);
    assert(json.str().find("\"hits\":2")!=std::string::npos);
    assert(json.str().find("\"combined_scissor\":[1600,0,1920,320]")!=std::string::npos);
    draw.viewport[0]=std::numeric_limits<float>::quiet_NaN();assert(!trace.Note(draw));
    trace.Reset();draw.viewport[0]=0;
    for(unsigned i=0;i<trace.Capacity;++i) {draw.vertex=i;assert(trace.Note(draw));}
    draw.vertex=trace.Capacity;assert(!trace.Note(draw));
    std::ostringstream bounded;trace.Write(bounded);
    assert(bounded.str().find("\"overflow_draws\":1")!=std::string::npos);
    trace.Reset();std::ostringstream empty;trace.Write(empty);
    assert(empty.str().find("\"draws\":[]")!=std::string::npos);
    // Presentation evidence must survive a truncated console and remain
    // bounded even across changing shaders, resolutions and camera ratios.
    Ps4RtPresentation::Collector phases;
    Ps4RtPresentation::Sample phase{};
    phase.stage=Ps4RtPresentation::Stage::FirstGui;
    phase.aspect_milli=phases.Aspect(32.f/9);assert(phase.aspect_milli==3556);
    assert(phases.Aspect(std::numeric_limits<float>::infinity())==0);
    assert(phases.Aspect(std::numeric_limits<float>::quiet_NaN())==0);
    assert(phases.Aspect(-1)==0);
    phase.flags=Ps4RtPresentation::FrameCamera|Ps4RtPresentation::NativeViewport;
    phases.Note(phase,1);phases.Note(phase,420);
    std::ostringstream evidence;phases.Write(evidence);
    assert(evidence.str().find("\"hits\":2,\"first_frame\":1,\"last_frame\":420")!=std::string::npos);
    assert(evidence.str().find("\"frame_aspect_milli\":3556")!=std::string::npos);
    for(unsigned n=1;n<phases.Capacity;++n) {phase.vertex=n;phases.Note(phase,n);}
    phase.vertex=phases.Capacity;phases.Note(phase,1000);
    std::ostringstream saturated;phases.Write(saturated);
    assert(saturated.str().find("\"overflow_events\": 1")!=std::string::npos);
    const auto census=std::filesystem::temp_directory_path()/
        ("ps4rt-presentation-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".json");
    phases.Report(census);assert(std::filesystem::is_regular_file(census));
    {std::ifstream saved(census);std::string content{std::istreambuf_iterator<char>(saved),{}};assert(content==saturated.str());}
    phases.Reset();phase.vertex=42;phases.Note(phase,2);phases.Report(census);
    {std::ifstream saved(census);std::string content{std::istreambuf_iterator<char>(saved),{}};assert(content.find("\"events\": 1")!=std::string::npos);}
    assert(std::filesystem::remove(census));
    phases.Reset();phases.Report(census);assert(!std::filesystem::exists(census));
    std::puts("PASS: executable guards, Hor+ geometry, original allocation size, HUD fit and projection session limits");
}
