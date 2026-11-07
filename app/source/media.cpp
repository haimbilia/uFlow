#include "library.hpp"
#include "media_filename.hpp"
#include "media_name_match.hpp"
#include "library_tabs.hpp"
#include "browser_path.hpp"
#include "directory_entry.hpp"
#include "saved_file.hpp"
#include "../vendor/picojson/picojson.h"
#include <cerrno>
#include "rom_art.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <dirent.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <sys/stat.h>

namespace {
constexpr const char *kRoot = "fs:/vol/external01/wiiu/apps/uFlow";
constexpr const char *kConfig = "fs:/vol/external01/wiiu/apps/uFlow/media_sources.cfg";
constexpr const char *kRefresh = "fs:/vol/external01/wiiu/apps/uFlow/media-refresh.pending";
constexpr std::array<const char *,kMediaTypeCount> kKinds{"covers","fullcovers","backgrounds","logos","previews","metadata","back","spine","media","labels"};
std::string Lower(std::string s) {
    for (auto &c:s) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string Trim(std::string s) {
    const auto a=s.find_first_not_of(" \r\n\t"), b=s.find_last_not_of(" \r\n\t");
    return a==std::string::npos ? "" : s.substr(a,b-a+1);
}
std::string Key(std::string s) {
    s=Lower(s);
    const auto region=s.find_last_of('(');
    if (region!=std::string::npos && !s.empty() && s.back()==')') s.resize(region);
    std::string out;
    for (unsigned char c:s) {
        if (std::isalnum(c)) out+=static_cast<char>(c);
        else if (!out.empty() && out.back()!='_') out+='_';
    }
    if (!out.empty() && out.back()=='_') out.pop_back();
    return out;
}
struct Asset { int library; std::string path; std::string libraryId; std::string name,compact; };
using Index=std::array<std::map<std::string,std::vector<Asset>>,kMediaTypeCount>;
bool ExtensionMatches(MediaType type,const std::string &extension) {
    if (type==MediaType::Metadata) return extension=="json" || extension=="ini";
    if (type==MediaType::Preview) return extension=="mp4" || extension=="avi" || extension=="mov";
    return extension=="png" || extension=="tga" || extension=="jpg" || extension=="jpeg";
}
std::string DefaultPath(const MediaSource &source) {
    if(source.defaultFile.empty()) return {};
    if(source.defaultFile.front()=='/' || source.defaultFile.find(':')!=std::string::npos) return source.defaultFile;
    return source.path+"/"+source.defaultFile;
}
std::string FindAsset(const std::vector<Asset> &assets,const GameEntry &game) {
    auto wanted=MediaNameMatch::RegionMask(game.name);
    if(!wanted)wanted=MediaNameMatch::RegionMask(game.region,false);
    int bestScope=-1,bestRegion=-1;std::string result;
    for(const auto &asset:assets){
        const int scope=!asset.libraryId.empty()?(asset.libraryId==game.libraryId?2:-1):(asset.library==int(game.platform)?1:asset.library==-1?0:-1);
        const int region=MediaNameMatch::RegionRank(wanted,MediaNameMatch::RegionMask(asset.name));
        if(scope<0||region<0)continue;
        if(scope>bestScope||(scope==bestScope&&region>bestRegion)){bestScope=scope;bestRegion=region;result=asset.path;}
    }
    return result;
}
void IndexDirectory(Index &index,const MediaSource &source,const std::string &path,int depth,const ScanObserver &progress,std::size_t games) {
    if(progress)progress({"INDEXING ARTWORK",path,games,0,0});
    DirectoryHandle dir(opendir(path.c_str()),closedir);
    if (!dir) return; // missing/unplugged paths are retained in settings, ignored by scan
    std::vector<std::pair<std::string,mode_t>> names;
    while (auto *entry=readdir(dir.get())) {
        std::string name=entry->d_name;
        if (name!="." && name!="..") {
            if(progress)progress({"INDEXING ARTWORK",path+"/"+name,games,0,0});
            names.emplace_back(name,DirectoryEntryMode(*entry,path+"/"+name));
        }
    }
    dir.reset();
    std::sort(names.begin(),names.end()); // stable duplicate precedence
    for (const auto &[name,mode]:names) {
        const auto full=path+"/"+name;
        if(progress)progress({"INDEXING ARTWORK",full,games,0,0});
        if (S_ISDIR(mode)) {
            if (depth>0) IndexDirectory(index,source,full,depth-1,progress,games);
        } else if (S_ISREG(mode)) {
            if(full==DefaultPath(source)) continue; // A chosen fallback is not a game-specific match.
            const auto dot=name.find_last_of('.');
            if (dot==std::string::npos || !ExtensionMatches(source.type,Lower(name.substr(dot+1)))) continue;
            const auto stem=name.substr(0,dot), id=MediaFilename::Id(stem);
            // Named downloads retain an exact ID so different releases never share artwork.
            const auto key=Key(id.empty()?stem:id);
            if (!key.empty()) {
                Asset asset{source.library,full,source.libraryId};
                if(id.empty()&&!MediaFilename::TitleId(stem)){
                    asset.name=Lower(stem);asset.compact=MediaNameMatch::Compact(Key(stem));
                }
                index[static_cast<int>(source.type)][key].push_back(std::move(asset));
            }
        }
    }
}
std::string Find(const Index &index,MediaType type,const GameEntry &game) {
    for (const auto &key:{Key(game.titleId),Key(game.name)}) {
        if (key.empty()) continue;
        const auto found=index[static_cast<int>(type)].find(key);
        if (found==index[static_cast<int>(type)].end()) continue;
        // ID before name; platform-specific before shared; then source-list order.
        const auto path=FindAsset(found->second,game);
        if(!path.empty()) return path;
    }
    const auto query=MediaNameMatch::SearchTitle(game.name,game.publisher,game.developer);
    const auto compact=MediaNameMatch::Compact(Key(query));
    const auto edition=MediaNameMatch::Edition(query);
    auto wantedRegion=MediaNameMatch::RegionMask(game.name);
    if(!wantedRegion)wantedRegion=MediaNameMatch::RegionMask(game.region,false);
    int best=0,second=0,bestScope=-1;std::string winner,name;
    for(const auto&[key,assets]:index[static_cast<int>(type)])for(const auto&asset:assets){
        if(asset.compact.empty())continue; // An explicit title ID is authoritative, never fuzzy.
        const int scope=!asset.libraryId.empty()?(asset.libraryId==game.libraryId?2:-1):(asset.library==int(game.platform)?1:asset.library==-1?0:-1);
        if(scope<0)continue;
        if(MediaNameMatch::RegionRank(wantedRegion,MediaNameMatch::RegionMask(asset.name))<0)continue;
        const int score=MediaNameMatch::Score(compact,asset.compact);if(score<89)continue;
        if(asset.name==name){if(scope>bestScope){winner=asset.path;bestScope=scope;}continue;}
        if(score>best){second=best;best=score;name=asset.name;winner=asset.path;bestScope=scope;}
        else second=std::max(second,score);
    }
    if(best<94||best-second<6)return {};
    // Check the winning original name, not its region/punctuation-normalized key.
    if(MediaNameMatch::Edition(name)!=edition)return {};
    return winner;
}
void Metadata(GameEntry &game,const std::string &path,bool missingOnly=false) {
    std::ifstream input(path,std::ios::binary);
    if(Lower(path).ends_with(".json")) {
        // Bound external metadata before parsing; malformed input leaves fields intact.
        input.seekg(0,std::ios::end);const auto size=input.tellg();
        if(size<=0 || size>1024*1024)return;
        input.seekg(0);std::string data(static_cast<size_t>(size),'\0');
        if(!input.read(data.data(),size))return;
        if(data.starts_with("\xef\xbb\xbf"))data.erase(0,3);
        picojson::value parsed;
        if(!picojson::parse(parsed,data).empty() || !parsed.is<picojson::object>())return;
        const auto &root=parsed.get<picojson::object>();
        const auto entry=root.find("details");
        const auto &details=entry!=root.end() && entry->second.is<picojson::object>()?entry->second.get<picojson::object>():root;
        const auto text=[](const picojson::value &v)->std::string {
            if(v.is<std::string>())return v.get<std::string>();
            if(v.is<double>())return v.to_str();
            if(v.is<picojson::array>()){
                std::string out;
                for(const auto &item:v.get<picojson::array>())if(item.is<std::string>()){
                    if(!out.empty())out+=", ";out+=item.get<std::string>();
                }
                return out;
            }
            return {};
        };
        const auto assign=[&](std::string &target,std::initializer_list<const char*> keys,bool year=false){
            if(missingOnly && !target.empty())return;
            for(const auto *key:keys){
                auto it=details.find(key);if(it==details.end())continue;
                auto value=text(it->second);if(value.empty()||value.find('\0')!=std::string::npos)continue;
                if(year){if(value.size()<4||!std::all_of(value.begin(),value.begin()+4,[](char c){return c>='0'&&c<='9';}))continue;value.resize(4);}
                target=std::move(value);return;
            }
        };
        if(game.name.empty()){auto it=root.find("title");if(it!=root.end())game.name=text(it->second);}
        assign(game.description,{"description"});assign(game.publisher,{"publisher"});assign(game.developer,{"developer"});
        assign(game.genre,{"genres","genre"});assign(game.region,{"region"});assign(game.rating,{"age_rating","rating"});
        assign(game.releaseYear,{"release_date","year"},true);assign(game.players,{"max_players","players"});
        return;
    }
    // Keep reading existing SD metadata; new catalog downloads use JSON only.

    std::string line;
    while (std::getline(input,line)) {
        const auto pos=line.find('=');
        if (pos==std::string::npos) continue;
        const auto key=Lower(Trim(line.substr(0,pos))), value=Trim(line.substr(pos+1));
        std::string *target=nullptr;
        if (key=="title" && game.name.empty()) target=&game.name;
        else if (key=="publisher") target=&game.publisher;
        else if (key=="developer") target=&game.developer;
        else if (key=="description") target=&game.description;
        else if (key=="genre") target=&game.genre;
        else if (key=="region") target=&game.region;
        else if (key=="rating") target=&game.rating;
        else if (key=="year") target=&game.releaseYear;
        else if (key=="players") target=&game.players;
        if(target && (!missingOnly || target->empty())) *target=value;
    }
}
}

void ApplyMediaPatch(GameEntry &game,const GameEntry &patch,unsigned mask){
    std::string* targets[]={&game.coverPath,&game.fullCoverPath,&game.backgroundPath,&game.logoPath,&game.previewPath,&game.metadataPath,&game.backCoverPath,&game.spinePath,&game.physicalMediaPath,&game.cartridgeLabelPath};
    const std::string* values[]={&patch.coverPath,&patch.fullCoverPath,&patch.backgroundPath,&patch.logoPath,&patch.previewPath,&patch.metadataPath,&patch.backCoverPath,&patch.spinePath,&patch.physicalMediaPath,&patch.cartridgeLabelPath};
    for(int i=0;i<kMediaTypeCount;++i)if(mask&(1u<<i))*targets[i]=*values[i];
    if((mask&(1u<<int(MediaType::Cover)))&&!game.coverPath.empty()&&game.fullCoverPath==game.defaultSleevePath)game.fullCoverPath.clear();
    if(mask&(1u<<int(MediaType::Metadata))){
        game.description=patch.description;game.publisher=patch.publisher;game.developer=patch.developer;
        game.genre=patch.genre;game.region=patch.region;game.rating=patch.rating;game.releaseYear=patch.releaseYear;game.players=patch.players;
    }
    if(game.sourceMedia){GameEntry source=game;SetGameMedia(source,*game.sourceMedia);source.sourceMedia.reset();ApplyMediaPatch(source,patch,mask);game.sourceMedia=std::make_shared<GameMedia>(CaptureGameMedia(source));}
}
void ReadMediaFile(GameEntry &game,MediaType type,const std::string &path){
    if(type==MediaType::Metadata){game.metadataPath=path;Metadata(game,path);return;}
    std::string* targets[]={&game.coverPath,&game.fullCoverPath,&game.backgroundPath,&game.logoPath,&game.previewPath,&game.metadataPath,&game.backCoverPath,&game.spinePath,&game.physicalMediaPath,&game.cartridgeLabelPath};
    if(int(type)>=0&&int(type)<kMediaTypeCount)*targets[int(type)]=path;
}

bool MediaFileMatches(MediaType type,const std::string &path) {
    const auto dot=path.find_last_of('.');
    return dot!=std::string::npos && ExtensionMatches(type,Lower(path.substr(dot+1)));
}
std::vector<std::string> ListMediaFiles(const std::string &path,MediaType type,const BrowserCancelled &cancelled,std::string *error) {
    if(error)error->clear();
    if(cancelled && cancelled())return {};
    if(BrowserPath::Container(path)){
        auto directories=ListDirectories(path,cancelled,error);
        for(auto &name:directories)if(name.back()!='/')name+="/";
        return directories;
    }
    std::vector<std::string> directories,files;
    DIR *dir=opendir(path.c_str());
    if(!dir){if(error)*error="CANNOT OPEN FOLDER";return {};}
    while(!cancelled || !cancelled()) {
        errno=0;
        auto *entry=readdir(dir);
        if(!entry){if(errno && errno!=ENOENT && error)*error="FOLDER READ FAILED";break;}
        const std::string name=entry->d_name;
        if(name=="." || name=="..") continue;
        const auto mode=DirectoryEntryMode(*entry,BrowserPath::Child(path,name));
        if(S_ISDIR(mode)) directories.push_back(name+"/");
        else if(S_ISREG(mode) && MediaFileMatches(type,name)) files.push_back(name);
    }
    closedir(dir);
    if(cancelled && cancelled())return {};
    std::sort(directories.begin(),directories.end());
    std::sort(files.begin(),files.end());
    directories.insert(directories.end(),files.begin(),files.end());
    return directories;
}
const char *MediaTypeName(MediaType type) {
    constexpr const char *names[]={"FRONT COVERS","FULL SLEEVES","BACKGROUNDS","LOGOS","GAMEPLAY VIDEOS","METADATA","BACK COVERS","SPINES","DISCS / CARTRIDGES","CARTRIDGE LABELS"};
    const int value=static_cast<int>(type);
    return value>=0 && value<kMediaTypeCount ? names[value] : "UNKNOWN";
}
std::string FindDefaultMediaFile(const MediaSource &source) {
    if(!MediaFileMatches(source.type,"Default.png")) return {};
    for(const auto &file:ListMediaFiles(source.path,source.type))
        if(Lower(file)=="default.png") return source.path+"/"+file;
    return {};
}
const char *MediaLibraryName(int library) {
    return library<0 ? "ALL LIBRARIES" : PlatformName(static_cast<Platform>(library));
}
std::vector<MediaSource> LoadMediaSources() {
    std::ifstream input(kConfig);
    std::vector<MediaSource> result;
    if (!input) {
        for (int i=0;i<kMediaTypeCount;++i) result.push_back({static_cast<MediaType>(i),-1,std::string(kRoot)+"/media/"+kKinds[i],true,0});
        for(auto &source:result) if(IsCoverMedia(source.type) || source.type==MediaType::PhysicalMedia)source.defaultFile=FindDefaultMediaFile(source);
        return result; // existing media keeps working without migration writes
    }
    std::string line;
    while (std::getline(input,line)) {
        std::istringstream row(line);
        std::string type,library,enabled,depth,path;
        if (!std::getline(row,type,'\t') || !std::getline(row,library,'\t') ||
            !std::getline(row,enabled,'\t') || !std::getline(row,depth,'\t') || !std::getline(row,path,'\t')) continue;
        type=Lower(type);
        if(type=="front")type="covers"; // New name; legacy front sources keep working.
        if(type=="full cover" || type=="fullcover")type="fullcovers";
        const auto kind=std::find(kKinds.begin(),kKinds.end(),type);
        if (kind==kKinds.end()) continue;
        try {
            const bool custom=library.rfind("id:",0)==0;
            const int scope=custom?-1:std::stoi(library);
            if (scope < -1 || scope > 2) continue;
            path=Trim(path);
            while (path.size()>1 && path.back()=='/') path.pop_back();
            if (path.empty()) continue;
            result.push_back({static_cast<MediaType>(kind-kKinds.begin()),scope,path,std::stoi(enabled)!=0,
                              std::clamp(std::stoi(depth),0,8),custom?library.substr(3):""});
            // Appended fields preserve every existing five-column source file.
            auto integer = [&](int fallback, int minimum, int maximum) {
                std::string field;
                if (!std::getline(row,field,'\t')) return fallback;
                field = Trim(field);
                try {
                    std::size_t used = 0;
                    const int value = std::stoi(field,&used);
                    return used == field.size() ? std::clamp(value,minimum,maximum) : fallback;
                } catch (...) { return fallback; }
            };
            auto &video = result.back().video;
            video.delayMs = integer(1000,0,10000);
            video.mode = static_cast<VideoMode>(integer(2,0,2));
            video.volume = integer(70,0,100);
            std::string fallback;
            if(std::getline(row,fallback,'\t')) result.back().defaultFile=Trim(fallback);
            if((IsCoverMedia(result.back().type) || result.back().type==MediaType::PhysicalMedia) && result.back().defaultFile.empty())
                result.back().defaultFile=FindDefaultMediaFile(result.back());
            video.foregroundOpacity=integer(100,0,100);
        } catch (...) {}
    }
    // Fresh setup creates these beside fullcovers. Also discover them on an
    // upgrade without rewriting an existing media config or enabling a disabled
    // explicit source. Empty configurations intentionally remain empty.
    const auto configured=result;
    for(const auto &sleeve:configured) if(sleeve.type==MediaType::FullCover) {
        const auto slash=sleeve.path.find_last_of('/');
        if(slash==std::string::npos)continue;
        for(const auto &[type,folder]:std::array<std::pair<MediaType,const char *>,5>{{
                {MediaType::Cover,"front"},{MediaType::BackCover,"back"},{MediaType::Spine,"spine"},{MediaType::PhysicalMedia,"media"},{MediaType::CartridgeLabel,"labels"}}}) {
            const auto path=sleeve.path.substr(0,slash)+"/"+folder;
            struct stat info{};
            if(stat(path.c_str(),&info) || !S_ISDIR(info.st_mode))continue;
            if(std::any_of(result.begin(),result.end(),[&](const auto &s){return s.type==type && s.path==path && s.library==sleeve.library && s.libraryId==sleeve.libraryId;}))continue;
            auto part=sleeve;part.type=type;part.path=path;part.defaultFile=FindDefaultMediaFile(part);
            result.push_back(std::move(part));
        }
    }
    return result;
}
bool MediaRefreshNeeded() {
    struct stat info{};
    return stat(kRefresh,&info)==0;
}
namespace {
std::set<std::string> PendingMediaScopes() {
    std::ifstream input(kRefresh);std::string line;std::set<std::string> scopes;
    while(std::getline(input,line)) {
        if(line=="pending" || line=="*")return {"*"};
        if(line.rfind("id:",0)==0 || line.rfind("platform:",0)==0)scopes.insert(line);
        else return {"*"}; // Older or incomplete marker: preserve full-refresh behavior.
    }
    if(MediaRefreshNeeded() && scopes.empty())return {"*"};
    return scopes;
}
bool QueueMediaScopes(std::set<std::string> scopes) {
    const auto previous=PendingMediaScopes();scopes.insert(previous.begin(),previous.end());
    if(scopes.contains("*"))scopes={"*"};
    mkdir(kRoot,0777);
    const std::string temporary=std::string(kRefresh)+".tmp";
    std::ofstream output(temporary);for(const auto &scope:scopes)output<<scope<<'\n';output.close();
    if(output.fail() || !SavedFile::Commit(temporary,kRefresh)){std::remove(temporary.c_str());return false;}
    return true;
}
bool SourceMatches(const MediaSource &s,const GameEntry &g){
    return !s.libraryId.empty()?s.libraryId==g.libraryId:
        s.library==-1||s.library==static_cast<int>(g.platform);
}
}
bool MediaRefreshIsScoped() {const auto scopes=PendingMediaScopes();return !scopes.empty()&&!scopes.contains("*");}
bool RequestMediaRefresh() { return QueueMediaScopes({"*"}); }
void CompleteMediaRefresh() { std::remove(kRefresh); }
bool SaveMediaSources(const std::vector<MediaSource> &sources) {
    for (const auto &source:sources) {
        const int type=static_cast<int>(source.type);
        if (type<0 || type>=kMediaTypeCount || source.library < -1 || source.library > 2 ||
            source.path.empty() || source.path.find_first_of("\t\r\n")!=std::string::npos ||
            source.defaultFile.find_first_of("\t\r\n")!=std::string::npos ||
            (!source.defaultFile.empty() && !MediaFileMatches(source.type,source.defaultFile))) return false;
    }
    const auto previous=LoadMediaSources();
    const auto same=[](const auto &a,const auto &b){
        return a.type==b.type && a.library==b.library && a.libraryId==b.libraryId &&
            a.enabled==b.enabled && a.depth==b.depth && a.path==b.path && a.defaultFile==b.defaultFile;
    };
    std::set<std::string> changed;
    const auto mark=[&](const MediaSource &s){if(s.enabled)changed.insert(!s.libraryId.empty()?"id:"+s.libraryId:
        s.library<0?"*":"platform:"+std::to_string(s.library));};
    for(const auto &s:previous)if(std::none_of(sources.begin(),sources.end(),[&](const auto &v){return same(s,v);}))mark(s);
    for(const auto &s:sources)if(std::none_of(previous.begin(),previous.end(),[&](const auto &v){return same(s,v);}))mark(s);
    // Reordering changes priority even when every source is otherwise identical.
    if(changed.empty() && previous.size()==sources.size() && !std::equal(previous.begin(),previous.end(),sources.begin(),same))
        for(const auto &s:sources)mark(s);
    mkdir(kRoot,0777);
    // Persist the union before the config: an interrupted save may refresh extra
    // content, but can never lose a previously queued refresh.
    if(!changed.empty() && !QueueMediaScopes(changed))return false;
    const std::string temporary=std::string(kConfig)+".tmp";
    std::ofstream output(temporary,std::ios::trunc);
    for (const auto &source:sources) {
        const int type=static_cast<int>(source.type);
        output<<kKinds[type]<<'\t'<<(source.libraryId.empty()?std::to_string(source.library):"id:"+source.libraryId)<<'\t'<<source.enabled<<'\t'
              <<std::clamp(source.depth,0,8)<<'\t'<<source.path;
        if (source.type == MediaType::Preview || !source.defaultFile.empty())
            output<<'\t'<<std::clamp(source.video.delayMs,0,10000)<<'\t'<<std::clamp(static_cast<int>(source.video.mode),0,2)
                  <<'\t'<<std::clamp(source.video.volume,0,100);
        if(source.type==MediaType::Preview)
            output<<'\t'<<source.defaultFile<<'\t'<<std::clamp(source.video.foregroundOpacity,0,100);
        else if(!source.defaultFile.empty()) output<<'\t'<<source.defaultFile;
        output<<'\n';
    }
    output.flush();
    output.close();
    if(output.fail() || !SavedFile::Commit(temporary,kConfig)) {
        std::remove(temporary.c_str());return false;
    }
    return true;
}
const MediaSource *VideoSourceFor(const std::vector<MediaSource> &sources, const GameEntry &game) {
    const MediaSource *best = nullptr;
    int bestScore = -1;
    for (const auto &source : sources) {
        if (!source.enabled || source.type != MediaType::Preview || game.previewPath.empty()) continue;
        const std::string prefix = source.path + "/";
        if(game.previewPath!=DefaultPath(source)) {
            if (game.previewPath.compare(0,prefix.size(),prefix) != 0) continue;
            const int nesting = static_cast<int>(std::count(game.previewPath.begin()+prefix.size(),game.previewPath.end(),'/'));
            if (nesting > source.depth) continue;
        }
        int score;
        if (!source.libraryId.empty()) {
            if (source.libraryId != game.libraryId && source.libraryId != game.mediaLibraryId) continue;
            score = source.libraryId==game.mediaLibraryId?3:2;
        } else if (source.library == static_cast<int>(game.platform)) score = 1;
        else if (source.library == -1) score = 0;
        else continue;
        if (score > bestScore) { best = &source; bestScore = score; }
    }
    return best;
}
void EnrichLibraryMedia(std::vector<GameEntry> &games,const ScanObserver &progress,const std::vector<MediaSource> *scopedSources) {
    Index index;
    std::array<std::vector<Asset>,kMediaTypeCount> defaults;
    std::map<std::string,std::array<float,2>> layouts;
    for (const auto &source:scopedSources?*scopedSources:LoadMediaSources()) if (source.enabled && std::any_of(games.begin(),games.end(),[&](const auto &g){return SourceMatches(source,g);})) {
        IndexDirectory(index,source,source.path,source.depth,progress,games.size());
        const auto path=DefaultPath(source);
        struct stat info{};
        if(!path.empty() && MediaFileMatches(source.type,path) && !stat(path.c_str(),&info) && S_ISREG(info.st_mode)) {
            defaults[static_cast<int>(source.type)].push_back({source.library,path,source.libraryId});
            if(source.type==MediaType::FullCover && !layouts.count(path)) {
                auto &cuts=layouts[path];
                std::ifstream layout(path+".layout");
                std::string magic;std::uint64_t bytes=0;float back=0,front=0;
                if(layout>>magic>>bytes>>back>>front && magic=="UFLOW-SLEEVE-1" &&
                   back>0 && back<front && front<1 && bytes==static_cast<std::uint64_t>(info.st_size))cuts={back,front};
            }
        }
    }
    std::size_t completed=0;
    for (auto &game:games) {
        RestoreSourceMedia(game);
        if(progress)progress({"MATCHING ARTWORK",game.absolutePath,games.size(),completed,games.size()});
        game.fullCoverPath=Find(index,MediaType::FullCover,game);
        game.backCoverPath=Find(index,MediaType::BackCover,game);
        game.spinePath=Find(index,MediaType::Spine,game);
        game.defaultSleevePath=FindAsset(defaults[static_cast<int>(MediaType::FullCover)],game);
        game.defaultBackEnd=game.defaultFrontStart=0;
        if(const auto layout=layouts.find(game.defaultSleevePath);layout!=layouts.end()) {
            game.defaultBackEnd=layout->second[0];game.defaultFrontStart=layout->second[1];
        }
        const auto cover=Find(index,MediaType::Cover,game);
        if (!cover.empty()) game.coverPath=cover;
        const auto background=Find(index,MediaType::Background,game);
        if (!background.empty()) game.backgroundPath=background;
        game.logoPath=Find(index,MediaType::Logo,game);
        game.cartridgeLabelPath=Find(index,MediaType::CartridgeLabel,game);
        game.physicalMediaPath=Find(index,MediaType::PhysicalMedia,game);
        if(game.physicalMediaPath.empty())game.physicalMediaPath=FindAsset(defaults[static_cast<int>(MediaType::PhysicalMedia)],game);
        game.previewPath=Find(index,MediaType::Preview,game);
        const auto metadata=Find(index,MediaType::Metadata,game);
        game.metadataPath=metadata;
        Metadata(game,metadata.empty()?FindAsset(defaults[static_cast<int>(MediaType::Metadata)],game):metadata,metadata.empty());
        // Extract only from the source cartridge/disc, never the injected donor.
        // Runs during scanning and is cached; rendering never reads ROM data.
        if(game.fullCoverPath.empty() && game.coverPath.empty()) {
            const auto dot=game.absolutePath.find_last_of('.');
            const auto ext=dot==std::string::npos?"":Lower(game.absolutePath.substr(dot));
            const bool ds=game.platform==Platform::Files&&(ext==".nds"||ext==".srl"||ext==".dsi");
            if(ds||game.platform==Platform::GameCube)game.coverPath=RomArt::Cached(game.absolutePath,ds,kRoot);
        }
        std::string *paths[]={&game.coverPath,&game.fullCoverPath,&game.backgroundPath,&game.logoPath,&game.previewPath};
        // A generic sleeve must not hide real flat artwork or an extracted icon.
        const bool hasArtwork=!game.coverPath.empty()||!game.fullCoverPath.empty()||!game.backCoverPath.empty()||!game.spinePath.empty();
        for(int type=0;type<5;++type) if(paths[type]->empty() && !(type<2&&hasArtwork)) *paths[type]=FindAsset(defaults[type],game);
        if(game.coverPath.empty() && game.fullCoverPath.empty())game.coverPath=FindAsset(defaults[static_cast<int>(MediaType::Cover)],game);
        if(game.backCoverPath.empty())game.backCoverPath=FindAsset(defaults[static_cast<int>(MediaType::BackCover)],game);
        if(game.spinePath.empty())game.spinePath=FindAsset(defaults[static_cast<int>(MediaType::Spine)],game);
        ++completed;
    }
    if(progress)progress({"MATCHING ARTWORK","",games.size(),completed,games.size()});
} // index freed after scan; no media directory work during rendering

void ApplyContentTypeMedia(std::vector<GameEntry> &games,const std::vector<LibraryDefinition> &libraries,const ScanObserver &progress){
    std::map<std::string,std::vector<size_t>> groups;
    for(size_t i=0;i<games.size();++i){
        RestoreSourceMedia(games[i]);
        const auto id=games[i].platform==Platform::WiiU&&games[i].installPackages.empty()?GameLibraryId(games[i]):DesiredMediaLibrary(games[i],libraries);
        if(id!=games[i].libraryId)groups[id].push_back(i);
    }
    if(groups.empty())return;
    const auto sources=LoadMediaSources();
    for(const auto &[id,indices]:groups){
        const auto definition=std::find_if(libraries.begin(),libraries.end(),[&](const auto &l){return l.id==id;});
        if(definition==libraries.end())continue;
        std::vector<MediaSource> scoped;
        for(const auto &s:sources)if(s.enabled && (s.libraryId==id ||
            (s.libraryId.empty()&&definition->parser!=Platform::Files&&s.library==static_cast<int>(definition->parser))))scoped.push_back(s);
        if(!definition->mediaByContentType){
            // Shared game artwork is intentional; shared platform placeholders
            // are not. Resolve defaults only, without scanning game directories.
            for(auto i:indices){auto &game=games[i];
                game.sourceMedia=std::make_shared<GameMedia>(CaptureGameMedia(game));
                game.defaultMediaLibraryId=id;
                if(!game.defaultSleevePath.empty())for(auto *part:{&game.coverPath,&game.fullCoverPath,&game.backCoverPath,&game.spinePath})
                    if(*part==game.defaultSleevePath)part->clear();
                for(const auto &source:sources)if(source.enabled&&SourceMatches(source,game)&&IsCoverMedia(source.type)){
                    const auto path=DefaultPath(source);if(path.empty())continue;
                    for(auto *part:{&game.coverPath,&game.fullCoverPath,&game.backCoverPath,&game.spinePath})if(*part==path)part->clear();
                }
                game.defaultSleevePath.clear();game.defaultBackEnd=game.defaultFrontStart=0;
                for(const auto &source:scoped)if(source.type==MediaType::FullCover){
                    const auto path=DefaultPath(source);struct stat info{};
                    if(path.empty()||stat(path.c_str(),&info)||!S_ISREG(info.st_mode))continue;
                    game.defaultSleevePath=path;
                    std::ifstream layout(path+".layout");std::string magic;uint64_t bytes=0;float back=0,front=0;
                    if(layout>>magic>>bytes>>back>>front&&magic=="UFLOW-SLEEVE-1"&&bytes==uint64_t(info.st_size)&&back>0&&back<front&&front<1){game.defaultBackEnd=back;game.defaultFrontStart=front;}
                    break;
                }
            }
            continue;
        }
        std::vector<GameEntry> variants;
        for(auto i:indices){auto variant=games[i];variant.libraryId=id;variant.platform=definition->parser;
            SetGameMedia(variant,{});variants.push_back(std::move(variant));}
        EnrichLibraryMedia(variants,progress,&scoped);
        for(size_t n=0;n<indices.size();++n){auto &game=games[indices[n]];
            game.sourceMedia=std::make_shared<GameMedia>(CaptureGameMedia(game));
            auto appearance=CaptureGameMedia(variants[n]);
            // Keep a working preview/backdrop when that system has no replacement.
            // Covers never fall back to the source DVD sleeve in this mode.
            if(appearance.background.empty())appearance.background=game.backgroundPath;
            if(appearance.logo.empty())appearance.logo=game.logoPath;
            if(appearance.physical.empty())appearance.physical=game.physicalMediaPath;
            if(appearance.cartridgeLabel.empty())appearance.cartridgeLabel=game.cartridgeLabelPath;
            if(appearance.video.empty())appearance.video=game.previewPath;
            SetGameMedia(game,appearance);game.mediaLibraryId=id;game.defaultMediaLibraryId=id;
        }
    }
}

void RefreshPendingLibraryMedia(std::vector<GameEntry> &games,const std::vector<LibraryDefinition> &libraries,const ScanObserver &progress){
    const auto scopes=PendingMediaScopes();
    std::vector<size_t> indices;std::vector<GameEntry> affected;
    for(size_t i=0;i<games.size();++i){
        const auto &g=games[i];const auto desired=DesiredMediaLibrary(g,libraries);
        bool match=scopes.empty()||scopes.contains("*")||scopes.contains("id:"+g.libraryId)||
            scopes.contains("id:"+desired)||scopes.contains("id:"+g.mediaLibraryId)||
            scopes.contains("platform:"+std::to_string(static_cast<int>(g.platform)))||
            (!g.installPackages.empty()&&scopes.contains("id:wiiu"))||
            desired!=(g.mediaLibraryId.empty()?g.libraryId:g.mediaLibraryId);
        for(const auto &l:libraries)if(l.id==desired && scopes.contains("platform:"+std::to_string(static_cast<int>(l.parser))))match=true;
        if(match){indices.push_back(i);affected.push_back(g);}
    }
    if(affected.empty())return;
    RefreshLibraryMedia(affected,progress);
    ApplyContentTypeMedia(affected,libraries,progress);
    for(size_t i=0;i<indices.size();++i)games[indices[i]]=std::move(affected[i]);
}
