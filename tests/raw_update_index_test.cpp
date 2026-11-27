#include "RawAddonNativePath.h"
#include <filesystem>
#include <fstream>
#include <cassert>
int main() {
 namespace fs=std::filesystem;
 const auto root=fs::temp_directory_path()/"uflow-update-index-test";
 fs::remove_all(root); fs::create_directories(root/"Sound/Resource");
 std::ofstream(root/"Sound/Resource/Intro.bin") << "intro";
 std::ofstream(root/"empty.bin");
 RawUpdateFileIndex index;
 assert(index.Build(root.string(),"wiiu/update/content"));
 assert(index.Size()==2);
 const auto manifest=fs::temp_directory_path()/"uflow-update-index-test.txt";
 assert(index.Save(manifest.string(),"wiiu/update/content"));
 index.Clear();
 assert(index.Load(manifest.string(),"wiiu/update/content"));
 assert(index.Size()==2);
 assert(!index.Load(manifest.string(),"wrong-title/content"));
 assert(index.Size()==0);
 assert(index.Load(manifest.string(),"wiiu/update/content"));
 fs::remove_all(root); // Lookup must be purely in memory after startup.
 assert(index.Contains("wiiu/update/content/sound/resource/INTRO.bin"));
 assert(index.Contains("wiiu/update/content/empty.bin"));
 assert(!index.Contains("wiiu/update/content/base-only.bin"));
 assert(!index.Contains("wiiu/update/content/Sound"));
 assert(!index.Build(root.string(),"other"));
 assert(index.Size()==0);
 fs::create_directories(root);
 assert(index.Build(root.string(),"other"));
 assert(index.Size()==0);
 index.Clear(); fs::remove_all(root);
 std::ofstream(manifest) << "UFLOW-UPDATE-INDEX-1\nwiiu/update/content\n2\nwiiu/update/content/one.bin\n";
 assert(!index.Load(manifest.string(),"wiiu/update/content"));
 assert(index.Size()==0);
 std::ofstream(manifest) << "UFLOW-UPDATE-INDEX-1\nwiiu/update/content\n0\nextra\n";
 assert(!index.Load(manifest.string(),"wiiu/update/content"));
 fs::remove(manifest);
}
