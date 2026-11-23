#pragma once
namespace orcsdr::rf_analysis {
// One pass of the analysis worker; the controller calls it from the page's timer (there are no tasks in the page).
void pump();
}  // namespace orcsdr::rf_analysis
