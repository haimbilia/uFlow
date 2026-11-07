#include "cover_faces.hpp"
#include <cassert>
#include <set>
int main(){
 GameEntry g;g.fullCoverPath="full";g.coverPath="front";g.backCoverPath="back";g.spinePath="spine";g.defaultSleevePath="default";
 std::set<std::string> known{""},available;
 auto k=[&](const auto&p){return known.contains(p);};auto a=[&](const auto&p){return available.contains(p);};
 assert(!CoverFaces::Resolved(g,k,a));known.insert("full");available.insert("full");assert(CoverFaces::Resolved(g,k,a));
 available.clear();known.insert({"front","back","spine"});available.insert({"front","back","spine"});assert(CoverFaces::Resolved(g,k,a));
 available.erase("back");assert(!CoverFaces::Resolved(g,k,a));known.insert("default");assert(CoverFaces::Resolved(g,k,a));
 known.erase("front");assert(!CoverFaces::Resolved(g,k,a));
}
