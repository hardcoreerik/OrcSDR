#include "lora_history.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <unistd.h>
int main(){
 const auto root=std::filesystem::path("/tmp")/("orc-zero-lora-history-"+std::to_string(getpid()));
 const auto file=root/"packets.tsv";orcsdr::lora_native::Packet packet{};packet.sender=0x1234;packet.destination=0x5678;packet.port=1;
 std::snprintf(packet.text,sizeof(packet.text),"HELLO\tWORLD");assert(orc::zero::lora_history::append(file,packet));
 packet.sender=0xABCD;packet.text[0]=0;packet.encrypted=true;assert(orc::zero::lora_history::append(file,packet));
 const auto lines=orc::zero::lora_history::load(file);assert(lines.size()==4);assert(lines[0].find("0000ABCD")!=std::string::npos);
 assert(lines[1]=="ENCRYPTED PACKET");assert(lines[3]=="HELLO?WORLD");
 std::filesystem::remove_all(root);std::cout<<"LoRa packet history append/reload/sanitize PASS\n";
}
