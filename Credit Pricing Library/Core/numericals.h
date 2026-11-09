
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace MathConstants {
    inline constexpr double PI           = 3.14159265358979323846;
    inline constexpr double SQRT2        = 1.41421356237309504880;
    inline constexpr double INV_SQRT2    = 0.70710678118654752440;
    inline constexpr double INV_SQRT_2PI = 0.39894228040143267794;
    inline constexpr double INV_SQRT_PI  = 0.56418958354775628695;
}

namespace Core {

    class NewtonRaphson {
        public:
        NewtonRaphson() = delete;
        NewtonRaphson(const double tol, const int iter_max) : m_tol(tol), m_iter_max(iter_max) {
        }

        /// @brief Solve f(x) = 0. f_and_df(x) return a pair {f(x), f'(x)}.
        ///        [lower, upper] : optional bounds on the values of x (none by default).
        template<typename F>
        [[nodiscard]] double solve(F&& f_and_df, const double x0,
                                   const double lower = -std::numeric_limits<double>::infinity(),
                                   const double upper =  std::numeric_limits<double>::infinity()) const {
            if (!(lower <= upper))
                throw std::invalid_argument("NewtonRaphson : lower bound must be <= upper bound");
            double x = std::clamp(x0, lower, upper);
            for (int i = 0; i < m_iter_max; ++i) {
                const auto [f, df] = f_and_df(x);
                if (std::abs(f) < m_tol) return x;
                if (df == 0.0 || !std::isfinite(df))
                    throw std::runtime_error("NewtonRaphson : null or non-finite derivative");
                const double x_new = std::clamp(x - f / df, lower, upper);
                if (x_new == x)
                    throw std::runtime_error("NewtonRaphson : stuck on a bound, no root found");
                if (std::abs(x_new - x) < m_tol * (1.0 + std::abs(x))) return x_new;
                x = x_new;
            }
            throw std::runtime_error("NewtonRaphson : No convergence of the algorithm");
        }

        private:
        double m_tol;
        int m_iter_max;
    };

    class Brent {
    public :
        Brent() = delete;
        Brent(const double tol, const int iter_max) : m_tol(tol), m_iter_max(iter_max) {
        }
        /// @brief Solve f(x) = 0. f return a functional f.

        template<typename F>
        [[nodiscard]] double solve(F&& f, double a, double b) const {
                double fa = f(a), fb = f(b);
                if (fa * fb > 0.0)
                    throw std::runtime_error("Brent : f(a) and f(b) got the same sign ");

                double c = a, fc = fa, d = b-a, e = d;
                for (int i = 0; i < m_iter_max; ++i) {
                    if (fb * fc > 0.0) { c = a; fc = fa; d = e = b-a; }
                    if (std::abs(fc) < std::abs(fb)) {
                        a = b; fa = fb; b = c; fb = fc; c = a; fc = fa;
                    }
                    const double tol1 = 2.0*m_tol*std::abs(b) + 0.5*m_tol;
                    const double m    = 0.5*(c - b);
                    if (std::abs(m) <= tol1 || fb == 0.0) return b;

                    if (std::abs(e) >= tol1 && std::abs(fa) > std::abs(fb)) {
                        double s = fb/fa, p, q, r;
                        if (a == c) {
                            p = 2.0*m*s; q = 1.0-s;
                        } else {
                            q = fa/fc; r = fb/fc;
                            p = s*(2.0*m*q*(q-r) - (b-a)*(r-1.0));
                            q = (q-1.0)*(r-1.0)*(s-1.0);
                        }
                        if (p > 0.0) q = -q; else p = -p;
                        if (2.0*p < std::min(3.0*m*q - std::abs(tol1*q), std::abs(e*q))) {
                            e = d; d = p/q;
                        } else { d = m; e = d; }
                    } else { d = m; e = d; }

                    a = b; fa = fb;
                    b += (std::abs(d) > tol1) ? d : (m > 0 ? tol1 : -tol1);
                    fb = f(b);
                }
                throw std::runtime_error("Brent : No convergence of the algorithm");
            }
    private:
        double m_tol;
        int m_iter_max;
    };

