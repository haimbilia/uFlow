#define UFLOW_HOST_PREVIEW
#include "launch_diagnostics.hpp"
#include <cassert>
int main(){
 using namespace LaunchDiagnostics;
 assert(XmlValue("<app><title_version type=\"hexBinary\" length=\"2\">00d0</title_version></app>","title_version")=="00d0");
 assert(XmlValue("<title_id_extra>wrong</title_id_extra><title_id>00050000101c9400</title_id>","title_id")=="00050000101c9400");
 assert(XmlValue("<title_version>broken","title_version")=="<invalid>");
 assert(XmlValue("<app/>","title_id")=="<absent>");
 assert(FsPath("/vol/storage_usb01/usr/title/test")=="fs:/vol/storage_usb01/usr/title/test");
 assert(FsPath("usb1:/games/code")=="usb1:/games/code");
 assert(FsPath("games/BOTW/code")=="fs:/vol/external01/games/BOTW/code");
}
