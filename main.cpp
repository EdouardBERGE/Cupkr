#include "upkr.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <unistd.h>

// Preset Z80 (identique a --z80 de upkr Rust) : c'est le format attendu par
// z80_unpacker/unpack.asm, applique PAR DEFAUT par cette version C++.
static void apply_z80(Config& c) {
    c = Config();
    c.use_bitstream = true;
    c.bitstream_is_big_endian = true;
    c.invert_bit_encoding = true;
    c.simplified_prob_update = true;
}
static const uint8_t Z80_LEVEL = 9;

static void usage(FILE* f) {
    fprintf(f,
"Usage:\n"
"  upkr [-l level(0-9)] [config options] <infile> [<outfile>]\n"
"  upkr -u [config options] <infile> [<outfile>]\n"
"  upkr --margin [config options] <infile>\n"
"\n"
"Par defaut : preset --z80 (format de z80_unpacker/unpack.asm).\n"
"\n"
" -l, --level N       compression level 0-9 (defaut 9 en z80, 2 en --generic)\n"
" -0, ..., -9         short form for setting compression level\n"
" -u, --unpack        unpack infile\n"
" --margin            calculate margin for overlapped unpacking of a packed file\n"
" -q, --quiet         pas de barre de progression (auto si stderr n'est pas un terminal)\n"
" --max-unpacked-size N\n"
"\n"
"Config presets (remplacent toute la config ; options suivantes appliquees par-dessus) :\n"
" --z80               [DEFAUT] --big-endian-bitstream --invert-bit-encoding --simplified-prob-update -9\n"
" --generic           format upkr standard (bytestream, -2), comme upkr Rust sans option\n"
" --x86               --bitstream --invert-is-match-bit --invert-continue-value-bit --invert-new-offset-bit\n"
" --x86b              --bitstream --invert-continue-value-bit --no-repeated-offsets -9\n"
"\n"
"Config options (need to match when packing/unpacking):\n"
" -b, --bitstream     bitstream mode\n"
" -p, --parity N      use N (2/4) parity contexts\n"
" -r, --reverse       reverse input & output\n"
"\n"
"Config options to tailor output to specific optimized unpackers:\n"
" --invert-is-match-bit\n"
" --invert-new-offset-bit\n"
" --invert-continue-value-bit\n"
" --invert-bit-encoding\n"
" --simplified-prob-update\n"
" --big-endian-bitstream   (implies --bitstream)\n"
" --no-repeated-offsets\n"
" --eof-in-length\n"
" --max-offset N\n"
" --max-length N\n");
}

static bool has_ext(const std::string& p, const char* ext) {
    return p.size() > strlen(ext) && p.compare(p.size()-strlen(ext), strlen(ext), ext)==0;
}

static std::string describe(const Config& c) {
    Config z; apply_z80(z);
    Config g;
    auto same = [](const Config& a, const Config& b) {
        return a.use_bitstream==b.use_bitstream && a.parity_contexts==b.parity_contexts
            && a.invert_bit_encoding==b.invert_bit_encoding && a.is_match_bit==b.is_match_bit
            && a.new_offset_bit==b.new_offset_bit && a.continue_value_bit==b.continue_value_bit
            && a.bitstream_is_big_endian==b.bitstream_is_big_endian
            && a.simplified_prob_update==b.simplified_prob_update
            && a.no_repeated_offsets==b.no_repeated_offsets && a.eof_in_length==b.eof_in_length
            && a.max_offset==b.max_offset && a.max_length==b.max_length;
    };
    if (same(c,z)) return "z80";
    if (same(c,g)) return "generic";
    return "custom";
}

// Barre de progression sur stderr (comme pbr dans la version Rust).
struct Progress {
    size_t total; bool enabled; int last_pct=-1;
    std::chrono::steady_clock::time_point t0=std::chrono::steady_clock::now(), last=t0;
    Progress(size_t t, bool e): total(t?t:1), enabled(e) {}
    void draw(size_t pos, bool force) {
        if (!enabled) return;
        auto now=std::chrono::steady_clock::now();
        int pct=(int)(pos*100/total);
        if (!force && pct==last_pct && now-last<std::chrono::milliseconds(200)) return;
        last_pct=pct; last=now;
        double s=std::chrono::duration<double>(now-t0).count();
        const int W=40; int fill=(int)(pos*W/total);
        char bar[W+1]; for (int i=0;i<W;i++) bar[i]= i<fill?'=':(i==fill?'>':' '); bar[W]=0;
        fprintf(stderr,"\r%zu / %zu [%s] %3d%% %.1fs ", pos,total,bar,pct,s);
        fflush(stderr);
    }
    void finish() { if (enabled) { draw(total,true); fputc('\n',stderr); } }
};

