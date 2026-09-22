#include "shortwave_dashboard_state.hpp"

namespace orcsdr::shortwave {

void DashboardState::open(Modal modal) { modal_ = modal; }
void DashboardState::close_modal() { modal_ = Modal::none; }

}  // namespace orcsdr::shortwave
