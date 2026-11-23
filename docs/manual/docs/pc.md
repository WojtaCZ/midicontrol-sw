# Konfigurace počítače { #pc }

Program běží v Dockeru. Data jsou v `/opt/midicontrol/data/`: `config.conf`, `songs/` (písně), `recordings/` (nahrávky), `diag/`.

Nastavení v `config.conf`:

| Sekce, klíč | Výchozí | Význam |
|---|---|---|
| `[FILES]` `PlayFolder`, `RecordFolder` | | složky písní a nahrávek |
| `[PLAYBACK]` `WarmupMs` | 3000 | čekání po zapnutí varhan, než píseň začne (ms) |
| `[PLAYBACK]` `KeepPowerOn` | false | `true` nechá varhany zapnuté i po písni |
| `[PLAYBACK]` `VerseGapMs` | 0 | pauza mezi slokami (ms) |
| `[PLAYBACK]` `OrganIdleOffMs` | 1800000 | po jaké nečinnosti počítač vypne varhany (ms, nejméně 1000; `0` = nikdy nevypínat, varhany zůstanou zapnuté, dokud je nevypnete ručně) |
| `[WEB]` `Port` | 50118 | port webové stránky |
| `[WEB]` `AllowedHosts` | | další jména, pod kterými lze webovou stránku otevřít a ukládat; `*` povolí jakékoli jméno |

Ukládání na webu (rejstříky, firmware) počítač přijme, jen když je v adrese IP adresa, `localhost`, vlastní jméno počítače (i `<jméno>.local`), `midicontrol` / `midicontrol.local` nebo jméno z `AllowedHosts`. Jinak odpoví chybou 403.

## Časovače

| Co | Po jaké době |
|---|---|
| Displej ovladače a základny zhasne | 30 s |
| Ovladač se sám vypne (ne s USB kabelem) | 20 min bez stisku |
| Počítač vypne varhany | 30 min nečinnosti (nastavitelné: `OrganIdleOffMs`; `0` = nevypínat); počítač čas měří, i když varhany po písni nechává zapnuté (`KeepPowerOn`); měří ho i po ručním zapnutí varhan (konzole nebo `Napajeni varhan` na základně) |
| Základna vypne varhany, když počítač mlčí | 6 s, nebo 2 h bez zprávy od počítače |
