// Web build: OrcSDR's scan engine drives the device's tuner. The page hunts a band by reading one wide
// spectrum instead (sw_controller.cpp), so only the type names the Hunt header mentions are needed.
#pragma once
namespace orcsdr::scan { class Engine; }
