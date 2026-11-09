
#pragma once
#include <cmath>
#include <concepts>
#include <cstddef>
#include <span>

namespace Core {

    template<class T>
    concept Interpolator = requires(std::span<const double> xs, std::span<const double> ys,
                                    std::size_t i, double x)
    {
        { T::to_node(x) }                     -> std::same_as<double>;
        { T::interpolate(xs, ys, i, x) }      -> std::same_as<double>;
        { T::extrapolate_left(xs, ys, x) }    -> std::same_as<double>;
        { T::extrapolate_right(xs, ys, x) }   -> std::same_as<double>;
    };

    /// @brief Linear interpolation of log(values): piecewise-flat forwards on discount factors.
    ///        Nodes are stored as log(value); extrapolation on the right keeps the last forward.
    struct LogLinearInterpolator {
        [[nodiscard]] static double to_node(double value) noexcept { return std::log(value); }

        [[nodiscard]] static double interpolate(std::span<const double> xs, std::span<const double> log_ys,
                                                std::size_t i, double x) noexcept {
            const double alpha = (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
            return std::exp(log_ys[i - 1] + alpha * (log_ys[i] - log_ys[i - 1]));
        }
        [[nodiscard]] static double extrapolate_left(std::span<const double>, std::span<const double> log_ys,
                                                     double) noexcept {
            return std::exp(log_ys.front());
        }

        [[nodiscard]] static double extrapolate_right(std::span<const double> xs, std::span<const double> log_ys,
                                                      double x) noexcept {
            const std::size_t n = xs.size();
            if (n < 2) return std::exp(log_ys.back());
            const double slope = (log_ys[n - 1] - log_ys[n - 2]) / (xs[n - 1] - xs[n - 2]);
            return std::exp(log_ys[n - 1] + slope * (x - xs[n - 1]));
        }
    };

    struct LinearInterpolator {
        [[nodiscard]] static constexpr double to_node(double value) noexcept { return value; }

        [[nodiscard]] static constexpr double interpolate(std::span<const double> xs, std::span<const double> ys,
                                                          std::size_t i, double x) noexcept {
            const double alpha = (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
            return ys[i - 1] + alpha * (ys[i] - ys[i - 1]);
        }
        [[nodiscard]] static constexpr double extrapolate_left(std::span<const double>, std::span<const double> ys,
                                                               double) noexcept {
            return ys.front();
        }
        [[nodiscard]] static constexpr double extrapolate_right(std::span<const double>, std::span<const double> ys,
                                                                double) noexcept {
            return ys.back();
        }
    };

    struct PiecewiseConstant {
        [[nodiscard]] static constexpr double to_node(double value) noexcept { return value; }
        [[nodiscard]] static constexpr double interpolate(std::span<const double>, std::span<const double> ys,
                                                          std::size_t i, double) noexcept {
            return ys[i];
        }
        [[nodiscard]] static constexpr double extrapolate_left(std::span<const double>, std::span<const double> ys,
                                                               double) noexcept {
            return ys.front();
        }
        [[nodiscard]] static constexpr double extrapolate_right(std::span<const double>, std::span<const double> ys,
                                                                double) noexcept {
            return ys.back();
        }
    };

}
