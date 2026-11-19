#pragma once


#include <tuple>

namespace Core {

class Date {
public:

    // ── Constructors ────────────────────────────────────────────────────────

    Date(int year, int month, int day);  // from Gregorian date
    explicit Date(long long jdn);        // from Julian Day Number directly
    Date();                              // today (system clock)

    // ── Accessors ───────────────────────────────────────────────────────────

    [[nodiscard]] long long              getJulianDays() const;
    [[nodiscard]] std::tuple<int,int,int> toGregorian()  const; //

    // ── Arithmetic ──────────────────────────────────────────────────────────

    [[nodiscard]] Date add_days  (int days)        const;
    [[nodiscard]] Date add_months(int n_months)    const;
    [[nodiscard]] Date add_months(double n_months) const;

    // ── Operators ───────────────────────────────────────────────────────────
    long long operator-(const Date& o) const { return m_jdn - o.m_jdn; }

    bool operator==(const Date& o) const { return m_jdn == o.m_jdn; }
    bool operator!=(const Date& o) const { return m_jdn != o.m_jdn; }
    bool operator< (const Date& o) const { return m_jdn <  o.m_jdn; }
    bool operator> (const Date& o) const { return m_jdn >  o.m_jdn; }
    bool operator<=(const Date& o) const { return m_jdn <= o.m_jdn; }
    bool operator>=(const Date& o) const { return m_jdn >= o.m_jdn; }

    // ── Static helpers ───────────────────────────────────────────────────────
    static long long               toJDN      (int year, int month, int day);
    static std::tuple<int,int,int> fromJDN    (long long jdn);
    static bool                    isLeapYear (int year);
    static int                     daysInMonth(int month, int year);

    // Legacy alias for existing test code
    static std::tuple<int,int,int> fromJulian(long long jdn) { return fromJDN(jdn); }

private:
    long long m_jdn;
};

} // namespace Core