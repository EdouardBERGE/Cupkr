#include "upkr.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>

static const uint16_t PROB_BITS = 8;
static const uint32_t ONE_PROB = 1 << PROB_BITS;
static const uint16_t INIT_PROB = 1 << (PROB_BITS - 1);
static const uint32_t UPDATE_RATE = 4;
static const uint32_t UPDATE_ADD = 8;

// ---------------- ContextState ----------------
class ContextState {
public:
    std::vector<uint8_t> contexts;
    bool invert_bit_encoding;
    bool simplified_prob_update;
    ContextState(size_t size, const Config& config) {
        contexts.assign(size, (uint8_t)INIT_PROB);
        invert_bit_encoding = config.invert_bit_encoding;
        simplified_prob_update = config.simplified_prob_update;
    }
    ContextState(const ContextState& o) = default;
    struct Ctx {
        ContextState* st;
        size_t idx;
        uint16_t prob() const { return st->contexts[idx]; }
        void update(bool bit) {
            uint8_t old = st->contexts[idx];
            uint8_t val;
            if (st->simplified_prob_update) {
                int offset = (bit ^ st->invert_bit_encoding) ? (ONE_PROB >> UPDATE_RATE) : 0;
                val = (uint8_t)(offset + (int)old - (((int)old + (int)UPDATE_ADD) >> UPDATE_RATE));
            } else {
                if (bit ^ st->invert_bit_encoding) {
                    val = old + (uint8_t)(((ONE_PROB - old + UPDATE_ADD) >> UPDATE_RATE));
                } else {
                    val = old - (uint8_t)(((old + UPDATE_ADD) >> UPDATE_RATE));
                }
            }
            st->contexts[idx] = val;
        }
    };
    Ctx context_mut(size_t index) { Ctx c{this, index}; return c; }
};

// ---------------- MatchFinder ----------------
struct Match { size_t pos; size_t length; };

class MatchFinder {
public:
    std::vector<int32_t> suffixes;
    std::vector<uint32_t> rev_suffixes;
    std::vector<uint32_t> lcp;
    size_t max_queue_size=100;
    size_t max_matches_per_length=5;
    size_t patience=100;
    size_t max_length_diff=2;
    std::priority_queue<size_t> queue;

    MatchFinder(const std::vector<uint8_t>& data) {
        size_t n = data.size();
        suffixes.assign(n, 0);
        std::vector<size_t> idx(n);
        for (size_t i=0;i<n;i++) idx[i]=i;
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b){
            size_t i=a, j=b;
            while (i<n && j<n && data[i]==data[j]) { i++; j++; }
            if (i==n && j==n) return a < b;
            if (i==n) return true;
            if (j==n) return false;
            return data[i] < data[j];
        });
        for (size_t i=0;i<n;i++) suffixes[i] = (int32_t)idx[i];
        rev_suffixes.assign(n,0);
        for (size_t si=0; si<n; si++) rev_suffixes[suffixes[si]] = (uint32_t)si;
        lcp.assign(n,0);
        size_t length=0;
        for (size_t t=0; t<n; t++) {
            size_t suffix_index = rev_suffixes[t];
            if (suffix_index + 1 < n) {
                size_t i = suffixes[suffix_index];
                size_t j = suffixes[suffix_index + 1];
                while (i + length < n && j + length < n && data[i + length] == data[j + length]) length++;
                lcp[suffix_index] = (uint32_t)length;
            }
            if (length > 0) length--;
        }
    }
};

