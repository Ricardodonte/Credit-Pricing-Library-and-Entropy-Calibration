// Binding of an index composition (contract) to the credit curves of its names (market data).
// Holds non-owning pointers to the curves: they must outlive the pool. The pool is only read while a
// PreparedTranche is being built, which keeps the marginal default probabilities it needs, not the pool.

#pragma once
#include <cstddef>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "Core/Dates.h"
#include "Instruments/Credit/CreditIndex.h"

namespace Pricing::Credit {

    template <class CreditCurveT>
    class ReferencePool {
    public:
        /// @param curves credit curves of the constituents, in the order of the index.
        ReferencePool(Instruments::Credit::CreditIndex index, std::span<const CreditCurveT> curves)
            : index_(std::move(index)) {
            if (curves.size() != index_.size())
                throw std::invalid_argument("ReferencePool: one credit curve per index constituent is required");
            curves_.reserve(curves.size());
            loss_given_default_.reserve(curves.size());
            for (std::size_t i = 0; i < curves.size(); ++i) {
                curves_.push_back(&curves[i]);
                loss_given_default_.push_back(index_[i].weight * (1.0 - index_[i].recovery_rate));
            }
        }

        [[nodiscard]] const Instruments::Credit::CreditIndex& index() const noexcept { return index_; }
        [[nodiscard]] std::size_t size() const noexcept { return curves_.size(); }
        [[nodiscard]] const CreditCurveT& curve(std::size_t i) const noexcept { return *curves_[i]; }

        /// @brief l_i = w_i (1 - R_i): loss on the index notional when name i defaults.
        [[nodiscard]] std::span<const double> loss_given_default() const noexcept { return loss_given_default_; }

        /// @brief Writes P(tau_i <= d) for every name into `out` (size() elements).
        void default_probabilities(Core::Date d, std::span<double> out) const noexcept {
            for (std::size_t i = 0; i < curves_.size(); ++i) out[i] = curves_[i]->default_probability(d);
        }

        /// @brief True when every curve is built as of `d`.
        [[nodiscard]] bool all_curves_as_of(Core::Date d) const noexcept {
            for (const auto* c : curves_)
                if (c->reference_date() != d) return false;
            return true;
        }

    private:
        Instruments::Credit::CreditIndex index_;
        std::vector<const CreditCurveT*> curves_;
        std::vector<double> loss_given_default_;
    };

    // Deduction guides: ReferencePool(index, std::span(curves)) or ReferencePool(index, curves) for a vector.
    template <class C, std::size_t Extent>
    ReferencePool(Instruments::Credit::CreditIndex, std::span<C, Extent>) -> ReferencePool<std::remove_const_t<C>>;
    template <class C>
    ReferencePool(Instruments::Credit::CreditIndex, const std::vector<C>&) -> ReferencePool<C>;

} // namespace Pricing::Credit
