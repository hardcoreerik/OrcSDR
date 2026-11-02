#pragma once
namespace orcsdr::c6_recovery_ui {
enum class Phase {idle,quiescing,connecting,release_boot,writing,verifying,restarting,complete,failed,release_boot_failure};
enum class Request {none,begin,install,resume,cancel};
struct View {const char* title;const char* instructions;const char* button;Request request;bool cancel=false;};
inline View view(Phase phase,bool busy,bool image,bool current=false) {
  if(phase==Phase::release_boot) return {"CONNECTED","Release physical C6 BOOT now. Then tap Install now to confirm release, write the embedded firmware and verify it.","INSTALL NOW",Request::install,true};
  if(phase==Phase::release_boot_failure) return {"RELEASE BOOT","Connection failed or was canceled. Release physical C6 BOOT, then tap Released: resume. No firmware will be written.","RELEASED: RESUME",Request::resume};
  if(phase==Phase::quiescing || phase==Phase::connecting) return {"CONNECTING","Keep holding physical C6 BOOT. Preparing the internal link and connecting to ROM.","CANCEL",Request::cancel};
  if(phase==Phase::writing || phase==Phase::verifying) return {"UPDATING C6","Keep power connected. Writing and verifying the embedded C6 firmware. Do not press RESET.","UPDATING...",Request::none};
  if(phase==Phase::restarting || busy) return {"RESTARTING C6","Keep power connected while the C6 restarts and Wi-Fi resumes.","PLEASE WAIT",Request::none};
  if(phase==Phase::complete) return {"C6 UPDATE COMPLETE","Firmware installed and verified. The C6 has restarted and is ready. You can leave this page.","",Request::none};
  if(phase==Phase::idle && current) return {"C6 UP TO DATE","The C6 is running the firmware included in this build. No update is needed.","",Request::none};
  if(!image) return {"IMAGE UNAVAILABLE","This build does not include a qualified C6 recovery package.","UNAVAILABLE",Request::none};
  return {phase==Phase::failed?"RECOVERY FAILED":"C6 INSTALL / RECOVERY",
    "Hold physical C6 BOOT, then tap Update now. Keep BOOT held until connected. Release it only when prompted. Keep power connected.","UPDATE NOW",Request::begin};
}
} // namespace orcsdr::c6_recovery_ui