class Matches {
public:
    MatchFinder* finder;
    size_t pos_range_end;
    size_t left_index, left_length, right_index, right_length, current_length, matches_left, max_length;
    Matches(MatchFinder* f, size_t pos) {
        finder=f;
        pos_range_end=pos;
        size_t index = f->rev_suffixes[pos];
        std::priority_queue<size_t> empty; std::swap(finder->queue, empty);
        left_index=index; left_length=std::numeric_limits<size_t>::max();
        right_index=index; right_length=std::numeric_limits<size_t>::max();
        current_length=std::numeric_limits<size_t>::max();
        matches_left=0; max_length=0;
        move_left();
        move_right();
    }
    void move_left() {
        size_t patience = finder->patience;
        while (left_length > 0 && patience > 0 && left_index > 0) {
            left_index -= 1;
            left_length = std::min(left_length, (size_t)finder->lcp[left_index]);
            size_t suf = (size_t)finder->suffixes[left_index];
            if (suf < pos_range_end) return;
            patience -= 1;
        }
        left_length = 0;
    }
    void move_right() {
        size_t patience = finder->patience;
        size_t n = finder->suffixes.size();
        while (right_length > 0 && patience > 0 && right_index + 1 < n) {
            right_index += 1;
            right_length = std::min(right_length, (size_t)finder->lcp[right_index - 1]);
            size_t suf = (size_t)finder->suffixes[right_index];
            if (suf < pos_range_end) return;
            patience -= 1;
        }
        right_length = 0;
    }
    std::optional<Match> next() {
        if (finder->queue.empty() || matches_left == 0) {
            std::priority_queue<size_t> empty; std::swap(finder->queue, empty);
            size_t maxlr = std::max(left_length, right_length);
            if (current_length == std::numeric_limits<size_t>::max()) current_length = maxlr;
            else {
                if (current_length > 0) current_length -= 1;
                current_length = std::min(current_length, maxlr);
            }
            max_length = std::max(max_length, current_length);
            if (current_length < 2 || current_length + finder->max_length_diff < max_length) {
                return std::nullopt;
            }
            while (finder->queue.size() < finder->max_queue_size &&
                   (left_length == current_length || right_length == current_length)) {
                if (left_length == current_length) {
                    finder->queue.push((size_t)finder->suffixes[left_index]);
                    move_left();
                }
                if (right_length == current_length) {
                    finder->queue.push((size_t)finder->suffixes[right_index]);
                    move_right();
                }
            }
            matches_left = finder->max_matches_per_length;
        }
        if (matches_left > 0) matches_left--;
        if (finder->queue.empty()) return std::nullopt;
        size_t posm = finder->queue.top(); finder->queue.pop();
        return Match{posm, current_length};
    }
};

// ---------------- RansCoder etc ----------------
struct EntropyCoder {
    virtual void encode_bit(bool bit, uint16_t prob)=0;
    virtual ~EntropyCoder()=default;
};

class RansCoder : public EntropyCoder {
public:
    std::vector<uint16_t> bits;
    Config config;
    RansCoder(const Config& c): config(c) {}
    void encode_bit(bool bit, uint16_t prob) override {
        bits.push_back(prob | (((bit ^ config.invert_bit_encoding) ? 1 : 0) << 15));
    }
    std::vector<uint8_t> finish() {
        std::vector<uint8_t> buffer;
        uint32_t l_bits = config.use_bitstream ? 15 : 12;
        uint32_t state = 1 << l_bits;
        uint8_t byte = 0;
        int bitpos = config.bitstream_is_big_endian ? 0 : 8;
        uint32_t num_flush_bits = config.use_bitstream ? 1 : 8;
        uint32_t max_state_factor = 1 << (l_bits + num_flush_bits - PROB_BITS);
        for (auto it = bits.rbegin(); it != bits.rend(); ++it) {
            uint16_t step = *it;
            uint32_t prob = step & 32767;
            bool is_bit = (step & 32768) != 0;
            uint32_t start, p;
            if (is_bit) { start=0; p=prob; } else { start=prob; p=ONE_PROB - prob; }
            uint32_t max_state = max_state_factor * p;
            while (state >= max_state) {
                if (config.use_bitstream) {
                    if (config.bitstream_is_big_endian) {
                        byte |= ((state & 1)?1:0) << bitpos; bitpos++;
                        if (bitpos==8){ buffer.push_back(byte); byte=0; bitpos=0; }
                    } else {
                        bitpos--; byte |= ((state & 1)?1:0) << bitpos;
                        if (bitpos==0){ buffer.push_back(byte); byte=0; bitpos=8; }
                    }
                } else {
                    buffer.push_back((uint8_t)(state & 0xFF));
                }
                state >>= config.use_bitstream ? 1 : 8;
            }
            state = ((state / p) << PROB_BITS) + (state % p) + start;
        }
        while (state > 0) {
            if (config.use_bitstream) {
                if (config.bitstream_is_big_endian) {
                    byte |= ((state & 1)?1:0) << bitpos; bitpos++;
                    if (bitpos==8){ buffer.push_back(byte); byte=0; bitpos=0; }
                } else {
                    bitpos--; byte |= ((state & 1)?1:0) << bitpos;
                    if (bitpos==0){ buffer.push_back(byte); byte=0; bitpos=8; }
                }
            } else {
                buffer.push_back((uint8_t)(state & 0xFF));
            }
            state >>= config.use_bitstream ? 1 : 8;
        }
        if (config.use_bitstream && byte != 0) buffer.push_back(byte);
        std::reverse(buffer.begin(), buffer.end());
        return buffer;
    }
};

