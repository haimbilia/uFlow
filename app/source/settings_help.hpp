#pragma once
#include <string>
#include <utility>
namespace SettingsHelp {
inline std::string ContentTypeLabel(int row) {
    static const char *labels[]={"NAME","SCANNER","EXTENSIONS","LAUNCHER","ENABLED","RAIL POSITION","MEDIA BY CONTENT TYPE","BOX MODEL","MEDIA MODEL","VIEW TYPE","LAYOUT","CASE WIDTH","CASE HEIGHT","CASE THICKNESS","BOX COLOR"};
    return row>=0 && row<15?labels[row]:"CONTENT TYPE";
}
inline std::string ContentTypeText(int row) {
    static const char *help[]={
        "The name shown for this content type in the library rail and folder settings. Use a system name, such as Game Boy Advance, or a name for your own collection.",
        "Chooses how game files are recognized and read. Select the scanner appropriate for the games in this content type. The launcher is chosen separately.",
        "Lists the file extensions this content type accepts, such as .gba or .zip. Separate multiple extensions with commas. Only include formats supported by your launcher.",
        "Sets this content type's default launcher. A game's own default takes priority. Configure the emulator or core in Launchers first; changing this field does not install it.",
        "Enables this content type for use in the library. Turning it off keeps its configuration and does not delete games or saves.",
        "Sets the order of this content type in the library rail. Lower positions appear earlier. It does not change the order of games within the content type.",
        "Uses this content type's media association when looking up artwork. This lets original-system artwork be used for games presented under that system, including Virtual Console titles.",
        "Chooses the prepared 3D package used for box artwork, such as a DVD case, cardboard box or PS1 jewel case. The small preview shows the model. This affects appearance, not how games launch.",
        "Chooses the prepared 3D object shown in Media view, such as a cartridge, disc or jewel case. Use box model shows this content type's box model and cover artwork instead. A model does not download labels or disc art.",
        "Overrides the Display view for this content type: Box, Logo, Media or Thumbnails. Display default inherits the global choice. All and Favorites continue to use Display settings.",
        "Chooses the arrangement for this content type's view, such as Media Bookshelf or a wall. Available layouts depend on View Type. Display default inherits the global layout; changing this field creates an override.",
        "Sets the case width in millimeters for proportional box presentation. Use the package's width, not the image width in pixels. Prepared models may use their own proportions.",
        "Sets the case height in millimeters for proportional box presentation. Different systems can have different heights. Prepared models may use their own proportions.",
        "Sets the case thickness in millimeters, affecting box depth and spine proportions. This does not change the artwork file. Prepared models may use their own proportions.",
        "Tints only the box material for this content type. Cover images, full sleeves, labels and discs keep their original colors. Choose a swatch or adjust Red, Green and Blue with Left/Right or the touch sliders. Auto retains the model/skin material. Done accepts the color into the draft; Cancel restores the previous color. Save the content type to keep it."
    };
    return row>=0 && row<15?help[row]:"Select a field to configure this content type. Save keeps the changes.";
}
inline std::string Text(const std::string &label){
    static const std::pair<const char*,const char*> entries[]={
        {"GENERAL","Choose the skin, startup library, navigation behavior and default A-Z mode. Audio settings are also here."},
        {"LIBRARY","Add folders containing games, choose their content type and launcher, then scan. Add new games keeps existing catalog entries; a complete scan rebuilds the catalog."},
        {"MEDIA","Configure folders for covers, logos, backgrounds and previews. Match artwork by title ID or game name. A library-specific folder takes priority over an all-libraries folder."},
        {"DISPLAY","Choose how the library is presented. Layout, spacing, artwork and animation settings affect browsing; they do not change your game files."},
        {"CONTROLS","Select an action to remap it. Hold the desired buttons together and release to assign a combination. Help defaults to Minus and can be remapped here."},
        {"LAUNCHERS","Choose how games are started. Point each launcher at its installed executable or core and assign compatible file extensions. Removing an entry does not delete the emulator or games."},
        {"SKIN HELP","Install standalone skin packages manually under wiiu/apps/uFlow/skins on the SD card, then choose them using Skin. Existing skins are preserved on reinstall."},
        {"SKIN","Changes the interface appearance. Layout and artwork settings remain available within the selected skin."},
        {"START VIEW","Chooses the library shown when uFlow starts. Select All to browse every enabled library."},
        {"WRAP NAVIGATION","When enabled, browsing past the first or last game continues from the other end of the library."},
        {"SHOW INSTALLED WII U","Includes Wii U titles installed on the console in your library."},
        {"AUTO SORT INSTALLED VC","Groups installed Virtual Console games into their original systems instead of keeping them all under Wii U."},
        {"AUDIO","Open music and menu sound settings. These controls do not change the audio settings inside games."},
        {"START UFLOW ON BOOT","Starts uFlow automatically at console boot. Hold Plus during boot to bypass it."},
        {"TITLE INSTALLER","Shows supported installable title packages in the library. The title installer module must be included."},
        {"DEMO MODE","Automatically browses the library after a period without input. Any input returns control to you."},
        {"IDLE TIME","Sets how long uFlow waits without input before starting Demo Mode."},
        {"DEFAULT A-Z MODE","Browse jumps to a letter without filtering the list. Filter shows only games beginning with the selected letter. This choice is used each time A-Z opens; its temporary mode toggle does not change this default."},
        {"MENU MUSIC","Plays your selected music track while browsing the library."},
        {"TRACK","Choose a track from the uFlow music folder. Supported formats are MP3, OGG and WAV; each file must be at most 32 MiB."},
        {"VOLUME","Adjusts menu music volume. Preview videos have their own volume setting."},
        {"RELOAD MUSIC LIST","Looks for new tracks in SD:/wiiu/apps/uFlow/music without restarting uFlow."},
        {"MENU SOUND EFFECTS","Enables sounds for interface actions such as selection and confirmation."},
        {"EFFECTS VOLUME","Adjusts interface sound volume independently of menu music."},
        {"COVER TYPE","Choose boxes, logos, discs/media or thumbnails. The available layouts depend on this choice."},
        {"COVER LAYOUT","Changes how the selected cover type is arranged. This is independent of the skin."},
        {"ANIMATION","Changes the speed of interface transitions."},
        {"COVER SPACING","Changes the distance between neighboring covers."},
        {"COVER ZOOM","Changes the size of covers in the library layout."},
        {"INSPECTION ZOOM","Sets the magnification used when inspecting a cover."},
        {"BACKGROUND MOTION","Enables animated background movement while browsing."},
        {"REFLECTIONS","Shows reflections beneath artwork in layouts that support them."},
        {"ACCENT COLOR","Choose the highlight color used by the interface."},
        {"BACKGROUNDS","Shows matching game backgrounds when available in your media folders."},
        {"TITLE LOGOS","Shows matching game logos when available."},
        {"VIDEO PREVIEWS","Allows matching preview videos to play while browsing."},
        {"PREVIEW DELAY","Sets the delay before a selected game's preview begins."},
        {"PRELOAD COVERS","Prepares built-in cover icons ahead of browsing. This can increase startup preparation time."},
        {"STARTUP VIDEO","Plays the introductory video when uFlow starts."},
        {"SORT BY","Changes game ordering, such as title or play history. It does not rename or move files."},
        {"BACKGROUND TRANSPARENCY","Adjusts how strongly game backgrounds show through the interface. At 100 percent they are transparent."},
        {"DIRECTORY","Choose the folder to scan. Use the directory browser to select an SD or supported USB folder."},
        {"CONTENT TYPE","Assigns games from this folder to a system or custom library."},
        {"MEDIA TYPE","Choose the artwork stored in this folder, such as covers, backgrounds, logos or videos."},
        {"SCAN DEPTH","Controls how many levels of subfolders are searched. Larger values include deeper folders and can take longer to scan."},
        {"ENABLED","Includes this entry in scanning or launcher selection. Disabling it does not delete its files."},
        {"LAUNCHER","Choose the installed emulator or launch method used for games in this source."},
        {"AUTO SORT VC BY SYSTEM","Separates supported Virtual Console content into its original systems during scanning."},
        {"VIDEO START DELAY","Waits this long after selecting a game before starting its video."},
        {"VIDEO MODE","Choose how previews are presented, such as a background or a showcase view."},
        {"VIDEO VOLUME","Sets volume for videos from this media source. Zero mutes them."},
        {"FOREGROUND OPACITY","Controls how visible foreground artwork is over a background preview."},
        {"DEFAULT FILE","Optional fallback artwork used when a game has no matching media. It does not replace game-specific files."},
        {"NAME","Sets the label displayed for this entry in uFlow."},
        {"METHOD","Chooses the launch mechanism. Other fields change to match the selected method."},
        {"SYSTEM","Identifies the console system supported by this entry."},
        {"ROM EXTENSIONS","Lists the game-file extensions handled by this launcher."},
        {"EXECUTABLE","Select the installed emulator executable that uFlow should start."},
        {"CORE (.RPX)","Select the RetroArch core executable for this system."},
        {"VC BASE FOLDER","Select a compatible Virtual Console base containing code, content and meta folders."},
        {"CACHE LIMIT","Limits storage used by prepared injections. Your original ROMs and saves are not this cache."},
        {"ZIP COMPRESSION","Chooses between faster preparation and smaller compressed injection output."},
        {"BRIGHTNESS","Overrides the injection brightness where supported. Base default keeps the base title's setting."},
        {"LAUNCH MODULE","Connects uFlow to the emulator so it can pass the selected game directly. Without a compatible module, uFlow opens the emulator. The RetroArch module supports multiple launchers with different selected cores."},
        {"RETROARCH (.WUHB)","Select the installed RetroArch frontend bundle used with this launcher. The Core (.RPX) field selects the emulation core. Separate launcher entries can use different cores for the same system."},
        {"PIXEL ART UPSCALER","Overrides the DS Virtual Console base's pixel-art upscaling setting for prepared games. Base default keeps the base title's setting."},
        {"SAVE LAUNCHER","Saves the edited launcher configuration."},
        {"REMOVE ENTRY","Removes this configuration entry. Its game or emulator files are not deleted."},
        {"SETTINGS HELP","Opens help about the currently selected Settings item. Minus is the default; assign another button or combination in Controls."},
        {"SAVE REPORT","Saves a redacted diagnostic report on your SD card. You can share that file manually when requesting support. Nothing is uploaded."},
        {"PROBLEM RECORDING","Records recent app status events to help diagnose a problem. Start recording, reproduce the issue, then send a report."},
        {"CRASH REPORT NOTIFICATIONS","Offers to send saved diagnostic evidence after a suspected crash or freeze and restart. Disabling notifications does not send or delete local reports."}
    };
    for(const auto&e:entries)if(label==e.first)return e.second;
    return "Select this entry to edit its configuration. Save to keep your changes, or use Back to return. Changing configuration does not move your game files.";
}
}
