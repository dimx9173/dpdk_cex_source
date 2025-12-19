/**
 * @file exchange_registry.h
 * @brief Singleton registry for exchange adapters
 */

#ifndef _EXCHANGE_REGISTRY_H_
#define _EXCHANGE_REGISTRY_H_

#include "exchange_adapter.h"
#include <map>
#include <memory>

namespace aero {

/**
 * @brief Singleton registry for exchange adapters
 *
 * Provides runtime lookup of exchange adapters by ExchangeId.
 * Usage:
 *   ExchangeRegistry::instance().register_adapter(id, adapter);
 *   auto* adapter = ExchangeRegistry::instance().get_adapter(id);
 */
class ExchangeRegistry {
public:
  /**
   * @brief Get singleton instance
   */
  static ExchangeRegistry &instance() {
    static ExchangeRegistry registry;
    return registry;
  }

  /**
   * @brief Register an exchange adapter
   * @param id Exchange ID
   * @param adapter Unique pointer to adapter (ownership transferred)
   */
  void register_adapter(ExchangeId id,
                        std::unique_ptr<IExchangeAdapter> adapter) {
    adapters_[id] = std::move(adapter);
  }

  /**
   * @brief Get registered adapter for an exchange
   * @param id Exchange ID
   * @return Pointer to adapter, or nullptr if not registered
   */
  IExchangeAdapter *get_adapter(ExchangeId id) {
    auto it = adapters_.find(id);
    if (it != adapters_.end()) {
      return it->second.get();
    }
    return nullptr;
  }

  /**
   * @brief Check if an adapter is registered
   */
  bool has_adapter(ExchangeId id) const {
    return adapters_.find(id) != adapters_.end();
  }

  /**
   * @brief Get all registered exchange IDs
   */
  std::vector<ExchangeId> get_registered_exchanges() const {
    std::vector<ExchangeId> ids;
    ids.reserve(adapters_.size());
    for (const auto &[id, adapter] : adapters_) {
      ids.push_back(id);
    }
    return ids;
  }

private:
  ExchangeRegistry() = default;
  ExchangeRegistry(const ExchangeRegistry &) = delete;
  ExchangeRegistry &operator=(const ExchangeRegistry &) = delete;

  std::map<ExchangeId, std::unique_ptr<IExchangeAdapter>> adapters_;
};

} // namespace aero

#endif // _EXCHANGE_REGISTRY_H_