class CostCounter : public EntropyCoder {
public:
    double cost=0;
    std::vector<double> log2_table;
    bool invert;
    CostCounter(const Config& c) {
        invert = c.invert_bit_encoding;
        log2_table.resize(ONE_PROB);
        for (uint32_t prob=0; prob<ONE_PROB; prob++) {
            if (prob==0) { log2_table[prob]=0; continue; }
            double inv = (double)ONE_PROB / (double)prob;
            log2_table[prob] = std::log2(inv);
        }
    }
    void reset(){cost=0;}
    double costv() const { return cost; }
    void encode_bit(bool bit, uint16_t prob) override {
        uint32_t p = (bit ^ invert) ? prob : (ONE_PROB - prob);
        cost += log2_table[p];
    }
};

struct CoderState {
    ContextState contexts;
    uint32_t last_offset=0;
    bool prev_was_match=false;
    size_t pos=0;
    size_t parity_contexts=1;
    CoderState(const Config& c): contexts((1+255)*c.parity_contexts + 1 + 64 + 64, c), parity_contexts(c.parity_contexts) {}
    CoderState(const CoderState& o) = default;
};

// helpers encode
static inline void encode_bit(EntropyCoder& coder, CoderState& state, size_t ci, bool bit, ContextState& cs) {
    auto ctx = cs.context_mut(ci);
    coder.encode_bit(bit, ctx.prob());
    ctx.update(bit);
}
static void encode_length(EntropyCoder& coder, CoderState& state, size_t context_start, uint32_t value, const Config& config, ContextState& cs) {
    size_t context_index=context_start;
    while (value >= 2) {
        auto ctx1 = cs.context_mut(context_index);
        coder.encode_bit(config.continue_value_bit, ctx1.prob()); ctx1.update(config.continue_value_bit);
        auto ctx2 = cs.context_mut(context_index+1);
        coder.encode_bit(value&1, ctx2.prob()); ctx2.update(value&1);
        context_index += 2;
        value >>= 1;
    }
    auto ctx = cs.context_mut(context_index);
    coder.encode_bit(!config.continue_value_bit, ctx.prob()); ctx.update(!config.continue_value_bit);
}

