# Konfigurace počítače { #pc }

Program běží v Dockeru. Data jsou v `/opt/midicontrol/data/`: `config.conf`, `songs/` (písně), `recordings/` (nahrávky), `diag/`.

Nastavení v `config.conf`:

| Sekce, klíč | Výchozí | Význam |
|---|---|---|
| `[FILES]` `PlayFolder`, `RecordFolder` | | složky písní a nahrávek |
| `[PLAYBACK]` `WarmupMs` | 3000 | čekání po zapnutí varhan, než píseň začne (ms) |
| `[PLAYBACK]` `KeepPowerOn` | false | `true` nechá varhany zapnuté i po písni |
| `[PLAYBACK]` `VerseGapMs` | 0 | pauza mezi slokami (ms) |
| `[WEB]` `Port` | 50118 | port webové stránky |

## Časovače

| Co | Po jaké době |
|---|---|
| Displej ovladače a základny zhasne | 30 s |
| Ovladač se sám vypne (ne s USB kabelem) | 20 min bez stisku |
| Počítač vypne varhany | 30 min nečinnosti (pevně dáno; jen pokud je sám zapnul) |
| Základna vypne varhany, když počítač mlčí | 6 s, nebo 2 h bez zprávy od počítače |
