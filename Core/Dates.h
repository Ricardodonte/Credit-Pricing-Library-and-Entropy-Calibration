#pragma once


#include <string>
#include <tuple>

namespace Core {

class Date {

    public:


    Date(int year, int month, int day);
    explicit Date(long long jdn);
    Date();

    [[nodiscard]] long long              getJulianDays() const;
    [[nodiscard]] std::tuple<int,int,int> toGregorian()  const;


    [[nodiscard]] Date add_days  (int days)        const;
    [[nodiscard]] Date remove_days  (int days)  const;
    [[nodiscard]] Date add_months(int n_months)    const;
    [[nodiscard]] Date add_months(double n_months) const;

    long long operator-(const Date& o) const { return m_jdn - o.m_jdn; }

    bool operator==(const Date& o) const { return m_jdn == o.m_jdn; }
    bool operator!=(const Date& o) const { return m_jdn != o.m_jdn; }
    bool operator< (const Date& o) const { return m_jdn <  o.m_jdn; }
    bool operator> (const Date& o) const { return m_jdn >  o.m_jdn; }
    bool operator<=(const Date& o) const { return m_jdn <= o.m_jdn; }
    bool operator>=(const Date& o) const { return m_jdn >= o.m_jdn; }

    static long long               toJDN      (int year, int month, int day);
    static std::tuple<int,int,int> fromJDN    (long long jdn);
    static bool                    isLeapYear (int year);
    static int                     daysInMonth(int month, int year);

    [[nodiscard]] std::string to_string() const;

    static std::tuple<int,int,int> fromJulian(const long long jdn) { return fromJDN(jdn); }


private:

    long long m_jdn;

};

}