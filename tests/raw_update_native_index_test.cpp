#include "RawAddonNativePath.h"
#include <cassert>
int main() {
 RawUpdateFileIndex index;
 assert(index.Build("fs:/vol/external01/update","update"));
 assert(index.Size()==1 && index.Contains("update/sub/movie.bin"));
 assert(opened==2 && closed==2 && deleted==1 && reads==4);
 rootNext=subNext=opened=closed=reads=deleted=0;failAt=3;
 assert(!index.Build("fs:/vol/external01/update","update"));
 assert(index.Size()==0 && opened==2 && closed==2 && deleted==1);
}
