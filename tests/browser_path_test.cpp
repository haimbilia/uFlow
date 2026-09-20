#include "browser_path.hpp"
#include <cassert>
#include <iostream>
int main(){
    using namespace BrowserPath;
    assert(Parent("fs:/vol/external01/roms/")=="fs:/vol/external01");
    assert(Parent("fs:/vol/external01")=="fs:/vol");
    assert(Parent("fs:/vol")=="fs:/");
    assert(Parent("fs:/")=="fs:/");
    assert(Child("fs:/","vol")=="fs:/vol");
    assert(Child("fs:/vol/","external02")=="fs:/vol/external02");
    assert(Container("fs:/vol/")&&Container("fs:/")&&!Container("fs:/vol/external02"));
    std::cout<<"PASS: browser parent traversal, root boundary, sibling drives, slash normalization\n";
}