int main(int argc, char** argv) {
    Config config; apply_z80(config);
    uint8_t preset_level=Z80_LEVEL;
    int level=-1;                 // -1 : niveau du preset
    bool reverse=false, do_unpack=false, margin=false, quiet=false;
    std::string infile, outfile;
    size_t max_unpacked=512ull*1024*1024;
    auto need=[&](int& i)->const char* {
        if (i+1>=argc) { fprintf(stderr,"option %s : valeur manquante\n",argv[i]); exit(1); }
        return argv[++i];
    };
    try {
    for (int i=1;i<argc;i++) {
        std::string a=argv[i];
        if (a=="-b"||a=="--bitstream") config.use_bitstream=true;
        else if (a=="-p"||a=="--parity") config.parity_contexts=std::stoi(need(i));
        else if (a=="-r"||a=="--reverse") reverse=true;
        else if (a=="--invert-is-match-bit") config.is_match_bit=false;
        else if (a=="--invert-new-offset-bit") config.new_offset_bit=false;
        else if (a=="--invert-continue-value-bit") config.continue_value_bit=false;
        else if (a=="--invert-bit-encoding") config.invert_bit_encoding=true;
        else if (a=="--simplified-prob-update") config.simplified_prob_update=true;
        else if (a=="--big-endian-bitstream") { config.use_bitstream=true; config.bitstream_is_big_endian=true; }
        else if (a=="--no-repeated-offsets") config.no_repeated_offsets=true;
        else if (a=="--eof-in-length") config.eof_in_length=true;
        else if (a=="--max-offset") config.max_offset=std::stoul(need(i));
        else if (a=="--max-length") config.max_length=std::stoul(need(i));
        else if (a=="--z80") { apply_z80(config); preset_level=Z80_LEVEL; }
        else if (a=="--generic") { config=Config(); preset_level=2; }
        else if (a=="--x86") { config=Config(); config.use_bitstream=true; config.continue_value_bit=false; config.is_match_bit=false; config.new_offset_bit=false; preset_level=2; }
        else if (a=="--x86b") { config=Config(); config.use_bitstream=true; config.continue_value_bit=false; config.no_repeated_offsets=true; preset_level=9; }
        else if (a=="-u"||a=="--unpack") do_unpack=true;
        else if (a=="--margin") margin=true;
        else if (a=="-q"||a=="--quiet") quiet=true;
        else if (a=="--max-unpacked-size") max_unpacked=std::stoull(need(i));
        else if (a=="-l"||a=="--level") level=std::stoi(need(i));
        else if (a.size()==2 && a[0]=='-' && a[1]>='0' && a[1]<='9') level=a[1]-'0';
        else if (a=="-h"||a=="--help") { usage(stdout); return 0; }
        else if (a.size()>1 && a[0]=='-') { fprintf(stderr,"option inconnue : %s\n",a.c_str()); usage(stderr); return 1; }
        else if (infile.empty()) infile=a;
        else if (outfile.empty()) outfile=a;
        else { fprintf(stderr,"argument inattendu : %s\n",a.c_str()); return 1; }
    }
    } catch (const std::exception&) { fprintf(stderr,"valeur numerique invalide\n"); return 1; }
    if (infile.empty()) { usage(stderr); return 1; }
    if (level<0) level=preset_level;
    if (level>9) { fprintf(stderr,"--level doit etre entre 0 et 9\n"); return 1; }
    if (config.parity_contexts!=1 && config.parity_contexts!=2 && config.parity_contexts!=4) {
        fprintf(stderr,"--parity has to be 1, 2, or 4\n"); return 1;
    }
    if (outfile.empty()) {
        if (do_unpack) outfile = has_ext(infile,".upk") ? infile.substr(0,infile.size()-4) : infile+".bin";
        else outfile=infile+".upk";
    }

    std::ifstream in(infile, std::ios::binary);
    if (!in) { fprintf(stderr,"impossible d'ouvrir %s\n",infile.c_str()); return 1; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (reverse) std::reverse(data.begin(), data.end());

    std::string fmt=describe(config);
    if (!do_unpack && !margin) {
        fprintf(stderr,"Format %s%s, level %d\n", fmt.c_str(), reverse?" (reverse)":"", level);
        Progress pb(data.size(), !quiet && isatty(2));
        auto packed = pack(data, (uint8_t)level, config, [&](size_t pos){ pb.draw(pos,false); });
        pb.finish();
        if (reverse) std::reverse(packed.begin(), packed.end());
        std::ofstream out(outfile, std::ios::binary);
        if (!out) { fprintf(stderr,"impossible d'ecrire %s\n",outfile.c_str()); return 1; }
        out.write((char*)packed.data(), packed.size());
        printf("Compressed %zu bytes to %zu bytes (%.2f%%)\n", data.size(), packed.size(),
               data.empty()?0.0:packed.size()*100.0/data.size());
    } else {
        if (do_unpack) {
            auto outdata = unpack(data, config, max_unpacked);
            if (reverse) std::reverse(outdata.begin(), outdata.end());
            std::ofstream out(outfile, std::ios::binary);
            if (!out) { fprintf(stderr,"impossible d'ecrire %s\n",outfile.c_str()); return 1; }
            out.write((char*)outdata.data(), outdata.size());
            printf("Unpacked %zu bytes to %zu bytes (format %s)\n", data.size(), outdata.size(), fmt.c_str());
        }
        if (margin) printf("%lld\n", (long long)calculate_margin(data, config));
    }
    return 0;
}
