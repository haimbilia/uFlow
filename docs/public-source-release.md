# Public edition release policy

The GitHub edition excludes the private Cloud service, media downloader and skin downloader entirely from source, assets and history. The sole Karo network client is the official signed application updater. Support reports stay on the SD card. Manual artwork and standalone skin loading remain available.

Official updates use the normal channel and replace the public edition with the full Karo binary. No persistent restriction or separate update channel is used.

Build the public source independently and audit the staged tree, every outgoing commit, source archive and binary. Never publish the private development checkout. Run tools/check-public-source.py before committing and pushing.
