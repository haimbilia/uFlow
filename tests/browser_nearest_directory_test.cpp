#include "browser_path.hpp"
#include <filesystem>
#include <cassert>
#include <iostream>
int main(){
    namespace fs=std::filesystem;
    const auto testRoot=fs::temp_directory_path()/"uflow-nearest-directory";
    fs::create_directories(testRoot);
    fs::current_path(testRoot);
    fs::create_directories("fs:/vol/external01/media/NES/renamed");
    fs::create_directories("usb1:/games");
    const auto exists=[](const std::string &p){return fs::is_directory(p);};
    using BrowserPath::NearestDirectory;
    assert(NearestDirectory("fs:/vol/external01/media/NES/old",exists)=="fs:/vol/external01/media/NES");
    assert(NearestDirectory("fs:/vol/external01/media/NES/old/deeper/",exists)=="fs:/vol/external01/media/NES");
    assert(NearestDirectory("fs:/vol/external01/media/NES/renamed/",exists)=="fs:/vol/external01/media/NES/renamed");
    assert(NearestDirectory("usb1:/games/missing",exists)=="usb1:/games");
    assert(NearestDirectory("usb1:/missing/deeper",exists)=="usb1:/");
    assert(NearestDirectory("fs:/vol/external02/missing",exists)=="fs:/vol");
    assert(NearestDirectory("",exists)=="fs:/vol/external01");
    assert(NearestDirectory("unsupported:/games",exists)=="fs:/vol");
    fs::remove_all("usb1:");
    assert(NearestDirectory("usb1:/missing",exists)=="fs:/");
    std::cout<<"PASS: missing/renamed ancestors, existing paths, SD/USB roots, unmounted volumes, empty paths\n";
}
