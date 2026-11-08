// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <ostream>

namespace Ps4RtPresentation {
enum class Stage { FirstGui, FirstMovie, FirstDepthDraw, Tonemap, BeforeDisplay, ComposedBackground, DisplayDraw, ProjectionBuffer };
inline const char* Name(Stage stage) {
    switch(stage) {
    case Stage::FirstGui:return "first_native_gui";
    case Stage::FirstMovie:return "first_movie_ui";
    case Stage::FirstDepthDraw:return "first_fullsize_depth_draw";
    case Stage::Tonemap:return "tonemap";
    case Stage::BeforeDisplay:return "before_display";
    case Stage::ComposedBackground:return "composed_background";
    case Stage::DisplayDraw:return "display_draw";
    case Stage::ProjectionBuffer:return "vertex_projection_buffer";
    }
    return "unknown";
}
enum Flag : std::uint32_t {
    FrameCamera=1, CurrentCamera=2, CameraHistory=4, SceneTarget=8,
    UiPhase=16, FrameDone=32, DisplayTarget=64, NativeViewport=128,
    Depth=256, WideBackground=512, SceneProxy=1024, ProjectionValid=2048, SpatialDisplay=4096
};
struct Sample {
    Stage stage{};
    std::uint64_t vertex=0,pixel=0;
    std::uint32_t flags=0,width=0,height=0,output_width=0,output_height=0;
    std::int32_t format=0,aspect_milli=0;
    std::uint32_t constant_bytes=0;
    std::int32_t projection_aspect_milli=0;
    bool operator==(const Sample&) const=default;
};
// A few phase-boundary samples per frame. No memory dumps, GPU readbacks,
// file writes or console output in the render loop. Stable integer keys prevent
// tiny projection differences from filling the bounded table.
class Collector {
public:
    static constexpr std::size_t Capacity=192;
    void Reset() {
        std::scoped_lock lock{mutex};count=0;overflow=0;events=0;reported=false;
    }
    static std::int32_t Aspect(float value) {
        return std::isfinite(value)&&value>0&&value<32 ? std::lround(value*1000) : 0;
    }
    void Note(const Sample& sample,std::uint64_t frame) {
        std::scoped_lock lock{mutex};++events;
        for(std::size_t i=0;i<count;++i) if(entries[i].sample==sample) {
            ++entries[i].hits;entries[i].last=frame;return;
        }
        if(count==Capacity) {++overflow;return;}
        entries[count++]={sample,1,frame,frame};
    }
    void Write(std::ostream& out) {
        std::scoped_lock lock{mutex};WriteLocked(out);
    }
    void Report(const std::filesystem::path& path) {
        std::scoped_lock lock{mutex};
        if(reported||!events||path.empty())return;
        // A failed export can be retried by the normal shutdown path.
        try {
            auto temporary=path;temporary+=L".tmp";
            std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
            if(!file)return;
            WriteLocked(file);file.close();
            if(!file)return;
            std::error_code error;
            std::filesystem::copy_file(temporary,path,std::filesystem::copy_options::overwrite_existing,error);
            if(error)return;
            std::filesystem::remove(temporary,error);reported=true;
            std::printf("Presentation diagnostics: saved presentation-last-census.json\n");
        } catch(...) {std::printf("Presentation diagnostics: could not save phase census\n");}
    }
private:
    void WriteLocked(std::ostream& out) {
        out<<"{\n  \"schema\": 1,\n  \"capacity\": "<<Capacity<<",\n  \"overflow_events\": "<<overflow
           <<",\n  \"events\": "<<events<<",\n  \"aspect_units\": \"ratio times 1000; zero means unavailable\","
             "\n  \"flags\": {\"frame_camera\":1,\"current_camera\":2,\"camera_history\":4,\"scene_target\":8,"
             "\"ui_phase\":16,\"frame_done\":32,\"display_target\":64,\"native_viewport\":128,"
             "\"depth\":256,\"wide_background\":512,\"scene_proxy\":1024,\"projection_valid\":2048,"
             "\"spatial_display\":4096},\n  \"samples\": [";
        for(std::size_t i=0;i<count;++i) {
            const auto& e=entries[i];const auto& s=e.sample;
            out<<(i?",":"")<<"\n    {\"stage\":\""<<Name(s.stage)<<"\",\"hits\":"<<e.hits
               <<",\"first_frame\":"<<e.first<<",\"last_frame\":"<<e.last
               <<",\"vertex_shader\":"<<s.vertex<<",\"pixel_shader\":"<<s.pixel
               <<",\"flags\":"<<s.flags<<",\"target\":["<<s.width<<','<<s.height
               <<"],\"output\":["<<s.output_width<<','<<s.output_height<<"],\"format\":"<<s.format
               <<",\"frame_aspect_milli\":"<<s.aspect_milli
               <<",\"constant_bytes\":"<<s.constant_bytes
               <<",\"projection_aspect_milli\":"<<s.projection_aspect_milli<<'}';
        }
        out<<"\n  ]\n}\n";
    }
    struct Entry {Sample sample;std::uint64_t hits=0,first=0,last=0;};
    std::array<Entry,Capacity> entries{};
    std::size_t count=0;
    std::uint64_t overflow=0,events=0;
    bool reported=false;
    std::mutex mutex;
};
inline Collector capture;
inline std::filesystem::path report_path;
}
