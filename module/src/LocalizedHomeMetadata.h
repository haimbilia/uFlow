#pragma once
#include <cstdio>
// A homebrew name must be present in every HOME-menu locale.
template<class Meta>
void SetLocalizedHomeMetadata(Meta &meta,const char *longname,const char *shortname,const char *author) {
#define UFLOW_META_LANGUAGE(lang) \
    std::snprintf(meta.longname_##lang,sizeof(meta.longname_##lang),"%s",longname); \
    std::snprintf(meta.shortname_##lang,sizeof(meta.shortname_##lang),"%s",shortname); \
    std::snprintf(meta.publisher_##lang,sizeof(meta.publisher_##lang),"%s",author)
    UFLOW_META_LANGUAGE(ja);UFLOW_META_LANGUAGE(en);UFLOW_META_LANGUAGE(fr);
    UFLOW_META_LANGUAGE(de);UFLOW_META_LANGUAGE(it);UFLOW_META_LANGUAGE(es);
    UFLOW_META_LANGUAGE(zhs);UFLOW_META_LANGUAGE(ko);UFLOW_META_LANGUAGE(nl);
    UFLOW_META_LANGUAGE(pt);UFLOW_META_LANGUAGE(ru);UFLOW_META_LANGUAGE(zht);
#undef UFLOW_META_LANGUAGE
}