static void encode_op(EntropyCoder& coder, CoderState& state, const Config& config, const Op& op) {
    size_t literal_base = state.pos % state.parity_contexts * 256;
    ContextState& cs = state.contexts;
    if (op.type == Op::Literal) {
        // literal base, !is_match
        auto ctx0 = cs.context_mut(literal_base);
        coder.encode_bit(!config.is_match_bit, ctx0.prob()); ctx0.update(!config.is_match_bit);
        size_t context_index=1;
        uint8_t lit=op.lit;
        for (int i=7;i>=0;i--) {
            bool bit = (lit>>i)&1;
            auto ctx = cs.context_mut(literal_base + context_index);
            coder.encode_bit(bit, ctx.prob()); ctx.update(bit);
            context_index = (context_index<<1) | (bit?1:0);
        }
        state.prev_was_match=false;
        state.pos += 1;
    } else {
        auto ctx0 = cs.context_mut(literal_base);
        coder.encode_bit(config.is_match_bit, ctx0.prob()); ctx0.update(config.is_match_bit);
        bool new_offset=true;
        if (!state.prev_was_match && !config.no_repeated_offsets) {
            new_offset = op.offset != state.last_offset;
            bool bit = (new_offset == config.new_offset_bit);
            auto ctx = cs.context_mut(256*state.parity_contexts);
            coder.encode_bit(bit, ctx.prob()); ctx.update(bit);
        }
        if (new_offset) {
            encode_length(coder, state, 256*state.parity_contexts + 1, op.offset + (config.eof_in_length?0:1), config, cs);
            state.last_offset = op.offset;
        }
        encode_length(coder, state, 256*state.parity_contexts + 65, op.len, config, cs);
        state.prev_was_match=true;
        state.pos += op.len;
    }
}

static void encode_eof(EntropyCoder& coder, CoderState& state, const Config& config) {
    ContextState& cs = state.contexts;
    size_t literal_base = state.pos % state.parity_contexts * 256;
    auto ctx0 = cs.context_mut(literal_base);
    coder.encode_bit(config.is_match_bit, ctx0.prob()); ctx0.update(config.is_match_bit);
    if (!state.prev_was_match && !config.no_repeated_offsets) {
        auto ctx = cs.context_mut(256*state.parity_contexts);
        coder.encode_bit(config.new_offset_bit ^ config.eof_in_length, ctx.prob()); ctx.update(config.new_offset_bit ^ config.eof_in_length);
    }
    if (!config.eof_in_length || state.prev_was_match || config.no_repeated_offsets) {
        encode_length(coder, state, 256*state.parity_contexts + 1, 1, config, cs);
    }
    if (config.eof_in_length) {
        encode_length(coder, state, 256*state.parity_contexts + 65, 1, config, cs);
    }
}

// ---------------- unpack ----------------
class RansDecoder {
public:
    const std::vector<uint8_t>* data;
    size_t pos=0;
    uint32_t state=0;
    bool use_bitstream;
    uint8_t byte=0;
    uint8_t bits_left=0;
    bool invert;
    bool big_endian;
    RansDecoder(const std::vector<uint8_t>& d, const Config& c): data(&d) {
        use_bitstream=c.use_bitstream; invert=c.invert_bit_encoding; big_endian=c.bitstream_is_big_endian;
    }
    size_t position() const { return pos; }
    bool decode_bit(uint32_t prob) {
        if (use_bitstream) {
            while (state < 32768) {
                if (bits_left==0) {
                    if (pos >= data->size()) throw std::runtime_error("EOF");
                    byte = (*data)[pos++];
                    bits_left=8;
                }
                if (big_endian) {
                    state = (state << 1) | ((byte >> 7) & 1);
                    byte <<= 1;
                } else {
                    state = (state << 1) | (byte & 1);
                    byte >>= 1;
                }
                bits_left--;
            }
        } else {
            while (state < 4096) {
                if (pos >= data->size()) throw std::runtime_error("EOF");
                state = (state << 8) | (*data)[pos++];
            }
        }
        bool bit = (state & (ONE_PROB-1)) < prob;
        uint32_t start, p;
        if (bit) { start=0; p=prob; } else { start=prob; p=ONE_PROB - prob; }
        state = p * (state >> PROB_BITS) + (state & (ONE_PROB-1)) - start;
        return bit ^ invert;
    }
    bool decode_with_context(ContextState& cs, size_t ci) {
        auto ctx = cs.context_mut(ci);
        bool bit = decode_bit(ctx.prob());
        ctx.update(bit);
        return bit;
    }
};

