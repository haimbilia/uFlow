# Updater dependencies

- Monocypher 4.0.2, including its optional Ed25519 implementation:
  https://github.com/LoupVaillant/Monocypher/tree/4.0.2/src
  BSD-2-Clause or CC0; license text is retained in the source headers.
- picojson 1.3.0: https://github.com/kazuho/picojson/tree/v1.3.0
  BSD-2-Clause; license text is retained in picojson.h.
- `app/content/updates/cacert.pem`: Mozilla CA bundle distributed by
  https://curl.se/docs/caextract.html. Refresh as part of updater releases.

The public verification key is compiled into the app. No private signing key
is stored in this repository or downloaded by the client.
