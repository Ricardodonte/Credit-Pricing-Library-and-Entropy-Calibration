// Composition of a credit index (or any bespoke reference portfolio): names, weights and recoveries.
// Pure contract data: the credit curves of the names are market data and are bound at pricing time.

#pragma once
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Instruments::Credit {

    struct IndexConstituent {
        std::string name;
        double weight;         /// fraction of the index notional, the weights sum to 1
        double recovery_rate;  /// assumed recovery at default, in [0, 1[
    };

    class CreditIndex {
    public:
        CreditIndex(std::string name, std::vector<IndexConstituent> constituents)
            : name_(std::move(name)), constituents_(std::move(constituents)) {
            if (constituents_.empty())
                throw std::invalid_argument("CreditIndex: an index needs at least one constituent");
            double total = 0.0;
            for (const auto& c : constituents_) {
                if (!(c.weight > 0.0))
                    throw std::invalid_argument("CreditIndex: weight of " + c.name + " must be positive");
                if (!(c.recovery_rate >= 0.0 && c.recovery_rate < 1.0))
                    throw std::invalid_argument("CreditIndex: recovery of " + c.name + " must lie in [0, 1[");
                total += c.weight;
            }
            if (std::abs(total - 1.0) > 1e-10)
                throw std::invalid_argument("CreditIndex: constituent weights must sum to 1");
        }

        /// @brief n names with equal weights 1/n and the same recovery (iTraxx / CDX style).
        [[nodiscard]] static CreditIndex homogeneous(std::string name, std::size_t n, double recovery_rate) {
            std::vector<IndexConstituent> constituents;
            constituents.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
                constituents.push_back({name + " #" + std::to_string(i + 1), 1.0 / static_cast<double>(n), recovery_rate});
            return {std::move(name), std::move(constituents)};
        }

        [[nodiscard]] const std::string& name() const noexcept { return name_; }
        [[nodiscard]] std::size_t size() const noexcept { return constituents_.size(); }
        [[nodiscard]] const std::vector<IndexConstituent>& constituents() const noexcept { return constituents_; }
        [[nodiscard]] const IndexConstituent& operator[](std::size_t i) const noexcept { return constituents_[i]; }

    private:
        std::string name_;
        std::vector<IndexConstituent> constituents_;
    };

} // namespace Instruments::Credit
