// Generic coupon schedule generation for generic derivatives .
// CDS schedules follow IMM-specific rules and are built in Instruments/Credit/Cds.h.

#pragma once
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "Dates.h"
#include "Period.h"
#include "calendar.h"

namespace Core {

    enum class DateGenerationRule {
        Forward,   /// from the effective date forward, stub at the end
        Backward   /// from the termination date backward, stub at the start
    };

    /// @brief Strictly increasing list of (adjusted) schedule dates. Immutable once built.
    class Schedule {
    public:
        explicit Schedule(std::vector<Date> dates) : dates_(std::move(dates)) {
            if (dates_.size() < 2)
                throw std::invalid_argument("Schedule: at least two dates are required");
            for (std::size_t i = 1; i < dates_.size(); ++i) {
                if (!(dates_[i - 1] < dates_[i]))
                    throw std::invalid_argument("Schedule: dates must be strictly increasing");
            }
        }

        [[nodiscard]] std::span<const Date> dates() const noexcept { return dates_; }
        [[nodiscard]] std::size_t size() const noexcept { return dates_.size(); }
        [[nodiscard]] Date operator[](std::size_t i) const noexcept { return dates_[i]; }
        [[nodiscard]] Date front() const noexcept { return dates_.front(); }
        [[nodiscard]] Date back() const noexcept { return dates_.back(); }

    private:
        std::vector<Date> dates_;
    };

    /// @brief Builds a generic schedule between effective and termination dates.
    ///        Dates are generated from an anchor (k * tenor, never iteratively, to avoid end-of-month
    ///        drift), then adjusted: `convention` for all dates but the last, `termination_convention`
    ///        for the last one. Dates that collapse after adjustment are dropped.
    template <class Cal>
        requires std::derived_from<Cal, CalendarBase<Cal>>
    [[nodiscard]] Schedule make_schedule(Date effective, Date termination, Period tenor, const Cal& calendar,
                                         BusinessDayConvention convention = BusinessDayConvention::ModifiedFollowing,
                                         BusinessDayConvention termination_convention = BusinessDayConvention::ModifiedFollowing,
                                         DateGenerationRule rule = DateGenerationRule::Backward,
                                         bool end_of_month = false) {
        if (!(effective < termination))
            throw std::invalid_argument("make_schedule: effective date must be before termination date");
        if (tenor.length() <= 0)
            throw std::invalid_argument("make_schedule: tenor must be strictly positive");

        std::vector<Date> unadjusted;
        if (rule == DateGenerationRule::Backward) {
            unadjusted.push_back(termination);
            for (int k = 1;; ++k) {
                const Date d = add_period(termination, tenor * -k, end_of_month);
                if (d <= effective) break;
                unadjusted.push_back(d);
            }
            unadjusted.push_back(effective);
            std::ranges::reverse(unadjusted);
        } else {
            unadjusted.push_back(effective);
            for (int k = 1;; ++k) {
                const Date d = add_period(effective, tenor * k, end_of_month);
                if (d >= termination) break;
                unadjusted.push_back(d);
            }
            unadjusted.push_back(termination);
        }

        std::vector<Date> adjusted;
        adjusted.reserve(unadjusted.size());
        for (std::size_t i = 0; i < unadjusted.size(); ++i) {
            const bool is_last = (i + 1 == unadjusted.size());
            const Date a = calendar.adjust(unadjusted[i], is_last ? termination_convention : convention);
            if (!adjusted.empty() && a <= adjusted.back()) {
                if (is_last) adjusted.back() = a;  //
                continue;
            }
            adjusted.push_back(a);
        }
        return Schedule(std::move(adjusted));
    }

} // namespace Core
