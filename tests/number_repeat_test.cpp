#include "../app/source/number_repeat.hpp"
#include "../app/source/prepared_models.hpp"
#include <cassert>
int main(){
 NumberRepeat::Hold h;
 assert(h.Update(1,1,100)==1);assert(h.Update(1,1,459)==0);assert(h.Update(1,1,460)==1);assert(h.Update(1,1,559)==0);assert(h.Update(1,1,560)==1);
 assert(h.Update(-1,1,561)==-1);assert(h.Update(-1,2,562)==-1);assert(h.Update(0,2,563)==0);assert(h.Update(-1,2,564)==-1);
 h.Reset();assert(h.Update(1,1,0xffffff00)==1);assert(h.Update(1,1,103)==0);assert(h.Update(1,1,104)==1);
 for(auto id:{"nes","snes","gba","gb","n64","ds"})assert(PreparedModels::For(id).media==id);
 assert(PreparedModels::For("custom","",".gb,.gbc").media=="gb");
 assert(PreparedModels::For("gamecube").media=="mini-disc");assert(PreparedModels::For("ps1").box=="ps1-jewel");
 assert(PreparedModels::For("unknown").media=="auto");
}
