#include "tecflix/telemetry/telemetry.hpp"

namespace tecflix::telemetry {
struct Telemetry::Impl { NoOpHttpMetrics http; };
Telemetry::Telemetry(const Config&) : impl_(std::make_unique<Impl>()) {}
Telemetry::~Telemetry() = default;
HttpMetrics& Telemetry::Http() { return impl_->http; }
bool Telemetry::ForceFlush() { return true; }
}  // namespace tecflix::telemetry