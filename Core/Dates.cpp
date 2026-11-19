
#include "Dates.h"
#include <chrono>
#include <cmath>

namespace Core {

long long Date::toJDN(int Y, int M, int D)
{
    const int a = (14 - M) / 12;
    const int y = Y + 4800 - a;
    const int m = M + 12 * a - 3;
    return D
        + (153 * m + 2) / 5
        + 365LL * y
        + y / 4
        - y / 100
        + y / 400
        - 32045;
}

std::tuple<int,int,int> Date::fromJDN(long long jdn)
{
    const long long a = jdn + 32044;
    const long long b = (4 * a + 3) / 146097;
    const long long c = a - (146097 * b) / 4;
    const long long d = (4 * c + 3) / 1461;
    const long long e = c - (1461 * d) / 4;
    const long long m = (5 * e + 2) / 153;

    const int day   = static_cast<int>(e - (153 * m + 2) / 5 + 1);
    const int month = static_cast<int>(m + 3 - 12 * (m / 10));
    const int year  = static_cast<int>(100 * b + d - 4800 + m / 10);

    return { year, month, day };
}

bool Date::isLeapYear(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int Date::daysInMonth(int month, int year)
{
    static constexpr int days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && isLeapYear(year)) return 29;
    return days[month - 1];
}

// ── Constructors ──────────────────────────────────────────────────────────────

Date::Date(const int year, const int month, const int day)
    : m_jdn(toJDN(year, month, day))
{}

Date::Date(const long long jdn)
    : m_jdn(jdn)
{}

Date::Date()
{
    const auto now  = std::chrono::system_clock::now();
    const auto days = std::chrono::floor<std::chrono::days>(now);
    const std::chrono::year_month_day ymd(days);

    const int y = static_cast<int>(ymd.year());
    const int m = static_cast<int>(static_cast<unsigned>(ymd.month()));
    const int d = static_cast<int>(static_cast<unsigned>(ymd.day()));

    m_jdn = toJDN(y, m, d);
}

// ── Accessors ─────────────────────────────────────────────────────────────────

long long Date::getJulianDays() const { return m_jdn; }

std::tuple<int,int,int> Date::toGregorian() const { return fromJDN(m_jdn); }

// ── Arithmetic ────────────────────────────────────────────────────────────────

Date Date::add_days(const int days) const
{
    return Date(m_jdn + days);
}

Date Date::add_months(const int n_months) const
{
    if (n_months == 0) return *this;

    auto [y, m, d] = fromJDN(m_jdn);

    const int total  = (y * 12 + (m - 1)) + n_months;
    int new_year     =  total / 12;
    int new_month    = (total % 12) + 1;

    if (new_month < 1) {
        new_month += 12;
        new_year  -= 1;
    }

    const int dim     = daysInMonth(new_month, new_year);
    const int new_day = (d > dim) ? dim : d;

    return {new_year, new_month, new_day};
}

Date Date::add_months(const double n_months) const
{
    const int    whole    = static_cast<int>(std::trunc(n_months));
    const double fraction = n_months - whole;

    Date result = add_months(whole);

    if (std::abs(fraction) > 1e-9) {
        auto [y, m, d] = fromJDN(result.m_jdn);
        const int dim  = daysInMonth(m, y);
        const int extra = static_cast<int>(std::round(fraction * dim));
        result = result.add_days(extra);
    }

    return result;
}

}