uint32_t decode_length_dec(RansDecoder& dec, ContextState& cs, size_t ci, const Config& c) {
    uint32_t length=0; size_t bit_pos=0;
    while (dec.decode_with_context(cs, ci) == c.continue_value_bit) {
        bool b = dec.decode_with_context(cs, ci+1);
        length |= (b?1u:0u) << bit_pos;
        bit_pos++;
        if (bit_pos >= 32) throw std::runtime_error("overflow");
        ci += 2;
    }
    return length | (1u << bit_pos);
}

std::vector<uint8_t> unpack(const std::vector<uint8_t>& packed, const Config& config, size_t max_size) {
    bool dummy=false;
    // call internal
    std::vector<uint8_t> result;
    std::vector<uint8_t>* resptr = &result;
    // reuse internal with has result true
    RansDecoder dec(packed, config);
    ContextState contexts((1+255)*config.parity_contexts + 1 + 64 + 64, config);
    size_t offset = std::numeric_limits<size_t>::max();
    size_t position=0;
    bool prev_was_match=false;
    long long margin=0;
    for (;;) {
        margin = std::max(margin, (long long)position - (long long)dec.position());
        size_t literal_base = position % config.parity_contexts * 256;
        bool ismatch = dec.decode_with_context(contexts, literal_base) == config.is_match_bit;
        if (ismatch) {
            if (config.no_repeated_offsets || prev_was_match || dec.decode_with_context(contexts, 256*config.parity_contexts) == config.new_offset_bit) {
                offset = decode_length_dec(dec, contexts, 256*config.parity_contexts + 1, config) - (config.eof_in_length?0:1);
                if (offset == 0) break;
            }
            size_t length = decode_length_dec(dec, contexts, 256*config.parity_contexts + 65, config);
            if (config.eof_in_length && length == 1) break;
            if (offset > position) throw std::runtime_error("offset out of range");
            for (size_t i=0;i<length;i++) {
                if (result.size() < max_size) result.push_back(result[result.size() - offset]);
            }
            position += length;
            prev_was_match=true;
        } else {
            size_t context_index=1;
            uint8_t byte=0;
            for (int i=7;i>=0;i--) {
                bool bit = dec.decode_with_context(contexts, literal_base + context_index);
                context_index = (context_index<<1) | (bit?1:0);
                byte |= (bit?1:0) << i;
            }
            if (result.size() < max_size) result.push_back(byte);
            position += 1;
            prev_was_match=false;
        }
    }
    if (position > max_size) throw std::runtime_error("oversize");
    return result;
}

long long calculate_margin(const std::vector<uint8_t>& packed, const Config& config) {
    RansDecoder dec(packed, config);
    ContextState contexts((1+255)*config.parity_contexts + 1 + 64 + 64, config);
    size_t offset = std::numeric_limits<size_t>::max();
    size_t position=0;
    bool prev_was_match=false;
    long long margin=0;
    for (;;) {
        margin = std::max(margin, (long long)position - (long long)dec.position());
        size_t literal_base = position % config.parity_contexts * 256;
        bool ismatch = dec.decode_with_context(contexts, literal_base) == config.is_match_bit;
        if (ismatch) {
            if (config.no_repeated_offsets || prev_was_match || dec.decode_with_context(contexts, 256*config.parity_contexts) == config.new_offset_bit) {
                offset = decode_length_dec(dec, contexts, 256*config.parity_contexts + 1, config) - (config.eof_in_length?0:1);
                if (offset == 0) break;
            }
            size_t length = decode_length_dec(dec, contexts, 256*config.parity_contexts + 65, config);
            if (config.eof_in_length && length == 1) break;
            if (offset > position) throw std::runtime_error("offset out of range");
            position += length;
            prev_was_match=true;
        } else {
            size_t context_index=1;
            for (int i=7;i>=0;i--) {
                bool bit = dec.decode_with_context(contexts, literal_base + context_index);
                context_index = (context_index<<1) | (bit?1:0);
            }
            position += 1;
            prev_was_match=false;
        }
    }
    long long final_margin = margin + (long long)dec.position() - (long long)position;
    return final_margin;
}

