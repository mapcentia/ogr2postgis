# GUI: import af udvalgte filer + persisteret Postgres-forbindelse

## Mål

Færdiggør GUI'en (`src/ogr2postgis_gui.cpp`) så den kan:

1. Importere udvalgte filer/lag til PostGIS.
2. Konfigurere en Postgres-forbindelse i en settings-dialog, der persisterer over genstart.
3. Eksponere CLI-flag'ene som passende GUI-kontroller i stedet for rå flags.

## Arkitektur

### Bibliotek (`src/ogr2postgis.hpp`)

- `start()` rydder nu det globale `layers`-vector ved start (gør gentagne kørsler i GUI mulige)
  og findes i en overload, der tager `std::vector<std::string> paths` (multi-filvalg).
- Importblokken i `start()` udtrækkes til `runImport(config, cb3, cb4)`.
- Ny `importLayers(config, std::vector<layer> selection, cb3, cb4)`: sætter det globale
  `layers` til udvalget og kører `runImport`. Returnerer lagene med evt. fejl udfyldt.
  Kaster `std::runtime_error` hvis PG-forbindelsen ikke kan åbnes.

CLI'en (`ogr2postgis.cpp`) er uændret i adfærd.

### GUI (`src/ogr2postgis_gui.cpp`)

- **Menu**: File → Open Files… (multi-select), Open Folder…, Settings…, Exit.
- **Liste**: `wxListCtrl` med checkbokse (`EnableCheckBoxes`). Analyse udfylder rækkerne;
  rækker uden fejl markeres automatisk. Ny kolonne "Status" viser Imported/Failed.
- **Import**: "Import Selected…"-knap → Import-dialog → import i baggrundstråd via
  `importLayers`; gauge og tæller genbruges; resultater skrives tilbage i rækkerne.
- **Settings-dialog**: host, port, database, user, password (maskeret), "Test Connection"
  (åbner PG-datasource via GDAL). Gemmes med `wxConfig` ved Save.
- **Import-dialog** (mapper CLI-flags til kontroller, værdier persisteres som "sidst brugte"):
  - `--schema` → tekstfelt (default `public`)
  - `--nln` → valgfrit tekstfelt ("tom = brug lagnavne")
  - `--append`/`--truncate` → radiogruppe: Create/overwrite, Append, Append + truncate
  - `--p_multi` → checkbox
  - `--t_srs`, `--s_srs` → tekstfelter (EPSG:4326 / tom)
  - `--encoding` → tekstfelt (LATIN1)
  - `--timestamp` → valgfrit tekstfelt
  - `--autodetect`, `--x_possible_names`, `--y_possible_names` → CSV-gruppe
  - `--json` → ikke relevant i GUI
- **Persistens**: `wxConfig("ogr2postgis")` → fil i hjemmemappen. Forbindelse under
  `/connection/*`, importvalg under `/import/*`. Password gemmes i klartekst (noteret som
  kendt begrænsning).
- **Trådning**: al widget-opdatering fra worker-tråde sker via events/`CallAfter`
  (retter også eksisterende usikker `SetRange`-kald fra worker-tråd).
- Død/defekt kode fjernes: sorterings-stub med UB, debug-`cout`s, Yes/No-valgdialogen.

## Fejlhåndtering

- Manglende forbindelsesindstillinger ved import → åbn Settings-dialogen.
- PG-forbindelse fejler → fejldialog med GDAL-besked.
- Per-lag importfejl vises i Error-kolonnen og Status = Failed.

## Test

Ingen eksisterende testinfrastruktur. Verifikation: kompilering + manuel kørsel af GUI.
