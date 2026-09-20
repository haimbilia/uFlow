// Read-only inspection of a supplied GBA VC archive's config and ROM identity.
#include "vc_formats.hpp"
#include <iostream>
using namespace VCFormat;
void Node(const PsbNode &node,const std::vector<std::string> &names,const std::vector<std::string> &strings,int depth=0){
    if(node.type==33||node.type==32){for(size_t i=0;i<node.children.size();++i){
        std::cout<<std::string(depth*2,' ')<<(node.type==33?names.at(node.names.at(i)):std::to_string(i))<<": ";
        if(node.children[i].type>=32)std::cout<<'\n';Node(node.children[i],names,strings,depth+1);
    }}else if(node.type>=21&&node.type<=24)std::cout<<strings.at(Number(node.leaf,0,node.type-20))<<'\n';
    else if(node.type>=4&&node.type<=8)std::cout<<PsbInteger(node)<<'\n';
    else std::cout<<"type="<<unsigned(node.type)<<" bytes="<<node.leaf.size()<<'\n';
}
int main(int argc,char **argv){try{
    Need(argc==2||argc==3,"Usage: inspect-gba-profile CONTENT_DIRECTORY [SOURCE_ROM]");const std::string path=argv[1];
    GbaArchive archive(Read(path+"/alldata.psb.m"));const auto bin=Read(path+"/alldata.bin",128*1024*1024);
    const auto rom=DecodeMdf(bin.substr(archive.offset,archive.length),archive.romName);
    if(argc==3){Need(rom==Read(argv[2]),"INJECTED ROM DIFFERS FROM SOURCE");std::cout<<"PASS: complete injected ROM equals source bytes\n";}
    std::cout<<"ROM "<<archive.romName<<" bytes="<<rom.size()<<" code="<<rom.substr(0xac,4)<<'\n';
    for(const char *tag:{"FLASH1M_V","FLASH512_V","FLASH_V","EEPROM_V","SRAM_V"}){auto p=rom.find(tag);if(p!=Bytes::npos)std::cout<<"Save signature "<<rom.substr(p,strlen(tag)+3)<<'\n';}
    const auto &files=archive.root.children[archive.info];
    for(size_t i=0;i<files.names.size();++i){
        const auto name=archive.names.at(files.names[i]);if(!name.ends_with("/config/title_prof.psb.m"))continue;
        const auto &record=files.children[i];if(record.children.size()!=2)continue;
        const auto off=PsbInteger(record.children[0]),len=PsbInteger(record.children[1]);Need(uint64_t(off)+len<=bin.size(),"Archive range invalid");
        auto data=bin.substr(off,len);if(data.compare(0,4,"mdf\0",4)==0)data=DecodeMdf(data,name);
        std::cout<<"FILE "<<name<<" size="<<data.size()<<'\n';
        if(data.compare(0,4,"PSB\0",4)==0){
            auto names=PsbNames(data);size_t p=Number(data,16);const auto offsets=PsbArray(data,p);std::vector<std::string> strings;
            for(auto off:offsets){const auto begin=Number(data,20)+off;Need(begin<data.size(),"String out of bounds");const auto end=data.find('\0',begin);Need(end!=Bytes::npos,"String unterminated");strings.push_back(data.substr(begin,end-begin));}
            Node(PsbRead(data,Number(data,36)),names,strings);
        }
    }
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