// ---------------- packers ----------------
std::vector<uint8_t> greedy_pack(const std::vector<uint8_t>& data, const Config& config, ProgressCallback cb) {
    MatchFinder mf(data);
    mf.max_queue_size=100; mf.max_matches_per_length=5; mf.patience=100; mf.max_length_diff=2;
    RansCoder coder(config);
    CoderState state(config);
    size_t pos=0;
    while (pos < data.size()) {
        if (cb) cb(pos);
        bool encoded=false;
        // matches(pos).next()
        Matches matches(&mf, pos);
        auto m = matches.next();
        if (m) {
            size_t max_offset = config.max_offset;
            size_t tmp = (m->length*3 -1); if (tmp>31) tmp=31;
            size_t limit = 1ull << tmp;
            max_offset = std::min(max_offset, limit);
            size_t offset = pos - m->pos;
            if (offset < max_offset && m->length >= config.min_length()) {
                size_t length = std::min((size_t)m->length, config.max_length);
                Op op; op.type=Op::Match; op.offset=(uint32_t)offset; op.len=(uint32_t)length;
                encode_op(coder, state, config, op);
                pos += length;
                encoded=true;
            }
        }
        if (!encoded) {
            size_t offset = state.last_offset;
            if (offset != 0) {
                size_t length = 0;
                while (pos+length < data.size() && (pos - offset + length) < data.size() && data[pos+length] == data[pos - offset + length]) length++;
                length = std::min(length, config.max_length);
                if (length >= config.min_length()) {
                    Op op; op.type=Op::Match; op.offset=(uint32_t)offset; op.len=(uint32_t)length;
                    encode_op(coder, state, config, op);
                    pos += length;
                    encoded=true;
                }
            }
        }
        if (!encoded) {
            Op op; op.type=Op::Literal; op.lit=data[pos];
            encode_op(coder, state, config, op);
            pos += 1;
        }
    }
    // encode eof as match? Use state mutable via RansCoder finish? encode_eof takes EntropyCoder&
    // We can call encode_eof with coder (RansCoder derives EntropyCoder)
    {
        // encode_eof signature EntropyCoder&; coder is RansCoder
        encode_eof(coder, state, config);
    }
    return coder.finish();
}

// parsing packer config
struct ParseConfig {
    size_t max_arrivals;
    double max_cost_delta;
    double max_offset_cost_delta;
    size_t num_near_matches;
    size_t greedy_size;
    size_t max_queue_size;
    size_t patience;
    size_t max_matches_per_length;
    size_t max_length_diff;
    static ParseConfig from_level(uint8_t level) {
        ParseConfig c;
        if (level<=1) c.max_arrivals=0;
        else if (level==2) c.max_arrivals=2;
        else if (level==3) c.max_arrivals=4;
        else if (level==4) c.max_arrivals=8;
        else if (level==5) c.max_arrivals=16;
        else if (level==6) c.max_arrivals=32;
        else if (level==7) c.max_arrivals=64;
        else if (level==8) c.max_arrivals=96;
        else c.max_arrivals=128;
        if (level<=4) { c.max_cost_delta=16; c.max_offset_cost_delta=0; }
        else if (level<=8) { c.max_cost_delta=16; c.max_offset_cost_delta=4; }
        else { c.max_cost_delta=16; c.max_offset_cost_delta=8; }
        c.num_near_matches = level>0? level-1 : 0;
        c.greedy_size = 4 + (size_t)level*level*3;
        if (level<=1) c.max_length_diff=0;
        else if (level<=3) c.max_length_diff=1;
        else if (level<=5) c.max_length_diff=2;
        else if (level<=7) c.max_length_diff=3;
        else c.max_length_diff=4;
        c.max_queue_size = (size_t)level*100;
        c.patience = (size_t)level*100;
        c.max_matches_per_length = level;
        return c;
    }
};

struct ParseNode { std::shared_ptr<ParseNode> prev; Op op; };
struct Arrival { std::shared_ptr<ParseNode> parse; CoderState state; double cost; };

