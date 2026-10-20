#include "library.hpp"
#include "library_tabs.hpp"
#include "browser_path.hpp"
#include "directory_entry.hpp"
#include "saved_file.hpp"
#include <cerrno>
#include "rom_art.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>

namespace {
constexpr const char *kRoot = "fs:/vol/external01/wiiu/apps/uFlow";
constexpr const char *kConfig = "fs:/vol/external01/wiiu/apps/uFlow/media_sources.cfg";
constexpr const char *kRefresh = "fs:/vol/external01/wiiu/apps/uFlow/media-refresh.pending";
constexpr std::array<const char *,kMediaTypeCount> kKinds{"covers","fullcovers","backgrounds","logos","previews","metadata","back","spine","media"};
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
struct Asset { int library; std::string path; std::string libraryId; };
using Index=std::array<std::map<std::string,std::vector<Asset>>,kMediaTypeCount>;
bool ExtensionMatches(MediaType type,const std::string &extension) {
    if (type==MediaType::Metadata) return extension=="ini";
    if (type==MediaType::Preview) return extension=="mp4" || extension=="avi" || extension=="mov";
    return extension=="png" || extension=="tga" || extension=="jpg" || extension=="jpeg";
}
std::string DefaultPath(const MediaSource &source) {
    if(source.defaultFile.empty()) return {};
    if(source.defaultFile.front()=='/' || source.defaultFile.find(':')!=std::string::npos) return source.defaultFile;
    return source.path+"/"+source.defaultFile;
}
std::string FindAsset(const std::vector<Asset> &assets,const GameEntry &game) {
    for(const auto &asset:assets) if(!asset.libraryId.empty() && asset.libraryId==game.libraryId) return asset.path;
    for(const int library:{static_cast<int>(game.platform),-1})
        for(const auto &asset:assets) if(asset.libraryId.empty() && asset.library==library) return asset.path;
    return {};
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
            const auto key=Key(name.substr(0,dot));
            if (!key.empty()) index[static_cast<int>(source.type)][key].push_back({source.library,full,source.libraryId});
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
    return {};
}
void Metadata(GameEntry &game,const std::string &path,bool missingOnly=false) {
    std::ifstream input(path);
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
    constexpr const char *names[]={"FRONT COVERS","FULL SLEEVES","BACKGROUNDS","LOGOS","GAMEPLAY VIDEOS","METADATA","BACK COVERS","SPINES","DISCS / CARTRIDGES"};
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
        for(const auto &[type,folder]:std::array<std::pair<MediaType,const char *>,4>{{
                {MediaType::Cover,"front"},{MediaType::BackCover,"back"},{MediaType::Spine,"spine"},{MediaType::PhysicalMedia,"media"}}}) {
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
bool RequestMediaRefresh() {
    mkdir(kRoot,0777);
    std::ofstream output(kRefresh);output<<"pending\n";output.close();
    return !output.fail();
}
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
    const bool matchingChanged=previous.size()!=sources.size() ||
        !std::equal(previous.begin(),previous.end(),sources.begin(),sources.end(),[](const auto &a,const auto &b){
            return a.type==b.type && a.library==b.library && a.libraryId==b.libraryId &&
                a.enabled==b.enabled && a.depth==b.depth && a.path==b.path && a.defaultFile==b.defaultFile;
        });
    // Playback options are read live. Artwork edits only mark media matching
    // pending, including across restart; they never invalidate the game catalog.
    mkdir(kRoot,0777);
    if(matchingChanged && !RequestMediaRefresh())return false;
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
    for (const auto &source:scopedSources?*scopedSources:LoadMediaSources()) if (source.enabled) {
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
        game.physicalMediaPath=Find(index,MediaType::PhysicalMedia,game);
        if(game.physicalMediaPath.empty())game.physicalMediaPath=FindAsset(defaults[static_cast<int>(MediaType::PhysicalMedia)],game);
        game.previewPath=Find(index,MediaType::Preview,game);
        const auto metadata=Find(index,MediaType::Metadata,game);
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
        const auto id=DesiredMediaLibrary(games[i],libraries);
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
            if(appearance.video.empty())appearance.video=game.previewPath;
            SetGameMedia(game,appearance);game.mediaLibraryId=id;
        }
    }
}
