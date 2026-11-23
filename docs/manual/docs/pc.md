# Konfigurace počítače { #pc }

Program běží v Dockeru. Data jsou v `/opt/midicontrol/data/`: `config.conf`, `songs/` (písně), `recordings/` (nahrávky), `diag/`.

Nastavení v `config.conf`:

| Sekce, klíč | Výchozí | Význam |
|---|---|---|
| `[FILES]` `PlayFolder`, `RecordFolder` | | složky písní a nahrávek |
| `[PLAYBACK]` `WarmupMs` | 3000 | čekání po zapnutí varhan, než píseň začne (ms) |
| `[PLAYBACK]` `KeepPowerOn` | true | varhany zůstanou zapnuté i po písni a vypne je až časovač nečinnosti; `false` = vypnout hned po každé písni |
| `[PLAYBACK]` `VerseGapMs` | 0 | pauza mezi slokami (ms) |
| `[PLAYBACK]` `OrganIdleOffMs` | 1800000 | po jaké nečinnosti počítač vypne varhany (ms, nejméně 1000; `0` = nikdy nevypínat, varhany zůstanou zapnuté, dokud je nevypnete ručně) |
| `[WEB]` `Port` | 50118 | port webové stránky |

## Časovače

| Co | Po jaké době |
|---|---|
| Displej ovladače a základny zhasne | 30 s |
| Ovladač se sám vypne (ne s USB kabelem) | 20 min bez stisku |
| Počítač vypne varhany | 30 min nečinnosti (nastavitelné: `OrganIdleOffMs`; `0` = nevypínat); varhany po písni zůstávají zapnuté a počítač od konce písně měří čas nečinnosti |
| Základna vypne varhany, když počítač mlčí | 6 s, nebo 2 h bez zprávy od počítače |
