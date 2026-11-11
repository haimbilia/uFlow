#include "updater.hpp"
#include <curl/curl.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
#include <filesystem>
std::atomic<unsigned> requests{0};
extern "C" CURLcode __wrap_curl_easy_perform(CURL*){++requests;return CURLE_COULDNT_CONNECT;}
void Wait(UFlowUpdate::Client&c){for(int i=0;i<1000&&c.Poll().state==UFlowUpdate::State::Checking;++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));assert(c.Poll().state==UFlowUpdate::State::Idle);c.Stop();}
int main(){
    assert(!std::filesystem::exists("fs:/vol/external01"));
    std::filesystem::create_directories("fs:/vol/external01/wiiu/apps/uFlow");
    {UFlowUpdate::Client c;c.Check(false);Wait(c);assert(requests==0);}
    {UFlowUpdate::Client c;c.Check(true);Wait(c);assert(requests==1);}
    std::cout<<"PASS disabled startup checks make zero HTTP requests; enabled checks reach the transport\n";
}
