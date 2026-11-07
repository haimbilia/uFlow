#include "diagnostics.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using namespace UFlowSupport;
void Wait(Client &client){for(int i=0;i<500&&client.Poll().busy;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));assert(!client.Poll().busy);}
int main(){
    assert(Quote("\"\n\\")=="\"\\\"\\u000a\\\\\"");
    const auto filtered=Redact("safe result=29\nopen usb1:/Private Name/game.wbfs\npeer 192.168.1.150\nmail a@b.com\n");
    assert(filtered.find("safe result=29")!=std::string::npos);
    assert(filtered.find("Private")==std::string::npos&&filtered.find("192.168")==std::string::npos&&filtered.find("a@b")==std::string::npos);
    auto root=std::filesystem::temp_directory_path()/"uflow-support-test";
    std::filesystem::create_directories(root);
    std::ofstream(root/"uflow.cfg")<<"theme=2\nmusic_path=private_name\n";
    std::ofstream(root/"wii-launch.log")<<"error=29\nopen sd:/private/game.wbfs\n";
    std::ofstream(root/"content_sources.cfg")<<"wii\t1\t3\tusb1:/Private Games\twiiflow\twii\twiiflow\t0\n";
    std::ofstream(root/"account.dat")<<"DO NOT COLLECT";
    const auto watchRoot=root/"watch";std::filesystem::remove_all(watchRoot);std::filesystem::create_directories(watchRoot);
    { FreezeWatch watch(watchRoot.string(),100);assert(!watch.Pending());watch.Active(true);
      for(int i=0;i<8;++i){watch.Beat(2);std::this_thread::sleep_for(std::chrono::milliseconds(40));}
      assert(!std::filesystem::exists(watchRoot/"freeze-pending"));
      watch.Active(false);std::this_thread::sleep_for(std::chrono::milliseconds(350));
      assert(!std::filesystem::exists(watchRoot/"freeze-pending"));
      watch.Active(true);watch.Beat(2);std::this_thread::sleep_for(std::chrono::milliseconds(600));
      assert(std::filesystem::exists(watchRoot/"freeze-pending")); }
    { FreezeWatch watch(watchRoot.string());assert(watch.Pending());watch.Acknowledge();assert(!watch.Pending()); }
    assert(std::filesystem::exists(watchRoot/"freeze-report.log"));
    { FreezeWatch watch(watchRoot.string());watch.Acknowledge();watch.Active(true);watch.Active(false);watch.CleanExit(); }
    assert(!std::filesystem::exists(watchRoot/"session-active"));
    { FreezeWatch watch(watchRoot.string(),100);watch.Active(true);
      watch.Transition("HOME media workers retired; foreground handoff pending");
      watch.Active(false);std::this_thread::sleep_for(std::chrono::milliseconds(350));
      assert(std::filesystem::exists(watchRoot/"session-active"));
      assert(!std::filesystem::exists(watchRoot/"freeze-pending")); }
    { FreezeWatch watch(watchRoot.string());assert(watch.Pending());
      std::ifstream file(watchRoot/"freeze-report.log");std::string text((std::istreambuf_iterator<char>(file)),{});
      assert(text.find("foreground handoff pending")!=std::string::npos);
      watch.Acknowledge();watch.CleanExit(); }
    { FreezeWatch watch(watchRoot.string());assert(!watch.Pending()); }
    std::ofstream(watchRoot/"session-active")<<"active\n";
    { FreezeWatch watch(watchRoot.string());assert(watch.Pending());watch.Acknowledge(); }
    std::ifstream recovery(watchRoot/"freeze-report.log");std::string incident((std::istreambuf_iterator<char>(recovery)),{});
    assert(incident.find("power loss possible")!=std::string::npos);
    { FreezeWatch watch(watchRoot.string());watch.Active(false);
      UFlow_HandoffStage(1);UFlow_HandoffStage(5);UFlow_HandoffStage(6);
      std::this_thread::sleep_for(std::chrono::milliseconds(600));
      std::ifstream in(watchRoot/"handoff.log");std::string log((std::istreambuf_iterator<char>(in)),{});
      assert(log.find("draw-release begin")!=std::string::npos);
      assert(log.find("release GPU wait begin")!=std::string::npos); }
    // An old unsent report must not conceal evidence from a newer failed session.
    std::ofstream(watchRoot/"freeze-report.log")<<"older incident";
    std::ofstream(watchRoot/"freeze-pending")<<"1";
    std::ofstream(watchRoot/"session-active")<<"transition newer incident";
    { FreezeWatch watch(watchRoot.string());assert(watch.Pending());
      std::ifstream in(watchRoot/"freeze-report.log");std::string log((std::istreambuf_iterator<char>(in)),{});
      assert(log.find("newer incident")!=std::string::npos);
      assert(log.find("release GPU wait begin")!=std::string::npos);watch.CleanExit(); }
    std::ofstream(root/"freeze-report.log")<<"Prior freeze evidence\n";
    Client client(root.string());assert(!client.Poll().recording);
    client.Event("not recording");assert(!client.Poll().busy);
    client.Record(true);for(int i=0;i<100;++i)client.Event("event "+std::to_string(i));client.Event("path sd:/secret");
    client.Send(3,1,2);Wait(client);assert(client.Poll().saved);
    std::ifstream input(root/"support-report.json");std::string body((std::istreambuf_iterator<char>(input)),{});
    for(const char *secret:{"private_name","Private Games","secret","DO NOT COLLECT","not recording","event 0\\"})assert(body.find(secret)==std::string::npos);
    assert(body.find("Prior freeze evidence")!=std::string::npos);
    assert(body.find("FAT32 USB")!=std::string::npos&&body.find("theme=2")!=std::string::npos&&body.find("error=29")!=std::string::npos);
    assert(client.Poll().message.find("Send failed. Saved: SD:/wiiu/apps/uFlow/support-report.json")!=std::string::npos);
    client.Record(false);client.Send(4,1,2);Wait(client);assert(client.Poll().saved); // Replacement works.
    Client failed((root/"missing").string());failed.Send(0,0,0);Wait(failed);assert(!failed.Poll().saved);
    std::cout<<"PASS redaction, allowlist, bounded events, consent precondition, offline preservation, replace and write failure\n";
}
