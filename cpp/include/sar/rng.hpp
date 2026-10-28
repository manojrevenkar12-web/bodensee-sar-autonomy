// Reproducible random numbers on every compiler: xoshiro256** seeded by SplitMix64, with our own
// uniform and normal draws (the standard library's distributions differ between implementations).
#pragma once

#include <cmath>
#include <cstdint>

namespace sar {

inline std::uint64_t splitmix64(std::uint64_t& state) {
  std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

// Derive an independent stream seed, e.g. one for the planner's belief and one for the truth.
inline std::uint64_t derive_seed(std::uint64_t seed, std::uint64_t stream) {
  std::uint64_t s = seed ^ (stream * 0xD1B54A32D192ED03ULL);
  return splitmix64(s);
}

class Rng {
 public:
  explicit Rng(std::uint64_t seed = 1) { reseed(seed); }

  void reseed(std::uint64_t seed) {
    std::uint64_t sm = seed;
    for (auto& w : s_) w = splitmix64(sm);
    has_spare_ = false;
  }

  std::uint64_t next() {
    const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
    const std::uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return result;
  }

  // Uniform in [0, 1) with 53 bits.
  double uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }

  // Standard normal by the polar Box-Muller method.
  double normal() {
    if (has_spare_) {
      has_spare_ = false;
      return spare_;
    }
    double u, v, s;
    do {
      u = 2.0 * uniform() - 1.0;
      v = 2.0 * uniform() - 1.0;
      s = u * u + v * v;
    } while (s >= 1.0 || s == 0.0);
    const double m = std::sqrt(-2.0 * std::log(s) / s);
    spare_ = v * m;
    has_spare_ = true;
    return u * m;
  }

  double normal(double mean, double sd) { return mean + sd * normal(); }

  // Index drawn from unnormalised non-negative weights.
  template <class Vec>
  int categorical(const Vec& w) {
    double total = 0.0;
    for (double x : w) total += x;
    double r = uniform() * total;
    for (int i = 0; i < static_cast<int>(w.size()); ++i) {
      r -= w[i];
      if (r < 0.0) return i;
    }
    return static_cast<int>(w.size()) - 1;
  }

 private:
  static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
  std::uint64_t s_[4]{};
  double spare_ = 0.0;
  bool has_spare_ = false;
};

}  // namespace sar