    [[nodiscard]] inline double norm_cdf(double x) noexcept {
    const double z = std::abs(x);
    double c = 0.0;
    if (z <= 37.0) {
        const double e = std::exp(-0.5 * z * z);
        if (z < 7.07106781186547) {
            const double n = ((((((0.0352624965998911 * z + 0.700383064443688) * z + 6.37396220353165) * z
                               + 33.912866078383) * z + 112.079291497871) * z + 221.213596169931) * z
                               + 220.206867912376);
            const double d = (((((((0.0883883476483184 * z + 1.75566716318264) * z + 16.064177579207) * z
                               + 86.7807322029461) * z + 296.564248779674) * z + 637.333633378831) * z
                               + 793.826512519948) * z + 440.413735824752);
            c = e * n / d;
        } else {
            const double d = z + 1.0 / (z + 2.0 / (z + 3.0 / (z + 4.0 / (z + 0.65))));
            c = e / d / 2.506628274631;
        }
    }
    if (std::isnan(x)) return x;
    return x <= 0.0 ? c : 1.0 - c;
}

[[nodiscard]] inline double norm_pdf(double x) noexcept {
    return MathConstants::INV_SQRT_2PI * std::exp(-0.5 * x * x);
}

/// @brief Guaussian law inverse (Acklam algorithm, relative precision of ~1.15e-9).
[[nodiscard]] inline double norm_inv(double p) {
    if (p <= 0.0 || p >= 1.0)
        throw std::domain_error("norm_inv: p must be in (0,1)");

    static constexpr double a[] = {
        -3.969683028665376e+01,  2.209460984245205e+02,
        -2.759285104469687e+02,  1.383577518672690e+02,
        -3.066479806614716e+01,  2.506628277459239e+00
    };
    static constexpr double b[] = {
        -5.447609879822406e+01,  1.615858368580409e+02,
        -1.556989798598866e+02,  6.680131188771972e+01,
        -1.328068155288572e+01
    };
    static constexpr double c[] = {
        -7.784894002430293e-03, -3.223964580411365e-01,
        -2.400758277161838e+00, -2.549732539343734e+00,
         4.374664141464968e+00,  2.938163982698783e+00
    };
    static constexpr double d[] = {
         7.784695709041462e-03,  3.224671290700398e-01,
         2.445134137142996e+00,  3.754408661907416e+00
    };

    double x;
    if (p < 0.02425) {

        double t = std::sqrt(-2.0 * std::log(p));
        x = (((((c[0]*t+c[1])*t+c[2])*t+c[3])*t+c[4])*t+c[5]) /
            ((((d[0]*t+d[1])*t+d[2])*t+d[3])*t+1.0);
    } else if (p > 1.0 - 0.02425) {

        double t = std::sqrt(-2.0 * std::log(1.0 - p));
        x = -(((((c[0]*t+c[1])*t+c[2])*t+c[3])*t+c[4])*t+c[5]) /
              ((((d[0]*t+d[1])*t+d[2])*t+d[3])*t+1.0);
    } else {

        double u = p - 0.5, t = u * u;
        x = u * (((((a[0]*t+a[1])*t+a[2])*t+a[3])*t+a[4])*t+a[5]) /
                (((((b[0]*t+b[1])*t+b[2])*t+b[3])*t+b[4])*t+1.0);
    }
    return x;
}

/// @brief P(X < a, Y < b) for (X, Y) bivariate guassian law with correlation rho.
///        Drezner-Wesolowsky / Genz (BVND) algorthm , Legendre gauss with 20 points.
[[nodiscard]] inline double bivariate_norm_cdf(const double a, const double b, const double rho) noexcept {

    static constexpr int    N_GL = 10;
    static constexpr double GL_X[N_GL] = {
        0.0765265211334973, 0.2277858511416450, 0.3737060887154195,
        0.5108670019508270, 0.6360536807265149, 0.7463319064601508,
        0.8391169718222189, 0.9122344282513259, 0.9639719272779137,
        0.9931285991850949
    };
    static constexpr double GL_W[N_GL] = {
        0.1527533871307256, 0.1491729864726036, 0.1420961093183818,
        0.1316886384491764, 0.1181945319615184, 0.1019301198172403,
        0.0832767415767043, 0.0626720483341093, 0.0406014298003875,
        0.0176140071391527
    };

    const double rho_c = std::clamp(rho, -1.0 + 1e-12, 1.0 - 1e-12);

    if (rho >= 1.0 - 1e-12)  return norm_cdf(std::min(a, b));
    if (rho <= -1.0 + 1e-12) {
        if (a > -b) return norm_cdf(a) - norm_cdf(-b);
        return 0.0;
    }

    double h  = -a;
    double k  = -b;
    double hk = h * k;
    double bvn = 0.0;

    if (std::abs(rho_c) < 0.925) {

        const double asr = std::asin(rho_c);
        for (int i = 0; i < N_GL; ++i) {
            for (int sg : {-1, 1}) {
                const double xs = GL_X[i] * sg;
                const double sn = std::sin(asr * (xs + 1.0) * 0.5);
                const double cs2 = 1.0 - sn * sn;
                bvn += GL_W[i] * std::exp((sn * hk - 0.5*(h*h + k*k)) / cs2);
            }
        }
        bvn = bvn * asr / (4.0 * MathConstants::PI)
            + norm_cdf(-h) * norm_cdf(-k);
    } else {

        if (rho_c < 0.0) { k = -k; hk = -hk; }

        const double ass = (1.0 - rho_c) * (1.0 + rho_c);
        double       a2  = std::sqrt(ass);
        const double bs  = (h - k) * (h - k);
        const double c   = (4.0 - hk) / 8.0;
        const double d_  = (12.0 - hk) / 16.0;
        const double as1 = -(bs / ass + hk) * 0.5;

        if (as1 > -100.0)
            bvn = a2 * std::exp(as1) *
                  (1.0 - c*(bs-ass)*(1.0-d_*bs/5.0)/3.0 + c*d_*ass*ass/5.0);

        if (-hk < 100.0) {
            const double b2 = std::sqrt(bs);
            bvn -= std::exp(-hk * 0.5) * std::sqrt(2.0 * MathConstants::PI)
                 * norm_cdf(-b2/a2) * b2
                 * (1.0 - c*bs*(1.0 - d_*bs/5.0)/3.0);
        }
        a2 *= 0.5;
        for (int i = 0; i < N_GL; ++i) {
            for (int sg : {-1, 1}) {
                const double xs  = a2 * (GL_X[i]*sg + 1.0);
                const double xs2 = xs * xs;
                const double rs  = std::sqrt(1.0 - xs2);
                const double asr = -(bs / xs2 + hk) * 0.5;
                if (asr > -100.0) {
                    const double sp = 1.0 + c * xs2 * (1.0 + d_ * xs2);
                    const double ep = std::exp(-hk * (1.0 - rs) / (2.0 * (1.0 + rs))) / rs;
                    bvn += a2 * GL_W[i] * std::exp(asr) * (ep - sp);
                }
            }
        }
        bvn = -bvn / (2.0 * MathConstants::PI);

        if (rho_c > 0.0)
            bvn += norm_cdf(-std::max(h, k));
        else {
            bvn = -bvn;
            if (k > h) bvn += norm_cdf(k) - norm_cdf(h);
        }
    }

    return std::clamp(bvn, 0.0, 1.0);
}

// Gauss-Hermite with 20 points ∫ e^{-x²} f(x) dx ≈ Σ w_i f(x_i).
inline constexpr int    N_GH = 20;
inline constexpr double GH_NODES[N_GH] = {
    -5.38748089001123, -4.60368244955074, -3.94476404011562,
    -3.34785456235800, -2.78880605842813, -2.25497400208928,
    -1.73853771211659, -1.23407621539532, -0.73747372854539,
    -0.24534070830090,  0.24534070830090,  0.73747372854539,
     1.23407621539532,  1.73853771211659,  2.25497400208928,
     2.78880605842813,  3.34785456235800,  3.94476404011562,
     4.60368244955074,  5.38748089001123
};
inline constexpr double GH_WEIGHTS[N_GH] = {
    2.22939364553415e-13, 4.39934099227318e-10, 1.08606937076928e-07,
    7.80255647853206e-06, 2.28338636016353e-04, 3.24377334223786e-03,
    2.48105208874636e-02, 1.09017206020023e-01, 2.86675505362834e-01,
    4.62243669600610e-01, 4.62243669600610e-01, 2.86675505362834e-01,
    1.09017206020023e-01, 2.48105208874636e-02, 3.24377334223786e-03,
    2.28338636016353e-04, 7.80255647853206e-06, 1.08606937076928e-07,
    4.39934099227318e-10, 2.22939364553415e-13
};

/// @brief E[f(Z)] with Z ~ N(0,1), with 20 points Gauss-Hermite algorithm.
template<typename F>
[[nodiscard]] double gauss_hermite_normal_expectation(F&& f) {
    double sum = 0.0;
    for (int i = 0; i < N_GH; ++i) {
        sum += GH_WEIGHTS[i] * f(MathConstants::SQRT2 * GH_NODES[i]);
    }
    return sum * MathConstants::INV_SQRT_PI;
}

}
