#pragma once
// Exact rational number used for all musical time values (never floating point).
#include <compare>
#include <cstdint>
#include <numeric>

namespace pauta {

struct Frac {
    int64_t num = 0;
    int64_t den = 1;

    constexpr Frac() = default;
    constexpr Frac(int64_t n, int64_t d = 1) : num(n), den(d) { normalize(); }

    constexpr void normalize()
    {
        if (den < 0) { num = -num; den = -den; }
        const int64_t g = std::gcd(num < 0 ? -num : num, den);
        if (g > 1) { num /= g; den /= g; }
        if (num == 0) den = 1;
    }

    constexpr Frac operator+(const Frac& o) const { return Frac(num * o.den + o.num * den, den * o.den); }
    constexpr Frac operator-(const Frac& o) const { return Frac(num * o.den - o.num * den, den * o.den); }
    constexpr Frac operator*(const Frac& o) const { return Frac(num * o.num, den * o.den); }
    constexpr Frac operator/(int64_t k) const { return Frac(num, den * k); }
    constexpr Frac& operator+=(const Frac& o) { return *this = *this + o; }
    constexpr Frac& operator-=(const Frac& o) { return *this = *this - o; }

    constexpr bool operator==(const Frac& o) const { return num == o.num && den == o.den; }
    constexpr std::strong_ordering operator<=>(const Frac& o) const { return num * o.den <=> o.num * den; }

    constexpr double toDouble() const { return double(num) / double(den); }
};

} // namespace pauta
