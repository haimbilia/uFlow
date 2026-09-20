#include "browser_path.hpp"
#include "library.hpp"
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
    std::vector<std::string> names;
    AddNamespaceEntries("fs:/",names,[](const auto &){return false;});
    assert(names==std::vector<std::string>{"vol"});
    names={"external01","custom_mount"};
    AddNamespaceEntries("fs:/vol",names,[](const auto &p){return p=="fs:/vol/external01"||p=="fs:/vol/external02";});
    assert((names==std::vector<std::string>{"external01","custom_mount","external02"}));
    names.clear();AddNamespaceEntries("fs:/vol",names,[](const auto &){return false;});assert(names.empty());
    const auto roots=ListDirectories("fs:/");
    assert(std::find(roots.begin(),roots.end(),"vol")!=roots.end());
    const auto mediaRoots=ListMediaFiles("fs:/",MediaType::FullCover);
    assert(std::find(mediaRoots.begin(),mediaRoots.end(),"vol/")!=mediaRoots.end());
    std::cout<<"PASS: browser parent traversal, root boundary, sibling drives, slash normalization\n";
}
