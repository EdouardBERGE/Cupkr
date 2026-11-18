#include "upkr.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>

static void usage() {
    fprintf(stderr, "Usage: upkr [-l level] [options] <infile> [<outfile>]\n");
}

static bool has_ext(const std::string& p, const char* ext) {
    return p.size() > strlen(ext) && p.compare(p.size()-strlen(ext), strlen(ext), ext)==0;
}

int main(int argc, char** argv) {
    Config config;
    bool reverse=false, do_unpack=false, margin=false;
    uint8_t level=2;
    std::string infile, outfile;
    size_t max_unpacked=512ull*1024*1024;
    for (int i=1;i<argc;i++) {
        std::string a=argv[i];
        if (a=="-b"||a=="--bitstream") config.use_bitstream=true;
        else if (a=="-p"||a=="--parity") { config.parity_contexts=std::stoi(argv[++i]); }
        else if (a=="-r"||a=="--reverse") reverse=true;
        else if (a=="--invert-is-match-bit") config.is_match_bit=false;
        else if (a=="--invert-new-offset-bit") config.new_offset_bit=false;
        else if (a=="--invert-continue-value-bit") config.continue_value_bit=false;
        else if (a=="--invert-bit-encoding") config.invert_bit_encoding=true;
        else if (a=="--simplified-prob-update") config.simplified_prob_update=true;
        else if (a=="--big-endian-bitstream") { config.use_bitstream=true; config.bitstream_is_big_endian=true; }
        else if (a=="--no-repeated-offsets") config.no_repeated_offsets=true;
        else if (a=="--eof-in-length") config.eof_in_length=true;
        else if (a=="--max-offset") config.max_offset=std::stoul(argv[++i]);
        else if (a=="--max-length") config.max_length=std::stoul(argv[++i]);
        else if (a=="--z80") { config.use_bitstream=true; config.bitstream_is_big_endian=true; config.invert_bit_encoding=true; config.simplified_prob_update=true; level=9; }
        else if (a=="--x86") { config.use_bitstream=true; config.continue_value_bit=false; config.is_match_bit=false; config.new_offset_bit=false; }
        else if (a=="--x86b") { config.use_bitstream=true; config.continue_value_bit=false; config.no_repeated_offsets=true; level=9; }
        else if (a=="-u"||a=="--unpack") do_unpack=true;
        else if (a=="--margin") margin=true;
        else if (a=="-l"||a=="--level") level=std::stoi(argv[++i]);
        else if (a.size()==2 && a[0]=='-' && a[1]>='0' && a[1]<='9') level=a[1]-'0';
        else if (a=="-h"||a=="--help") { usage(); return 0; }
        else if (infile.empty()) infile=a;
        else if (outfile.empty()) outfile=a;
        else { fprintf(stderr,"unexpected %s\n",a.c_str()); return 1; }
    }
    if (infile.empty()) { usage(); return 1; }
    if (outfile.empty()) {
        if (do_unpack) {
            if (has_ext(infile,".upk")) { outfile=infile.substr(0,infile.size()-4); }
            else outfile=infile+".bin";
        } else {
            outfile=infile+".upk";
        }
    }
    // read
    std::ifstream in(infile, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (reverse) std::reverse(data.begin(), data.end());
    if (!do_unpack && !margin) {
        auto packed = pack(data, level, config, nullptr);
        if (reverse) std::reverse(packed.begin(), packed.end());
        std::ofstream out(outfile, std::ios::binary);
        out.write((char*)packed.data(), packed.size());
        printf("Compressed %zu to %zu\n", data.size(), packed.size());
    } else {
        if (do_unpack) {
            auto outdata = unpack(data, config, max_unpacked);
            if (reverse) std::reverse(outdata.begin(), outdata.end());
            std::ofstream out(outfile, std::ios::binary);
            out.write((char*)outdata.data(), outdata.size());
        }
        if (margin) {
            printf("%lld\n", (long long)calculate_margin(data, config));
        }
    }
    return 0;
}