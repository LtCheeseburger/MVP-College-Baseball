#include "game/walkup.h"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    auto file = std::filesystem::path(argv[1]) / "tone.wav";
    std::ofstream out(file, std::ios::binary);
    auto word = [&](uint32_t n, int bytes) { for (int i=0;i<bytes;i++) out.put(char(n >> (i*8))); };
    out.write("RIFF",4); word(36+44100*2,4); out.write("WAVEfmt ",8); word(16,4);
    word(1,2);word(1,2);word(44100,4);word(88200,4);word(2,2);word(16,2);
    out.write("data",4);word(88200,4);
    for(int i=0;i<44100;i++) word(uint16_t(int16_t(12000*std::sin(i*440.*6.283185307/44100))),2);
    out.close();
    std::string error;
    auto clip=walkup::Decode(file,0.1,0.3,error);
    if(clip.pcm.size()<28000 || clip.pcm.size()>30000) throw std::runtime_error("trim/resample length: "+error);
    double energy=0;
    for(size_t i=0;i<clip.pcm.size();i+=2) {
        if(!std::isfinite(clip.pcm[i]) || clip.pcm[i]!=clip.pcm[i+1]) throw std::runtime_error("mono stereo conversion");
        energy+=clip.pcm[i]*clip.pcm[i];
    }
    if(energy<1) throw std::runtime_error("silent decode");
    auto before=clip.pcm.size(); walkup::Stadium(clip);
    if(clip.pcm.size()<=before) throw std::runtime_error("missing stadium tail");
    for(float sample:clip.pcm) if(!std::isfinite(sample)) throw std::runtime_error("nonfinite stadium mix");
    auto missing=walkup::Decode(file.parent_path()/"missing.wav",0,0,error);
    if(!missing.pcm.empty() || error.empty()) throw std::runtime_error("missing-file failure");
    std::cout<<"PASS WAV mono decode, trim, 48kHz resample, stereo, stadium tail and missing-file failure\n";
}
