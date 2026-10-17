# POS 0.6.5-dev changes and validation

- **Gain capped at 64×:** settings used uint8/power-of-two validation and the driver
  rejected other gains; UI offered only presets. Format 5 uses uint16, accepting
  integers 1–4096; formats 1–4 still decode under their original rules. Numeric
  entry validates before saving; live gain stays atomic, integer multiply fits
  int32 (32768×4096), and signed PCM clamps before conversion. No capture-time
  blocking text dialog or credential/settings write was added. Existing digital
  gain amplifies noise too; physical loudness and clipping need retesting.
- **No global context in Maps:** regional road projection cannot represent the
  planet/poles/dateline. A separate bounded equirectangular overview streams pinned
  public-domain Natural Earth coastline segments from SD, checks CRC/count/ranges,
  splits seam crossings, and fits the planet around the GPS camera. No false
  location pin without a current fix. Returning to the menu clears borrowed
  raster pointers before freeing. Detailed regional tools are retained.
- **Unwanted legacy passphrase UI:** removed its menu entry and import branch;
  known profiles and encrypted storage code remain. Existing legacy vault bytes
  are untouched. Missing SSIDs can be saved again through normal Wi-Fi setup.
- **Validation:** all 367 host tests pass; tests exercise real PCM driver behavior using timed fake I2S,
  numeric UI/persistence/migrations, menu removal and Known Networks operations,
  world zooms/poles/date line/corruption and UI Home cleanup. Both ESP-IDF roles
  retain existing partition and stack guards. Screen previews use the actual
  compositor and an example GPS coordinate, not live hardware evidence.

No physical Cardputer, GPS receiver or real microphone/SD hardware was available.
