#include "media_groups.hpp"
#include <cassert>
#include <iostream>

int main() {
    std::vector<LibraryDefinition> libraries={{"wiiu","Wii U"},{"wii","Wii"},{"gc","GameCube"},{"ds","Nintendo DS"}};
    std::vector<MediaSource> sources={
        {MediaType::Preview,-1,"/wii/video",true,0,"wii"},
        {MediaType::Cover,-1,"/shared",true,0},
        {MediaType::FullCover,0,"/wiiu/legacy",true,0},
        {MediaType::Logo,-1,"/wii/logo",false,0,"wii"},
        {MediaType::Cover,-1,"/ds",true,0,"ds"},
        {MediaType::Metadata,-1,"/unknown",true,0,"retired"},
        {MediaType::Cover,-1,"",true,0,"gc"}};
    auto rows=MediaDirectoryRows(sources,libraries,{});
    assert(rows.size()==8); // five populated groups and three actions
    assert(rows[0].key.empty() && rows[0].directories==1);
    assert(rows[1].key=="wiiu" && rows[1].directories==1);
    assert(rows[2].key=="wii" && rows[2].directories==2);
    assert(rows[3].key=="ds" && rows[3].label=="Nintendo DS");
    assert(rows[4].key=="retired");
    for(const auto &row:rows) assert(row.kind!=MediaDirectoryRow::Directory && row.key!="gc");
    rows=MediaDirectoryRows(sources,libraries,{"wii","wiiu","gc"});
    assert(rows.size()==11);
    assert(rows[2].source==2); // legacy Wii U index, not its visible row
    assert(rows[4].source==0 && rows[5].source==3); // both media kinds, disabled still saved
    assert(rows[8].kind==MediaDirectoryRow::Add && rows[10].kind==MediaDirectoryRow::Help);
    sources[0].libraryId="ds";
    rows=MediaDirectoryRows(sources,libraries,{"ds"});
    const auto ds=std::find_if(rows.begin(),rows.end(),[](const auto &r){return r.key=="ds";});
    assert(ds!=rows.end() && ds->directories==2 && (ds+1)->source==0 && (ds+2)->source==4);
    sources.erase(sources.begin()+3); // last saved Wii directory removed
    rows=MediaDirectoryRows(sources,libraries,{"wii"});
    for(const auto &row:rows) assert(row.key!="wii");
    libraries[3].name="DS Renamed";
    rows=MediaDirectoryRows(sources,libraries,{"ds"});
    assert(std::any_of(rows.begin(),rows.end(),[](const auto &r){return r.label=="DS Renamed" && r.expanded;}));
    rows=MediaDirectoryRows({},libraries,{"wii"});
    assert(rows.size()==3 && rows[0].kind==MediaDirectoryRow::Add);
    std::cout<<"PASS: media accordion populated groups only, shared/legacy/custom/orphan IDs, source indices, disabled dirs, reassignment, deletion, rename and empty list\n";
}
