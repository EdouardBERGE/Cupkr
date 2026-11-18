#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <functional>

struct Config {
    bool use_bitstream = false;
    int parity_contexts = 1;

    bool invert_bit_encoding = false;
    bool is_match_bit = true;
    bool new_offset_bit = true;
    bool continue_value_bit = true;

    bool bitstream_is_big_endian = false;
    bool simplified_prob_update = false;

    bool no_repeated_offsets = false;
    bool eof_in_length = false;

    size_t max_offset = std::numeric_limits<size_t>::max();
    size_t max_length = std::numeric_limits<size_t>::max();

    size_t min_length() const { return eof_in_length ? 2 : 1; }
};

struct Op {
    enum Type { Literal, Match } type;
    uint8_t lit = 0;
    uint32_t offset = 0;
    uint32_t len = 0;
};

typedef std::function<void(size_t)> ProgressCallback;

std::vector<uint8_t> pack(const std::vector<uint8_t>& data, uint8_t level, const Config& config, ProgressCallback progress = nullptr);
std::vector<uint8_t> unpack(const std::vector<uint8_t>& packed, const Config& config, size_t max_size = 512ull*1024*1024);
long long calculate_margin(const std::vector<uint8_t>& packed, const Config& config);