std::vector<uint8_t> parsing_pack(const std::vector<uint8_t>& data, uint8_t level, const Config& config, ProgressCallback cb) {
    ParseConfig pcfg = ParseConfig::from_level(level);
    MatchFinder base_mf(data);
    // We'll set fields when using
    std::vector<int> near_matches(1024, -1);
    std::vector<int> last_seen(256, -1);
    size_t max_arrivals = pcfg.max_arrivals;
    std::unordered_map<size_t, std::vector<Arrival>> arrivals;
    auto sort_arrivals = [&](std::vector<Arrival>& vec) {
        if (max_arrivals==0) return;
        std::sort(vec.begin(), vec.end(), [](const Arrival&a, const Arrival&b){ return a.cost < b.cost; });
        std::unordered_set<size_t> seen;
        std::vector<Arrival> remaining;
        std::vector<Arrival> out;
        for (auto& arr : vec) {
            if (seen.insert(arr.state.last_offset).second) {
                if (out.size() < max_arrivals) out.push_back(arr);
            } else remaining.push_back(arr);
        }
        for (auto& arr : remaining) {
            if (out.size() >= max_arrivals) break;
            out.push_back(arr);
        }
        vec = std::move(out);
    };
    auto add_arrival = [&](size_t pos, Arrival arr) {
        auto& vec = arrivals[pos];
        if (max_arrivals==0) {
            if (vec.empty()) vec.push_back(arr);
            else if (vec[0].cost > arr.cost) vec[0]=arr;
            return;
        }
        vec.push_back(arr);
        if (vec.size() > max_arrivals*2) {
            sort_arrivals(vec);
        }
    };
    // CostCounter for encoding cost estimation
    CostCounter cost_counter(config);
    // initial arrival
    {
        Arrival a{nullptr, CoderState(config), 0.0};
        add_arrival(0, a);
    }
    auto add_match = [&](std::unordered_map<size_t,std::vector<Arrival>>& arrivals, size_t pos, size_t offset, size_t length, const Arrival& arrival) {
        if (length < config.min_length()) return;
        length = std::min(length, (size_t)config.max_length);
        CostCounter cc(config); // reset cost
        CoderState st = arrival.state;
        Op op; op.type=Op::Match; op.offset=(uint32_t)offset; op.len=(uint32_t)length;
        // encode to cost counter
        CostCounter ccc = CostCounter(config);
        // Need encode with cost counter; encode_op template? encode_op takes EntropyCoder&; CostCounter derives.
        CoderState st2 = arrival.state;
        encode_op(ccc, st2, config, op);
        Arrival na{std::make_shared<ParseNode>(), st2, arrival.cost+ccc.costv()};
        na.parse->prev=arrival.parse; na.parse->op=op;
        add_arrival(pos+length, na);
    };
    std::unordered_map<uint32_t, double> best_per_offset;
    for (size_t pos=0; pos<data.size(); pos++) {
        // compute match_length lambda
        auto match_length = [&](size_t offset)->size_t {
            size_t L=0;
            while (pos+L < data.size() && (pos - offset + L) < data.size() && data[pos+L]==data[pos-offset+L]) L++;
            return L;
        };
        auto it_arr = arrivals.find(pos);
        if (it_arr == arrivals.end()) continue;
        auto here = it_arr->second;
        arrivals.erase(it_arr);
        sort_arrivals(here);
        best_per_offset.clear();
        double best_cost = 1e300;
        for (auto& a : here) {
            best_cost = std::min(best_cost, a.cost);
            auto itbo = best_per_offset.find(a.state.last_offset);
            if (itbo==best_per_offset.end()) best_per_offset[a.state.last_offset]=a.cost; else itbo->second=std::min(itbo->second,a.cost);
        }
        // match finder for this pos
        MatchFinder mf = base_mf;
        mf.max_queue_size = pcfg.max_queue_size;
        mf.max_matches_per_length = pcfg.max_matches_per_length;
        mf.patience = pcfg.patience;
        mf.max_length_diff = pcfg.max_length_diff;
        // base copy suffix arrays
    for (auto& arrival : here) {
            if (arrival.cost > std::min(best_cost + pcfg.max_cost_delta, best_per_offset[arrival.state.last_offset] + pcfg.max_offset_cost_delta)) continue;
            bool found_last_offset=false;
            std::optional<size_t> closest_match;
            Matches matches(&mf, pos);
            // iterate
            while (true) {
                auto m = matches.next();
                if (!m) break;
                closest_match = closest_match.has_value() ? std::max(closest_match.value(), m->pos) : m->pos;
                size_t offset = pos - m->pos;
                if (offset <= config.max_offset) {
                    found_last_offset |= ((uint32_t)offset == arrival.state.last_offset);
                    add_match(arrivals, pos, offset, m->length, arrival);
                    if ((size_t)m->length >= pcfg.greedy_size) break;
                }
                if ((size_t)m->length >= pcfg.greedy_size) {
                    // break arrival loop
                    goto after_matches;
                }
            }
            after_matches:
            // near matches
            size_t near_left = pcfg.num_near_matches;
            int match_pos = (last_seen[(uint8_t)data[pos]]); // -1 if none
            while (near_left>0 && match_pos != -1 && (!closest_match.has_value() || closest_match.value() < (size_t)match_pos)) {
                size_t offset = pos - (size_t)match_pos;
                if (offset > config.max_offset) break;
                size_t length = match_length(offset);
                // length>0
                add_match(arrivals, pos, offset, length, arrival);
                found_last_offset |= ((uint32_t)offset == arrival.state.last_offset);
                if (offset < near_matches.size()) match_pos = near_matches[(size_t)match_pos % near_matches.size()];
                near_left--;
            }
            if (!found_last_offset && arrival.state.last_offset > 0) {
                size_t offset = arrival.state.last_offset;
                size_t length = match_length(offset);
                if (length > 0) add_match(arrivals, pos, offset, length, arrival);
            }
            CostCounter cc(config);
            CoderState st = arrival.state;
            Op op; op.type=Op::Literal; op.lit=data[pos];
            encode_op(cc, st, config, op);
            Arrival na{std::make_shared<ParseNode>(), st, arrival.cost+cc.costv()};
            na.parse->prev=arrival.parse; na.parse->op=op;
            add_arrival(pos+1, na);
        }
        near_matches[pos % near_matches.size()] = last_seen[(uint8_t)data[pos]];
        last_seen[(uint8_t)data[pos]] = (int)pos;
        if (cb) cb(pos+1);
    }
    if (arrivals.find(data.size())==arrivals.end()) return {};
    if (arrivals[data.size()].empty()) return {};
    auto parse = arrivals[data.size()][0].parse;
    // build ops reverse
    std::vector<Op> ops;
    while (parse) {
        ops.push_back(parse->op);
        parse = parse->prev;
    }
    std::vector<Op> revops; // ops collected from end to start; need reverse
    std::reverse(ops.begin(), ops.end());
    // ops currently? Rust: while let Some(link)=parse { ops.push(link.op); parse=link.prev } => ops from tail to head. Then coder encodes ops.into_iter().rev(). So if we reverse ops we got head-to-tail; then encode in that order? Rust encodes ops reversed (head-to-tail? Let's verify: ops tail-to-head, .rev() => head-to-tail). So we need head-to-tail. Our ops collected tail-to-head; reverse => head-to-tail. Good.
    CoderState state(config);
    RansCoder coder(config);
    for (auto& op : ops) encode_op(coder, state, config, op);
    encode_eof(coder, state, config);
    return coder.finish();
}

// wrapper pack
std::vector<uint8_t> pack(const std::vector<uint8_t>& data, uint8_t level, const Config& config, ProgressCallback progress) {
    if (level==0) return greedy_pack(data, config, progress);
    else return parsing_pack(data, level, config, progress);
}

// fix parsing_pack call signature order already
