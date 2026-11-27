#include "globals.h"

RPXLoader_ReplacementInformation gReplacementInfo = {};
CRLayerHandle contentLayerHandle                  = 0;
CRLayerHandle codeLayerHandle                     = 0;
CRLayerHandle metaLayerHandle                     = 0;
CRLayerHandle saveLayerHandle                     = 0;
CRLayerHandle updateLayerHandle                   = 0;
CRLayerHandle dlcLayerHandle                      = 0;
std::string gActiveSaveRedirectionPath;
LooseTitleInformation gLooseTitleInfo = {};

RawUpdateFileIndex gRawUpdateFiles;

RawAssetCache gRawAssetCache;
