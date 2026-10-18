#ifndef WALNUT_SEMANTICS_COMPILE_TIME_RANDOM_HPP
#define WALNUT_SEMANTICS_COMPILE_TIME_RANDOM_HPP

#include <array>
#include <cstdint>
#include <string>

namespace walnut {
namespace semantics {

void set_compile_time_random_seed(const std::string& seed);
bool compile_time_random_is_seeded();

class RandomStream {
public:
    RandomStream(std::uint64_t site, std::uint64_t place, std::uint64_t draw);

    std::uint32_t next32();
    std::uint64_t next64();
    std::uint64_t below(std::uint64_t bound);

private:
    void refill();

    std::array<std::uint32_t, 16> m_input{};
    std::array<std::uint32_t, 16> m_block{};
    std::size_t                   m_used = 16;
};

} // namespace semantics
} // namespace walnut

#endif // WALNUT_SEMANTICS_COMPILE_TIME_RANDOM_HPP
