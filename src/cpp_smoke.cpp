#include <cstdint>

namespace {
class Counter final {
public:
    constexpr explicit Counter(std::uint32_t value) noexcept
        : value_(value) {}

    constexpr std::uint32_t next() const noexcept {
        return value_ + 1U;
    }

private:
    std::uint32_t value_;
};

static_assert(Counter{41U}.next() == 42U);
}  // namespace

// Can be called from any .c file with:
//   extern uint32_t cpp_smoke_test(void);
extern "C" std::uint32_t cpp_smoke_test(void) noexcept 
{
    constexpr Counter counter{41U};
    return counter.next();
}
