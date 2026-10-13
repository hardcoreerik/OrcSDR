#pragma once
// Deterministic, explicitly fabricated ACARS records for the dashboard's DEMO mode and for host tests.
// Everything here is synthetic: tails, flights and text are invented, every record is tagged
// SourceKind::demo by Model::inject_demo, and nothing here can reach the live store, an archive or a feed.
#include "acars_dashboard_model.hpp"

namespace orcsdr::acars_demo {

// Fills the model's demo store for `scenario`. Only valid while the model is in demo mode.
void populate(acars_ui::Model& model, acars_ui::DemoScenario scenario);
const char* scenario_name(acars_ui::DemoScenario scenario);

}  // namespace orcsdr::acars_demo
