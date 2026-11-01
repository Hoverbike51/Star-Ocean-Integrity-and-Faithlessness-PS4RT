// Developer-only CPU rendering of real Dear ImGui draw lists for visual review.
// This is not linked into, installed with, or used by the player runtime.
#pragma once
#include <fstream>

inline void CaptureMenuDrawData(const std::filesystem::path& destination) {
    const auto* data=ImGui::GetDrawData();
    const int width=int(data->DisplaySize.x),height=int(data->DisplaySize.y);
    std::vector<unsigned char> pixels(size_t(width)*height*4);
    for(size_t i=0;i<pixels.size();i+=4) {pixels[i]=42;pixels[i+1]=29;pixels[i+2]=15;pixels[i+3]=255;}
    auto edge=[](ImVec2 a,ImVec2 b,ImVec2 c){return (c.x-a.x)*(b.y-a.y)-(c.y-a.y)*(b.x-a.x);};
    for(const auto* list:data->CmdLists) for(const auto& command:list->CmdBuffer) {
        if(command.UserCallback) continue;
        const auto* texture=command.TexRef._TexData;
        if(!texture || !texture->Pixels) continue;
        for(unsigned i=0;i+2<command.ElemCount;i+=3) {
            const auto& a=list->VtxBuffer[list->IdxBuffer[command.IdxOffset+i]+command.VtxOffset];
            const auto& b=list->VtxBuffer[list->IdxBuffer[command.IdxOffset+i+1]+command.VtxOffset];
            const auto& c=list->VtxBuffer[list->IdxBuffer[command.IdxOffset+i+2]+command.VtxOffset];
            const float area=edge(a.pos,b.pos,c.pos);if(std::abs(area)<1e-6f) continue;
            const int left=std::max({0,int(std::floor(std::min({a.pos.x,b.pos.x,c.pos.x}))),int(command.ClipRect.x)});
            const int right=std::min({width,int(std::ceil(std::max({a.pos.x,b.pos.x,c.pos.x}))),int(command.ClipRect.z)});
            const int top=std::max({0,int(std::floor(std::min({a.pos.y,b.pos.y,c.pos.y}))),int(command.ClipRect.y)});
            const int bottom=std::min({height,int(std::ceil(std::max({a.pos.y,b.pos.y,c.pos.y}))),int(command.ClipRect.w)});
            for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) {
                const ImVec2 p(float(x)+.5f,float(y)+.5f);
                const float wa=edge(b.pos,c.pos,p)/area,wb=edge(c.pos,a.pos,p)/area,wc=1-wa-wb;
                if(wa<0 || wb<0 || wc<0) continue;
                const int u=std::clamp(int((a.uv.x*wa+b.uv.x*wb+c.uv.x*wc)*texture->Width),0,texture->Width-1);
                const int v=std::clamp(int((a.uv.y*wa+b.uv.y*wb+c.uv.y*wc)*texture->Height),0,texture->Height-1);
                const auto* texel=texture->Pixels+(v*texture->Width+u)*texture->BytesPerPixel;
                float color[4]{};
                for(int channel=0;channel<4;++channel) color[channel]=float((a.col>>(channel*8))&255)*wa+float((b.col>>(channel*8))&255)*wb+float((c.col>>(channel*8))&255)*wc;
                const float alpha=color[3]*(texture->BytesPerPixel==1?texel[0]:texel[3])/(255.0f*255.0f);
                auto* out=pixels.data()+(size_t(y)*width+x)*4;
                for(int channel=0;channel<3;++channel) {
                    const float value=color[channel]*(texture->BytesPerPixel==1?255:texel[channel])/255.0f;
                    out[2-channel]=static_cast<unsigned char>(std::clamp(value*alpha+out[2-channel]*(1-alpha),0.0f,255.0f));
                }
            }
        }
    }
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+DWORD(pixels.size());
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=width;info.biHeight=-height;info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    std::ofstream file(destination,std::ios::binary);assert(file);
    file.write(reinterpret_cast<const char*>(&header),sizeof(header));file.write(reinterpret_cast<const char*>(&info),sizeof(info));
    file.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()));assert(file.good());
}
