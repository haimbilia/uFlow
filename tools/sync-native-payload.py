"""Synchronize built release components, or fail if native packaging is stale.

This mechanically updates the existing personal payload/manifest, never SD files.
Build changed components first. Nintendo bases and user configuration are not inputs.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil

ENTRY = re.compile(r'    \{"([^"\n]+)","([0-9a-f]{64})",(\d+)ULL,(\d+)u,\'([rkb])\'\},')
TITLE_INSTALLER = "wiiu/apps/uFlow/modules/title-installer.ufmod"
SIGPATCHES = "wiiu/environments/aroma/modules/setup/01_sigpatches.rpx"
# Upstream SigpatchesModuleWiiU release 1.2; loaded after 00_mocha on reboot.
SIGPATCHES_HASH = "41f9409f0c615cf3276e647bcad44a4723389388342f0e2b8994527651a38776"
# User-provided system menu tracks, restored from the SD music folder.
MUSIC_INPUTS = {'GameCube Menu.ogg': '4648ca65fad2285c88828f1ca94d1984fdaef4ee6ada9aaad4de50e2f5aa2993', 'Wii Menu.ogg': 'd35cae2250f40ec95deddc3745d0b7cb3fbe3ab3b1136dbf472ea7465f660536', 'Wii Shop Channel.ogg': '6411fd422c301c1958d4c77cb99eb50a4bfaca1e94f734778f1adb25dbc3c083', 'Wii U Menu.ogg': '3f833c34f3f0066b1aa69472471ae96be418035565d41439c332dee6072bd0c5'}
# Security release: reject older builds with privileged network diagnostics.
NETWORK_DISABLED_INPUTS = {'wiiu/environments/aroma/modules/RPXLoadingModule.wms': 'ecb6ef9fe5cd7100438ee08cc42feb343b18393ff2392162fd764524b59b2ffd', 'wiiu/environments/aroma/modules/setup/00_mocha.rpx': '1a650a341a0d91e7fb676984d701ba74000b90a6a4e151349a28b235305e3d85'}
RAW = 1017  # Wii U and DS/NES/SNES/GBA/N64/PCE/MSX VC selections.
REGION = "wiiu/environments/aroma/plugins/regionfree.wps"
FTPIIU = "wiiu/environments/aroma/plugins/ftpiiu.wps"
FTPIIU_PATCH_HASH = "123495ccb090f76fae6065dc0687f48ebe9dd10c99d13078ecfe82738160ab28"
PLUGIN_BACKEND = "wiiu/environments/aroma/modules/PluginBackend.wms"
PLUGIN_BACKEND_HASH = "aa564fa55f3e82d8c69df07e5e85b8072949c5fefe3e45a74b7a3064ceae0c5d"
MEMORY_MAPPING = "wiiu/environments/aroma/modules/MemoryMappingModule.wms"
MEMORY_MAPPING_HASH = "10f4183b237cbc5211794df9f909866db6ce7fb0832f00d6727350cb4917f73d"
CONTENT_REDIRECTION = "wiiu/environments/aroma/modules/ContentRedirectionModule.wms"
CONTENT_REDIRECTION_HASH = "5944f0394e16d09227843abe2ba27044d653441502bb0b58467e4c2c8065017c"
# Statically linked into the Core app by tools/video-ffmpeg.Dockerfile.
# Rebuild the video builder and app when changing this tested adapter.
H264_ADAPTER = "tools/ffmpeg/h264_wiiu.c"
H264_ADAPTER_HASH = "fb50285dfa6ee75dbe4a45c387b47d11ba8519f90715358469a120ada957be33"
# Statically linked SDL BC3 backend and encoder, included in Core. Legacy bridge is unused.
COVER_CACHE_INPUTS = {'tools/patch-sdl-cover-format.py': 'ce05409a76120e777a662c0bdedbff63ecbfcb7450532f8eff81c04aae37dcde', 'tools/bc3-upload-layout.h': '45c0bb4945912183bbd6b98da6a6f4a66456cf6174a751b7c8b336c19b62d650', 'tools/cover-cache.Dockerfile': 'eccf083768ce4747074e40788901b4d478e4a577333fe986eafa9bbeb65c2a79', 'app/vendor/stb/stb_dxt.h': '807667ef98e0fd749cdb65cca0c2d980bc148109d2fed6f1873c81ae0f449933'}
# Statically linked preview GPU colour shaders and renderer patch.
VIDEO_COLOUR_INPUTS = {'tools/patch-sdl-video-colour.py': 'b9a7689c954f4e0a4c2a1faefd6fe79f3e40eafb03bccd1db2aacbe3ba5d25ab', 'tools/video-colour.Dockerfile': 'b6ee630ca4f53d870d98cc311ddb8a6f169ca11100bf9885dfa12cb62858a157', 'tools/video-shaders/video1.inc': 'd6476c6c722026a3daf4630e3c35f8ca4da664203423f25af07ef043fdce9c00', 'tools/video-shaders/video2.inc': '122c9ff56e9e0c4961bccb8d23e90e0e7241545384af965ae0b555fc9664c311', 'tools/video-shaders/video3.inc': '0abc69c338209e3a35a485ecaca177949d6ca0bf1a37fbac71128521ee9c02d7', 'tools/video-shaders/video4.inc': '1fe6a67cb66c4c74de3dbd405d5a1945cc1c4c138f7ecd2195c8a81b2c21d9bf'}
VIDEO_NV12_INPUTS = {'tools/patch-sdl-video-nv12.py': '69354e8f8147d4030083c3fbd5dc5da0845c6908b2493762d505a40f73b35749', 'tools/video-nv12-backend.c': 'e929d917cee4b32721add2d37b640fb904f89c4b18056c2231f7e2701e8d8965', 'tools/video-nv12.Dockerfile': 'b09be3bf3d994c9701f0fe9e6a9e58d44b89bea9278ef3749e81b7136f926579', 'tools/video-shaders/nv1.inc': 'e3dbcf475559e1a5821e3fa577448c1bad8e7c97c22ff7753a940a0e1a7519ce', 'tools/video-shaders/nv2.inc': '5af9f2b5788a119d68ac27268fcd92f5f53753c6afdcb92e0714d8abb780fcb8', 'tools/video-shaders/nv3.inc': '24117a511f36f0a6c72ce68ea1e32662228fe2232fff605308b6dc0f3337b5ec', 'tools/video-shaders/nv4.inc': '17cc512b6de5d9577439cd9bcb1cfad35b62831c6c38cc38a042d6383a330c0f', 'tools/patch-ffmpeg-buffer-pool.py': '37c86f045704ec8cc5bc30cb3ecadff11baade9b8cab32be48dc4c836c76d942', 'tools/ffmpeg/wiiu-buffer-pool.h': 'a1614dbf0ce39ea2ff7f3c19f0885388707e46f7d5473bab68075c4bc1207340'}
# SDL foreground diagnostic dependency; build app with uflow-builder:handoff.
HANDOFF_INPUTS = {'tools/patch-sdl-handoff.py': 'e36191d9f0461b32473ca3ff6a080a5b34d3ed678f2fc2c8f126a699b9212eb2', 'tools/handoff-diagnostics.Dockerfile': '03dca3fdf8e19bc7f6cdea166700e83abc6dbe36c4bf33910d5f27df7f87404a'}
# Cartridge geometry is compiled into Core; no SD-only model dependency.
RETROARCH_LAUNCHER_INPUTS = {'app/source/launchers.hpp': 'c42933e496ec717e1de7c6feba4c56302eb10114e9f046a2ccc809862b684ace', 'app/source/launcher_modules.hpp': '338a71a866cae3da9f0df86885ffdb8eab987c9b9c620bcbd96851eeb4bb1858', 'app/source/retroarch_args.hpp': '7a0e6205418135a4d3c637d19c8837e922f45bb8a8e2160e1b34fa0f4d9415f4'}
SKIN_PACKAGE_INPUTS = {'app/source/cover_faces.hpp': 'd8468b7d395399d7ccaa4e1b0aa9d2f4930de91458d0ccc98fcc97e5f768f2fd', 'app/source/settings.hpp': 'f4be8fbc4c2f6fc0c71bd0284a3da8a42a190837db481acfbc423a99b3f942b9', 'app/source/settings.cpp': 'c20913e6e85a176b756c68bb4449617c8d9e86a8fdf19c64cb2f0c66d4a4b465', 'app/source/settings_writer.hpp': '5cc768738622fa930a6090f6975a1e2279b6bde07d6a874820440a454e78a991', 'app/source/frame_profile.hpp': '0c86b51e612320525217a6f324936c202b7294f139517c01b5ab3c769df2a496', 'app/source/skin_surface.hpp': '8fbd8499bbef481a85627c44a2f9ed384481aece87d9253730f31c3c89b694e4', 'app/source/skin.hpp': '97632a02dc2784987b09a3f6d287f38fa14850411d956bf78f423361f2a9c9c0', 'app/source/skin_layout.hpp': '27f0807753a4224d93524c8e8dbd9758e15f8a89e6d022f696e955c8edd7af85', 'app/source/skin_layout_members.hpp': '1fb23d6197d0fd8a7a2ff0ecc18e67f0a0eaff28eaf3fa195f8b5e9ce28762b0', 'app/source/skin_effects.hpp': '85910e6a2e31a7701f24327a31f7327e8479c0d466e5db271c1b6e9e61eeaf6a', 'app/source/skin_scene_members.hpp': 'c709e036257da6f515692705fdd9eab849002b9425bbb30d94ecd97d4c6db928', 'app/source/skin_shelf.hpp': 'b867e3485644c4b4b768230060680e4b735496670dc31ce2c4ca430cd0b3e9b5', 'app/source/skin_shelf_members.hpp': '8e39b731f6a8e91fe9f57db746a07cc46e1f1c8ed83ab61fc58dcc40b0649a64'}
GLASS_POPUP_INPUTS = {'app/source/uforce_skin_members.hpp': '16fdbbb1b0316c9f097676cef75c25ed3cfc5841bf5d3325e42e41efe8a10ea0', 'app/source/switch_skin_members.hpp': '61b8a2dea8de7e33238d896aa788d097d381126d0f5cc75346cb23844aa2dd04', 'app/source/wiiu_menu_members.hpp': '882c697ec1cdfa1042888c290867fff0b89e81eaf69479c6ba789e205349741a', 'app/source/settings_help.hpp': '3cca50871f7c75249e33af1119a4da1bfecc5130f4c9f2526c955d297adb4277', 'app/source/home_navigation.hpp': '9c20ab36f46db791fb60982e4b385636412aae54504602bf79ca47903e9f138b', 'app/source/settings_help_members.hpp': 'ba67980969b6c3f3953973f8ebed2946f9ae9ded7830bc8235f26ed45f2a6abd'}
SCOPED_MEDIA_INPUTS = {'app/source/media.cpp': '41e00dc2cffe887b5532c39ded7a36fd61a2c3d6771239bbd85cd0d0f81e589f', 'app/source/scan_worker.hpp': 'fffc90a0887d063809d58ead2e22f9c4ff1076f7f0e8f9e6ed094404a6aaf79e'}
BROWSER_PATH_INPUTS = {'app/source/browser_path.hpp': '4377b100575406dd2f6f2c4391311934fd6a5e4c81f95cb8c6947c9250e3f358'}
CONTENT_TYPE_SAVE_INPUTS = {'app/source/library.cpp': 'f7c38a2175bf719ace3fa59fa0ab82130772478d53ec39c3173fa8bfc3ac6560', 'app/source/library.hpp': '18e507495888c3088cb0e1df2632a39ebd0bac0ebe6d5fc33d0d3d99bea39c96', 'app/source/saved_file.hpp': 'a36da2a0776d0d85bd3d4ce6c326a4812ede200c4d1ba9b0c3169fb88ba56d22', 'app/source/box_color_settings_members.hpp': '9c4a5b5036b08aaccb8f947bfd9167f1d4a289f84dae67b73f1f4b7170e2aae6', 'app/source/navigation_default_sleeve.hpp': '25dbf36a82cf2c6d0fd786d6ca6ac8c1dc760cb37f368053f5973e30c9e64c68'}
NUMBER_DEFAULT_INPUTS = {'app/source/number_repeat.hpp': '9558e9d9e9848119c08c1e58d1e8df0c5176b62ca2719244f30ab18712513244', 'app/source/number_settings_members.hpp': '30e9015752363f2d3e12db053014ce2fa62bfe0e09c25427fd0267a9594286a5', 'app/source/case_profiles.hpp': '828862a98fb9e60a23ca194684e4b5ed5027e63a4fb92b9cea86241ab4131678', 'app/source/cover_presentation.hpp': '69fb9ab3dd0188fa61cbae667b6a76d1e03c024666d863cab0a078db1acfdc2b', 'native-installer/source/installer.hpp': 'c5404d6b80b9c930eaece898c5369f34fb56561d19a56af8f6f2847b5a30183c'}
PREPARED_MODEL_INPUTS = {'tools/prepare-ps1-jewel.py': 'ccc189c3b5113eabee3ebba4bc2587f39e4cd9572b1206c623e45cc4da8522bf', 'app/source/ps1_jewel.hpp': '2edcb5d5a062715f25d82353140293f71acbccbf05724e1726f0cffb8a2c16a5', 'app/source/ps1_jewel_data.hpp': '734003499e3d7e4881e59e2b0252ff82a58b760887ef9aa5e87524f9e8149f44', 'app/source/prepared_models.hpp': 'ba4c8f802a5709335059638f365636477d8a57d09772a14d1bf7e728f35b4fa3', 'app/source/cardboard_box.hpp': '6ed5f97638db9eba2f635af75dd13db25e099a0af1fefeca9b7660d2d610fff0'}
CARTRIDGE_INPUTS = {'tools/prepare-n64-model.py': 'cb09b4d4acffc56d5a26afb9f7e8ac287f0a2f255df019368c927dbf5a753c3d', 'app/source/cover_projection.hpp': '4206cb202dc57dc0c41fb15d6bd25e2274afc1f06fbd10444fcc20bb10c5546a', 'app/source/disc_model.hpp': 'd09677d02204bbffd4fba7b6ac6e18582f558ee1250decc7c9f0ca52aa227a55', 'app/source/n64_label_shape.hpp': '3f7e87695d481e0b15b34d580f407fa9eb356387db2cfef6d98f041c969e1255', 'app/source/perspective_geometry.hpp': '03b215316e25bcc04bcaeb43f2a7d0ca2b1368be025ba873722b677b0015e639', 'tools/prepare-genesis-model.py': '627ad6ebd10cfab11c1cdd7abda1566847bde45e1d997f88141a850964ccb52e', 'tools/make-genesis-relief.py': '9bcdc8fca02e637eb418fae6c76f3b7bae7f47289ddba04590ad8caf133afeb5', 'app/source/genesis_relief.hpp': '2c7109793fd3c04be0511a176502db3db5a93ceb1c3704828e48749feb3c3d40', 'app/source/genesis_cartridge_data.hpp': '70002ec6bf027f893fe922add1f7165e305f557c264c38a03610e7cf881c5d13', 'assets/models/genesis/genesis-cart.obj': 'fef13ebb76667bfb79537150b71a45d6454c2beac0a8c081892be91653cc1594', 'assets/models/genesis/genesis-cart-small.obj': '6d3960e8fb2a5f036b92b86034b2cd295ef42c3239bc1d0be6b9c07319e8ae3f', 'tools/prepare-hucard-model.py': 'eab191014acb6b5c8a1329ee76db8353c46db486c7ff2a900889e0b7e35f5dc9', 'assets/models/hucard/hucard-cart.obj': '68289fe367a7410ce4b6e0f93775bedf7b5aa8325b7c6438166d729a099965e5', 'assets/models/hucard/hucard-cart-small.obj': 'b71ed02f4be665b77cdfa5e5c31dd4cc5a46e3c472364dd2af0e237d6af5c5d6', 'app/source/hucard_cartridge_data.hpp': 'd0805fdfb0adf79b94d70511fb73e8655e365f25e304c998724832c52d73ea1c', 'tools/make-nes-back-label.py': 'c639a94d2f9cfd342eaf944bfa02147f9a784d9baa6234dfef7aa4e6a8fbe68b', 'tools/embed-cartridge-model.py': '1fbc1478f6786470d3da3751ec8f31aaa5f024bfffa8b721d62916ee4012f39d', 'app/source/nes_back_label.hpp': '3256d76c1c6acb259b00cdc00388cc1fa2ea358b291e5bb7a2210b5417deb6a1', 'app/source/nes_cartridge_data.hpp': 'c7a6a95b017df74719822f1ba2de89aa0e1b906228d023a63719e6cbf09dbce8', 'assets/models/nes/nes-cart.obj': 'cebe979768625dcebf702c74641104e0fcd4c60e6c2823826a419c7e4a56e8c3', 'assets/models/nes/nes-cart-small.obj': '7fa90e0077e9474888be2c17d7548d4b93c11a060c2f63a5348c6ecfda80523c', 'app/source/nes_cartridge.hpp': 'cd2d1e1caec6346154d30404a24712afd52b293e55a602dda5a50807c7f33be7', 'tools/prepare-snes-model.py': 'c69496f996cec4d8f5423cf003985103cf6a732e1306e2f0bb4c3c8b12887359', 'app/source/snes_cartridge.hpp': 'c687173c2d53ce7e6e2884be03196114b6233d9353db6e665cd5e4711f468ffa', 'app/source/snes_cartridge_data.hpp': 'b7bce5efa39d1078072e5ec5069f182c5c55318bcf42504b7c2365aed93087be', 'assets/models/snes/snes-cart.obj': '93c28e0a7ad1279333346fb22f76d6466dc65dd47ce255ea22af3f54b0c2fee2', 'assets/models/snes/snes-cart-small.obj': '4cd304a0f0792ee094bddbce467393a79b5563e98ac92418a0dfc5bfef91abae', 'tools/prepare-cartridge-models.py': '3c0568db87e3f3d15c5e86c4ffe9d543fa40a9570da758e4091645d20a51886b', 'tools/make-cartridge-brand-masks.py': 'd12463e7e21678894ed359180fd81f51b426a3d9868cfdec5e849e1f6abb59fc', 'app/source/cartridge_geometry.hpp': 'c9da46bf9537151ca53ff25e4a6626fa8486f6161c4821e6ae36f691a7902ab0', 'app/source/cartridge_models.hpp': 'cf7479f117c771463ef6a1f564dd5f741e565ceb948ec32ed540a43fd8c11bb4', 'app/source/cartridge_brand_masks.hpp': '32572652ff01355cadad584d2dcb102c33f77488f37fcca2a90ceceed2686634', 'app/source/n64_cartridge_data.hpp': '47fc2606c814e5af4ee8b60bdddfebbde551b6946f7d77bebcbbee365afcd6a7', 'assets/models/n64/n64-cart.obj': '1210702f3485805d69999313ee5ecc4592f3cf718dfff4e29791b3255c4c7c0e', 'assets/models/n64/n64-cart-small.obj': '387424cccd0f177b614cc77ba7998862ab7a6a04f7ca17f925bdef8bc5b90eb2', 'app/source/gba_cartridge_data.hpp': '762de8c53cf25f2464145d41118cfcbd827c1633d1685c84456f21b5066a5177', 'assets/models/gba/gba-cart.obj': 'e98b4950391a34756fb65d55b3d703a79d8a9be09284eb12387d446bc021258f', 'assets/models/gba/gba-cart-small.obj': 'db5b45df7a315a995bb0cfb7433bd9066f8b4e71154d3bdf8c293d14be95301b', 'app/source/gb_cartridge_data.hpp': 'f43e1c9dcbe2d6e299970bfe366aff94da36d33aa46bde7b5c9551591f801e3b', 'assets/models/gb/gb-cart.obj': 'a5b05554d915a362647762b314660cd225fa42c4bbbd287c75a95962dead32db', 'assets/models/gb/gb-cart-small.obj': '5352b236772386e47d26c98a8b19f61bf2c8a531fa1f09f7b282e2bcde6dc5d6', 'app/source/ds_cartridge_data.hpp': '80525ee6b4a7dc0ac66377705b1134056c3ea52c894faee3d08adfc1405a152e', 'assets/models/ds/ds-cart.obj': 'f62f9cce789e208808fa45fad0cad2813b8692cf4a8b68b253455ca5b8b35861', 'assets/models/ds/ds-cart-small.obj': '4a0fbad7d5f89eb29be9cc45e20088bc6768d4d96ceef9a2e2d9ebc00e3f6c00'}
WONDERSWAN_INPUTS = {'tools/prepare-wonderswan-model.py': '1361228ea1af1e29f37a65ace2a239a7d97b3deb9cdc7f0862ef7798f38d11e6', 'tools/make-wonderswan-material.py': '2f7ed3c042f8a5b395ee7eee246cfdc06f8619577a9df1689bdca8841f09097b', 'app/source/wonderswan_cartridge_data.hpp': 'ad03a38ba9153d27729b240fe9edb937f8f3bc58ce222832a2be1d856d3d9750', 'app/source/wonderswan_material_data.hpp': '82ffa3f02cf2f8bd4c6a360532496a2fca7eda5a7861a455a1d632a9e5d5aca8', 'assets/models/wonderswan/wonderswan-cart.obj': 'c3a65d18a19fdb2dd70d132e21ae44be69318808feb6ea6ec34105e51e1ef452', 'assets/models/wonderswan/wonderswan-cart-small.obj': '890e1231ed6562836c4007bf995dbc96cd10e4d1271d03edd840bdce4de9f97d'}
WIIFLOW_BINS = ("app_booter.bin", "ext_loader.bin", "ext_booter.bin")
WIIFLOW_BIN_PATHS = {f"apps/wiiflow/bins/{name}" for name in WIIFLOW_BINS}
WIIFLOW_FORWARDER = "wiiu/apps/WiiFlow.wuhb"
RETURN_RUNTIME = {
    CONTENT_REDIRECTION,
    PLUGIN_BACKEND,
    "wiiu/environments/aroma/modules/RPXLoadingModule.wms",
    "wiiu/environments/aroma/modules/uFlowRawKernel.wms",
    "wiiu/environments/aroma/modules/setup/00_mocha.rpx",
}
# WPS payload is compressed: validate the tested patched artifact, not strings.
# Update this pin only after testing a replacement Region Free build.
REGION_PATCH_HASH = "30560de32d190999faddd37a6ac88b430b7bc9f3e7a026adbeac7d439aaca49f"
# Optional RetroArch components: the Aroma RetroArch build that was
# hardware-verified with one-press launching (PlayStation, bit 13), and the
# Snes9x / Nestopia UE / mGBA cores from the same archive (bits 14-16). The
# frontend ships with any of them. Pinned so a different emulator build can
# never slip into a release unnoticed. Kept ('k') on the SD so an existing
# RetroArch install is never overwritten.
PLAYSTATION, SNES_RETROARCH, NES_RETROARCH, GBA_RETROARCH = 1 << 13, 1 << 14, 1 << 15, 1 << 16
RETROARCH_SYSTEMS = PLAYSTATION | SNES_RETROARCH | NES_RETROARCH | GBA_RETROARCH
RETROARCH_INPUTS = {
    "retroarch/cores/info/mednafen_wswan_libretro.info": ("mednafen_wswan_libretro.info", "ac8fe7cd930c31ae6355ec51bfc05f0b12b19ee39720547127688142e821a6c2", RETROARCH_SYSTEMS),
    "retroarch/cores/mednafen_wswan_libretro.rpx": ("mednafen_wswan_libretro.rpx", "453d37796b4da2a77ae94c4a4b4413961cb6b0d22099f2797ea08a2f278a48c4", RETROARCH_SYSTEMS),
    "retroarch/cores/info/genesis_plus_gx_libretro.info": ("genesis_plus_gx_libretro.info", "9793bff8d9e298a7ee0c94c0511dab242200ca60fbe87e3720eb9231a3e0166a", RETROARCH_SYSTEMS),
    "retroarch/cores/genesis_plus_gx_libretro.rpx": ("genesis_plus_gx_libretro.rpx", "10fc5b4f6f043d75873cccda7ca0ef0d625aa39074251868a5acf63d02637b3e", RETROARCH_SYSTEMS),
    "retroarch/cores/info/genesis_plus_gx_wide_libretro.info": ("genesis_plus_gx_wide_libretro.info", "8aa2205e8a3ea2cf2ed04d5ae83eed52c4e9ca6b8d2722afa2bb853a9939357c", RETROARCH_SYSTEMS),
    "retroarch/cores/genesis_plus_gx_wide_libretro.rpx": ("genesis_plus_gx_wide_libretro.rpx", "75aaddac0c6d4a09c568fd67e16a38b6d8205d59e33bf707291c22205d50ceae", RETROARCH_SYSTEMS),
    "wiiu/apps/retroarch.wuhb": ("retroarch.wuhb", "487c77a863348c44fa87df34594962cf0390d905594430d09a05e45c67ddc630", RETROARCH_SYSTEMS),
    "retroarch/cores/pcsx_rearmed_libretro.rpx": ("pcsx_rearmed_libretro.rpx", "1bdcae8ef63ce250c31c75468f53be8095de6e5626c32a2a93f9af6825ca3ac4", PLAYSTATION),
    "retroarch/cores/info/pcsx_rearmed_libretro.info": ("pcsx_rearmed_libretro.info", "19e8859d2c4b6b09ef2ccf8b0ef8e4ad2bcdbeede063da33a664719e605b8e75", PLAYSTATION),
    "retroarch/cores/snes9x_libretro.rpx": ("snes9x_libretro.rpx", "33ed3fdad36b59a02e03b0a34b57322b7b4b276cdc422ad742544cdd0349ed95", SNES_RETROARCH),
    "retroarch/cores/info/snes9x_libretro.info": ("snes9x_libretro.info", "fa62b78d58bc4c30f4e0a4a581f0cb3252447b7f198b55bd6ebe2e4b9bde5cc2", SNES_RETROARCH),
    "retroarch/cores/nestopia_libretro.rpx": ("nestopia_libretro.rpx", "9f2fd9b96d3ab614e5c1dbabceecfad82f473a0103a6406a0a5cb11cbc73ab3c", NES_RETROARCH),
    "retroarch/cores/info/nestopia_libretro.info": ("nestopia_libretro.info", "3aedf30570ef2f751eee31d06022243ec5ae13205b1ab17b880a077651c27e87", NES_RETROARCH),
    "retroarch/cores/mgba_libretro.rpx": ("mgba_libretro.rpx", "fc801cb92ff8c48a82d463a4efa522521d1264d27c7450e87b09a5c5b6517125", GBA_RETROARCH),
    "retroarch/cores/info/mgba_libretro.info": ("mgba_libretro.info", "64444beb8268d3a57d53a45564c55049f39ff621315401fef86e82f3849042d6", GBA_RETROARCH),
}
RETROARCH_MODULES = {
    f"wiiu/apps/uFlow/launchers/retroarch-{system}.ufmod": (f"retroarch-{system}.ufmod", mask)
    for system, mask in (("ps1", PLAYSTATION), ("snes", SNES_RETROARCH), ("nes", NES_RETROARCH), ("gba", GBA_RETROARCH))
}
# User-provided sleeves are release assets, not machine-specific Media paths.
# DS also applies when only the alternative NooDS backend is selected, and
# NES/SNES/GBA when only their RetroArch component is.
DEFAULT_COVERS = {"wiiu": 1 << 0, "ds": (1 << 3) | (1 << 10), "wii": 1 << 1, "gamecube": 1 << 2, "nes": (1 << 4) | NES_RETROARCH, "snes": (1 << 5) | SNES_RETROARCH, "gba": (1 << 6) | GBA_RETROARCH, "n64": 1 << 7, "pce": 1 << 8, "msx": 1 << 9, "gb": 1 << 31, "genesis": 1 << 31, "ps1": PLAYSTATION}


DEFAULT_COVERS["wsc"] = 1 << 31
DEFAULT_LABELS = {system: DEFAULT_COVERS.get(system, 1 << 31) for system in
                  ("nes", "snes", "n64", "gb", "gbc", "gba", "ds", "genesis", "pce", "wonderswan", "wsc", "msx")}

def cover_paths():
    return {f"wiiu/apps/uFlow/media/{system}/fullcovers/Default.png{suffix}": mask
            for system, mask in DEFAULT_COVERS.items() for suffix in ("", ".layout")} | {
            f"wiiu/apps/uFlow/media/{system}/labels/Default.png": mask for system, mask in DEFAULT_LABELS.items()}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


BLOOPAIR = "wiiu/environments/aroma/modules/setup/30_bloopair.rpx"
BLOOPAIR_HASH = "5d777a1d487e7911bd9a9424f7efbb417ceea385eec060239f7d336ad67ac95b"

def inputs(project, sources):
    result = {
        BLOOPAIR: project / "app/content/controllers/30_bloopair.rpx",
        "wiiu/bloopair/LICENSE": project / "app/content/controllers/LICENSE",
        SIGPATCHES: sources / "SigpatchesModuleWiiU/01_sigpatches.rpx",
        TITLE_INSTALLER: project / "launcher-modules/title-installer.ufmod",
        "wiiu/apps/uFlow/uFlow.wuhb": project / "app/uFlow.wuhb",
        "wiiu/environments/aroma/modules/RPXLoadingModule.wms": project / "module/RPXLoadingModule.wms",
        "wiiu/environments/aroma/modules/uFlowRawKernel.wms": project / "kernel/uFlowRawKernel.wms",
        "wiiu/environments/aroma/modules/setup/00_mocha.rpx": project / "payload/00_mocha.rpx",
        REGION: sources / "region_free_plugin/regionfree.wps",
        FTPIIU: sources / "ftpiiu_plugin/ftpiiu.wps",
        PLUGIN_BACKEND: sources / "WiiUPluginLoaderBackend/PluginBackend.wms",
        MEMORY_MAPPING: sources / "MemoryMappingModule/MemoryMappingModule.wms",
        CONTENT_REDIRECTION: sources / "ContentRedirectionModule/ContentRedirectionModule.wms",
        "wiiu/environments/aroma/modules/setup/99_autoboot.rpx": sources / "AutobootModule/99_autoboot.rpx",
        "apps/wiiflow/boot.dol": sources / "wiiflow-lite/out/boot.dol",
        WIIFLOW_FORWARDER: sources / "Boot2vWii/uFlow-WiiFlow.wuhb",
        "apps/usbloader_gx/boot.dol": sources / "usbloadergx/boot.dol",
        "apps/nintendont/boot.dol": sources / "nintendont/loader/loader.dol",
        "wiiu/apps/noods/noods.wuhb": project / "emulators/NooDS/noods.wuhb",
        "wiiu/apps/noods/uflow-api.txt": project / "launcher-modules/noods-capability.txt",
    }
    for skin in ("pixel", "glass"):
        result[f"wiiu/apps/uFlow/skins/{skin}/skin.ini"] = project / f"app/content/skins/{skin}/skin.ini"
    for name in MUSIC_INPUTS:
        result[f"wiiu/apps/uFlow/music/{name}"] = project / "native-installer/assets/music" / name
    for name in ("ds-vc", "nes-vc", "snes-vc", "gba-vc", "n64-vc", "pce-vc", "msx-vc", "noods"):
        result[f"wiiu/apps/uFlow/launchers/{name}.ufmod"] = project / f"launcher-modules/{name}.ufmod"
    for name in WIIFLOW_BINS:
        result[f"apps/wiiflow/bins/{name}"] = sources / f"wiiflow-lite/out/bins/{name}"
    for path, (name, _) in RETROARCH_MODULES.items():
        result[path] = project / "launcher-modules" / name
    for path, (name, _, _) in RETROARCH_INPUTS.items():
        result[path] = sources / "retroarch-aroma" / name
    for system in DEFAULT_COVERS:
        result[f"wiiu/apps/uFlow/media/{system}/fullcovers/Default.png"] = project / f"native-installer/assets/default-covers/{system}.png"
        result[f"wiiu/apps/uFlow/media/{system}/fullcovers/Default.png.layout"] = project / f"native-installer/assets/default-covers/{system}.layout"
    for system in DEFAULT_LABELS:
        result[f"wiiu/apps/uFlow/media/{system}/labels/Default.png"] = project / f"native-installer/assets/default-labels/{system}.png"
    return result


VWII_DIAGNOSTIC_INPUTS = {'native-installer/source/vwii.cpp': '3e7988668e91da2275865c57a5656d9933b886a550a2960c25db7f84f0816ff9', 'native-installer/source/vwii.hpp': 'd2f8bd7f0a89ca3f5f699d182a4775a1a2397752ffa3d00079545c144e93968a', 'native-installer/vendor/compat/d2x_patcher.cpp': '9464736d1565af3673e165f99cf90d63f6e738faba7ee61fa9a82865051c42d1', 'native-installer/vendor/compat/ios_common.cpp': '6c155f38c56abcee90ff690d95bcc43a8bfaceaaf2b77bf4c0e10836adef220c'}

CONTROLLER_INPUTS = {'app/source/wii_remote.hpp': '574d94550d70a51606eb8e2be8574b850e56ba468a47570e1cf08136925cc020', 'app/source/controller_manager.hpp': '6551f6872f4d1ea2289e720aa4d3180137a8a0334b730354f13b44f12f15676c', 'app/source/controller_settings_members.hpp': '710b75a2ad002dab4320efaf6712415c7930ef72a3c97fea929d3fd037f06c30', 'app/vendor/bloopair/source/bloopair.c': '18b47fb6623f61c39a9c5449f773027771afc3d6c7670af03497d11d8bf9bc61', 'app/vendor/bloopair/include/bloopair/bloopair.h': '8772b23cae2ac2316053258d4e8732b0158066194bdccff4cea6b713475fcf01', 'app/vendor/bloopair/include/bloopair/ipc.h': '9379e677e09e6aa0e08494064c563ecd77d06746c8b096eba2cfe64b34801e97', 'app/vendor/bloopair/include/bloopair/controllers/common.h': 'ca3ebbcb787900abfb0dab8dab8bd84ff5c247d84a1033b46666ce2d0c3f10a3', 'app/source/controller_pairing.hpp': '592ccccd6242178c83c2fdf32b4f4f31bcd4cfdd2982b942de1d91c5c8db4a6f'}

def synchronize(project, sources, sync=False):
    adapter = project / H264_ADAPTER
    if not adapter.is_file() or hashlib.sha256(adapter.read_text(encoding="utf-8").encode("utf-8")).hexdigest() != H264_ADAPTER_HASH:
        raise ValueError("H264 adapter differs from the tested stream-sized workspace build")
    for name, expected in (WONDERSWAN_INPUTS | COVER_CACHE_INPUTS | VIDEO_COLOUR_INPUTS | VIDEO_NV12_INPUTS | HANDOFF_INPUTS | CARTRIDGE_INPUTS | RETROARCH_LAUNCHER_INPUTS | PREPARED_MODEL_INPUTS | GLASS_POPUP_INPUTS | CONTENT_TYPE_SAVE_INPUTS | BROWSER_PATH_INPUTS | SCOPED_MEDIA_INPUTS | SKIN_PACKAGE_INPUTS | NUMBER_DEFAULT_INPUTS | VWII_DIAGNOSTIC_INPUTS | CONTROLLER_INPUTS).items():
        source = project / name
        if not source.is_file() or hashlib.sha256(source.read_text(encoding="utf-8").encode("utf-8")).hexdigest() != expected:
            raise ValueError("Renderer dependency differs from the tested build: " + name)
    native = project / "native-installer"
    manifest = native / "source/generated_files.hpp"
    text = manifest.read_text(encoding="utf-8")
    entries = {m[1]: m for m in ENTRY.finditer(text)}
    if len(entries) != len(list(ENTRY.finditer(text))):
        raise ValueError("Duplicate native manifest entries")
    replacements, copies, stale = {}, [], []
    retired = {"wiiu/apps/uFlow/skins/uforce/skin.ini"}
    retired.update(f"wiiu/apps/uFlow/skins/{skin}/{name}" for skin in ("switch-light", "switch-dark") for name in ["skin.ini"]+[f"icon-{i}.png" for i in range(6)])
    for path in retired:
        if path in entries or (native / "content/payload" / path).exists():
            stale.append("removed download-only skin: " + path)
    for path, source in inputs(project, sources).items():
        if not source.is_file():
            raise ValueError(f"Build input missing: {source}; build components and mount sources first")
        hashed, size = digest(source), source.stat().st_size
        if path in NETWORK_DISABLED_INPUTS and hashed != NETWORK_DISABLED_INPUTS[path]:
            raise ValueError("Input differs from network-debug-disabled build: " + path)
        music_name = path.removeprefix("wiiu/apps/uFlow/music/")
        is_music = path.startswith("wiiu/apps/uFlow/music/") and music_name in MUSIC_INPUTS
        if is_music and (hashed != MUSIC_INPUTS[music_name] or not 0 < size <= 32 * 1024 * 1024):
            raise ValueError("Bundled menu music differs from the verified SD track: " + path)
        if path == BLOOPAIR and hashed != BLOOPAIR_HASH:
            raise ValueError("Bloopair differs from pinned upstream v1.1.0")
        if path == REGION and hashed != REGION_PATCH_HASH:
            raise ValueError("Region Free input lacks the uFlow raw-metadata fix")
        if path == PLUGIN_BACKEND and hashed != PLUGIN_BACKEND_HASH:
            raise ValueError("Plugin backend input differs from tested exit-trace build")
        if path == MEMORY_MAPPING and hashed != MEMORY_MAPPING_HASH:
            raise ValueError("MemoryMapping input differs from tested heap-gate build")
        if path == CONTENT_REDIRECTION and hashed != CONTENT_REDIRECTION_HASH:
            raise ValueError("ContentRedirection input differs from tested missing-file removal build")
        if path == SIGPATCHES and hashed != SIGPATCHES_HASH:
            raise ValueError("Signature module differs from pinned upstream release 1.2")
        old = entries.get(path)
        if path == FTPIIU and hashed != FTPIIU_PATCH_HASH:
            raise ValueError("FTPiiU input differs from tested early-exit and idle-poll build")
        if path in RETROARCH_INPUTS and hashed != RETROARCH_INPUTS[path][1]:
            raise ValueError(f"RetroArch input differs from the pinned build: {path}")
        new_component = path.startswith("wiiu/apps/uFlow/skins/") or is_music or path in (TITLE_INSTALLER, SIGPATCHES, BLOOPAIR, "wiiu/bloopair/LICENSE") or path in RETROARCH_INPUTS or path in RETROARCH_MODULES
        if old is None and not new_component and path not in (REGION, FTPIIU, PLUGIN_BACKEND, CONTENT_REDIRECTION, MEMORY_MAPPING, WIIFLOW_FORWARDER) and path not in cover_paths() and path not in WIIFLOW_BIN_PATHS:
            raise ValueError(f"Required component missing from manifest: {path}")
        if path.startswith("wiiu/apps/uFlow/skins/"):
            mask, mode = 1 << 31, "k"
        elif is_music:
            mask, mode = 1 << 31, "k"  # Core music; preserve user replacements and preferences.
        elif path in (BLOOPAIR, "wiiu/bloopair/LICENSE"):
            mask, mode = 1 << 31, "k"  # Core controller support; preserve custom/newer modules.
        elif path == SIGPATCHES:
            mask, mode = 1 << 17, "k"  # Optional installer; preserve an existing module.
        elif path == TITLE_INSTALLER:
            mask, mode = 1 << 17, "r"
        elif path in RETROARCH_INPUTS:
            mask, mode = RETROARCH_INPUTS[path][2], "k"  # never replace a user's own RetroArch
        elif path in RETROARCH_MODULES:
            mask, mode = RETROARCH_MODULES[path][1], "r"
        elif path in WIIFLOW_BIN_PATHS or path == WIIFLOW_FORWARDER:
            mask, mode = 6, "r"  # Both Wii and GameCube need WiiFlow's external booters.
        elif path in cover_paths():
            mask, mode = cover_paths()[path], "k"  # Never replace a user's sleeve.
        elif path in (PLUGIN_BACKEND, FTPIIU, CONTENT_REDIRECTION, MEMORY_MAPPING):
            mask, mode = 1 << 31, "r"
        else:
            mask, mode = (RAW, "r") if path == REGION else (int(old[4]), old[5])
        if path in RETURN_RUNTIME:
            mask, mode = 1 << 31, "r"  # Core: game return also serves Wii/GC/NooDS.
        line = f'    {{"{path}","{hashed}",{size}ULL,{mask}u,\'{mode}\'}},'
        replacements[path] = line
        dest = native / "content/payload" / path
        if not dest.is_file() or digest(dest) != hashed:
            stale.append(path)
            copies.append((source, dest))
        if old is None or old[0] != line:
            stale.append(f"manifest: {path}")
    font = native / "content/font.ttf"
    if not font.is_file() or digest(font) != digest(project / "app/data/glass.ttf"):
        stale.append("font.ttf")
        copies.append((project / "app/data/glass.ttf", font))
    # Check every unmanaged bundled asset too; do not silently bless altered bases.
    for path, old in entries.items():
        if path in replacements or path in retired:
            continue
        dest = native / "content/payload" / path
        if not dest.is_file() or dest.stat().st_size != int(old[3]) or digest(dest) != old[2]:
            raise ValueError(f"Unmanaged bundled asset differs from manifest: {path}")
    if stale and not sync:
        raise ValueError("Stale native payload; run tools/sync-native-payload.py --sync:\n" + "\n".join(stale))
    if sync:
        # Inputs and existing assets all checked before changing the payload.
        for source, dest in copies:
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, dest)
            if digest(source) != digest(dest):
                raise ValueError(f"Payload copy readback failed: {dest}")
        for path in retired:
            (native / "content/payload" / path).unlink(missing_ok=True)
        updated = ENTRY.sub(lambda m: "" if m[1] in retired else replacements.get(m[1], m[0]), text)
        added = [line for path, line in replacements.items() if path not in entries]
        if added:
            updated = updated.replace("return {\n", "return {\n" + "\n".join(added) + "\n", 1)
        if updated != text:
            manifest.write_text(updated, encoding="utf-8", newline="\n")
    print(f"PASS native release payload: {len(replacements)} current components, all bundled assets verified")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sync", action="store_true")
    parser.add_argument("--sources-root", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sources = args.sources_root or Path(os.environ.get("UFLOW_SOURCES_ROOT", root.parent.parent / "tooling/sources"))
    synchronize(root, sources, args.sync)
