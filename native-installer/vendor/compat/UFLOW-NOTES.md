# Narrow uFlow integration

Imported from the local v2.1.1 source snapshot of
https://github.com/Xpl0itU/vwii-compat-installer (GPL-2.0-or-later).
Original copyright notices and LICENSE are retained.

Only d2x in-memory preparation, IOS reading/ticket retargeting, tinyxml2 and
declarations are used. Interactive menus, recovery/uninstall/region/IOS80 paths
are excluded. CINS_Install is supplied by uFlow as a validated in-memory queue,
not upstream's direct NAND installer. Files are verified before publication.
uFlow adds strict base-version/content/patch/module validation and private cIOS
contents; no original shared IOS content is modified.
