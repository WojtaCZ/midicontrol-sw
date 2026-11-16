# Pro správce { #sprava }

Tato kapitola je pro osobu, která spravuje počítač s programem MIDIcontrol.
Varhaník ji ke službě nepotřebuje.

!!! warning
    Webová stránka programu nemá žádné ověření uživatele ani šifrování. Kdo se
    dostane na port stránky (výchozí 50118) z místní sítě, může měnit rejstříky
    písní a nahrávat firmware do základny a ovladače. Ve výchozím nastavení program
    naslouchá na všech síťových rozhraních (`Bind = 0.0.0.0`). Počítač proto
    nepřipojujte do nedůvěryhodné sítě. Adresu, na které program naslouchá,
    omezuje klíč `Bind` (viz [Konfigurace](#konfigurace)).

## Instalace programu na počítači { #instalace }

Program běží jako uživatelská služba systemd. Základna se k počítači připojuje
USB kabelem. Počítač zároveň vidí základnu jako sériový port a jako MIDI port
„MIDIControl“. Pro nahrávání se používá program `arecordmidi`.

Před začátkem: program je přeložený (`cmake --build build`). Uživatel, pod kterým
služba poběží, musí patřit do skupiny pro sériový port (`uucp` nebo `dialout`)
a do skupiny `audio`.

1. Nainstalujte program: `cmake --install build`. Do instalačního adresáře
   (prefixu) se uloží `bin/midicontrol`, `share/midicontrol/config.conf.example`
   a `lib/systemd/user/midicontrol.service`.
2. Vytvořte složku pro konfiguraci: `mkdir -p ~/.config/midicontrol`.
3. Zkopírujte vzorový soubor do ní, například
   `cp /usr/local/share/midicontrol/config.conf.example ~/.config/midicontrol/config.conf`
   (cesta závisí na prefixu).
4. Upravte `config.conf`: nastavte `PlayFolder` a `RecordFolder`. Obě složky musí
   existovat.
5. Načtěte službu: `systemctl --user daemon-reload`.
6. Spusťte a povolte službu: `systemctl --user enable --now midicontrol.service`.
7. Povolte start bez přihlášení: `loginctl enable-linger $USER`.
   Bez tohoto kroku se služba po zapnutí počítače sama nespustí.
8. Zkontrolujte: `systemctl --user status midicontrol`. Webová stránka odpovídá na
   `http://<adresa počítače>:50118/`. Základna nemusí být při startu připojena.

Po úpravě konfigurace službu restartujte: `systemctl --user restart midicontrol`.

Služba se spouští příkazem `midicontrol --headless`. Systemd ji hlídá
(`WatchdogSec=30`), po pádu nebo zaseknutí ji po 5 s spustí znovu
(`Restart=always`, `RestartSec=5`).

!!! warning
    Docker zatím není součástí hlavní verze. Instalace přes Docker existuje jen na
    nesloučené větvi `docker-deployment` a může se změnit. Tato kapitola ji
    nepopisuje. TODO: Až bude větev sloučena, doplnit postup (instalace, aktualizace, zálohy).
    Otázka pro majitele: kdy bude Docker sloučen a bude výchozí instalací?

## Konfigurační soubor { #konfigurace }

Program hledá soubor `config.conf` v tomto pořadí:

1. parametr `--config CESTA` (nebo `-c`),
2. proměnná prostředí `MIDICONTROL_CONFIG`,
3. `$XDG_CONFIG_HOME/midicontrol/config.conf`, případně
   `~/.config/midicontrol/config.conf`,
4. `config.conf` v aktuální složce.

Formát je INI: sekce `[NÁZEV]` a řádky `Klíč = hodnota`. Cesty smějí začínat `~/`;
relativní cesty se berou od složky se souborem. Logické hodnoty: `1`, `true`,
`yes`, `on` a `0`, `false`, `no`, `off` (na velikosti písmen nezáleží).
Nevyplněný volitelný klíč má výchozí hodnotu z tabulky.

| Sekce | Klíč | Výchozí | Význam |
|---|---|---|---|
| FILES | PlayFolder | povinný | Složka s písněmi (`.mid`) a soubory rejstříků (`.conf`). Musí existovat. |
| FILES | RecordFolder | povinný | Složka pro nahrávky. Musí existovat. |
| PLAYBACK | WarmupMs | 3000 | Čekání v ms mezi zapnutím varhan a první notou. Celé číslo, 0 a víc. |
| PLAYBACK | KeepPowerOn | false | `true` = varhany po písni nevypínat. |
| PLAYBACK | VerseGapMs | 0 | Ticho v ms před opakovanou slokou (přidání sloky). |
| WEB | Enabled | true | Zapnutí a vypnutí webové stránky. |
| WEB | Port | 50118 | Port stránky, 1 až 65535. |
| WEB | Bind | 0.0.0.0 | IPv4 adresa, na které stránka naslouchá. |
| DIAG | LogPort | prázdný | Sériový port s logem základny. Prázdný = hledá se automaticky podle jména „MIDIControl log“. |
| DIAG | Dir | `$XDG_STATE_HOME/midicontrol`, jinak `~/.local/state/midicontrol`, jinak `/tmp/midicontrol-<uid>/diag` | Složka s diagnostikou (vytvoří se). |
| ORGAN | StopsEnabled | true | Hlavní vypínač nastavování rejstříků. |
| ORGAN | Channel | 12 | MIDI kanál 1 až 16 pro přepínání rejstříků. |
| ORGAN | ProgramOffset | -1 | Posun mezi číslem rejstříku a číslem programu, −127 až 127. |
| ORGAN | AllOffSysex | `F0 00 4A 4F 48 41 53 7F F7` | Bajty (hex) zprávy „vypnout všechny rejstříky“. |
| ORGAN | AllOffDelayMs | 100 | Čekání po ní, 0 až 60000 ms. |
| ORGAN | StopDelayMs | 20 | Odstup mezi rejstříky, 0 až 60000 ms. |
| ORGAN | DropSongProgramChanges | true | Zahodit změny programu uložené v souboru písně, jsou-li rejstříky nastavené. |
| ORGAN | Groups | čtyři výchozí skupiny | Skupiny rejstříků ve tvaru `Název:první-poslední, …`. Celkový počet vyplývá z posledního čísla. |
| ORGAN | Name<N>, ST<N>, Program<N> | výchozí pro 1 až 42 | Jméno rejstříku, jeho číslo z elektrického schématu (osmičkově) a číslo programu (0 až 127, přebíjí ST a posun). Rejstříky nad 42 dostanou jméno „Rejstřík N“ a potřebují `Program<N>`. |
| BINDINGS | (libovolné) | nepoužito | Načítá se, ale nepoužívá. |

Výchozí hodnoty varhan (rejstříky 1 až 42, skupiny „Spojky a pomocná zařízení“,
„Pedál“, „I. manuál – hlavní stroj“, „II. manuál – žaluziový stroj“) odpovídají
varhanům Johannus Opus 910.

!!! note
    TODO: Hodnotu `ProgramOffset` (−1) zatím nikdo neověřil na skutečných varhanách
    (poznámka v `config.conf`). Je ověřena? Otázka pro majitele.

Při chybě v konfiguraci se program ukončí s hláškou na začátku „config: …“,
například „config: PlayFolder not specified“, „config: play folder not found: <cesta>“,
„config: invalid value for <klíč>: '<hodnota>'“ nebo
„config: unable to find the config file: <cesta>“.

## Složka s písněmi a jména souborů { #pisne }

Všechny písně leží v jedné složce (`PlayFolder`), bez podsložek:

- `<kód>.mid`: píseň, například `401.mid`, `406A.mid`,
- `<kód>.conf`: nepovinný soubor rejstříků téže písně, například `406B.conf`.

**Kód písně** je 1 až 4 číslice s hodnotou 1 až 1999 a nepovinným jedním
písmenem A až D za nimi. Příklady: `401`, `7`, `1999`, `406B`. Velikost písmen se
nerozlišuje (`406b.mid` je totéž co `406B.mid`). Kódy `0`, `2000`, `12345`, `406E`
nebo `406AB` neplatí.

Do seznamu písní na ovladači a na webové stránce se dostanou jen soubory, jejichž
jméno (bez `.mid`) je platný kód. Soubor s jiným jménem (například
`rejstriky.mid`) se nenabízí a stránka ho uvede v upozornění na soubory písní.

Píseň je standardní MIDI soubor (formát 0 nebo 1) s displejovými SysEx zprávami
(„kotvami“), z nichž se bere číslo písně a sloky na displeji základny
a názvy částí písně. Přesný formát: `docs/SYSTEM.md` v repozitáři programu.
Písně bez kotev se zobrazí s kódem podle jména souboru.

Nový soubor program zaznamená do 60 s (složka se prochází jednou za minutu),
nebo při příštím startu. Chyby souborů se zapisují do logu jako
„song <soubor>: <problém>“ a zobrazují se na stránce Přehled
(viz [Upozornění](web.md#upozorneni)).

### Soubory rejstříků písní (songs/<kód>.conf) { #rejstriky-soubory }

Soubor `<PlayFolder>/<kód>.conf` leží vedle `<kód>.mid`. Obvykle ho zapisuje
webová stránka (viz [Nastavení rejstříků písně](web.md#rejstriky)), lze ho
upravit i ručně, ale jen když je přehrávač zastavený.

```
[STOPS]
Stops = 1 2 3 17 18 19
```

- Čísla rejstříků jsou 1 až 42 (podle konfigurace), oddělená mezerami nebo čárkami.
- `Stops =` bez čísel znamená „všechny rejstříky vypnout“.
- Žádný soubor znamená „nenastaveno“: přehrávač nic neodešle a varhany nechá, jak jsou.
- Komentáře začínají `#` nebo `;`. Sekce i klíč nerozlišují velikost písmen.
- Řádek, který obsahuje text, ale žádné platné číslo rejstříku, se bere jako
  „nenastaveno“, ne jako „vše vypnuto“.
- Při chybě se do logu zapíše „<soubor>:<řádek>: …“ a druhý řádek `Stops`
  nahradí první.

Při spuštění písně s souborem přehrávač odešle zprávu „vše vypnout“, počká
`AllOffDelayMs` a potom jednou změnou programu zapne každý vybraný rejstřík
(odstup `StopDelayMs`). Webová stránka soubor při každém uložení přepíše
(komentáře se neuchovají) a **Zrušit nastavení** soubor smaže.

!!! note
    Výchozí registrace pro píseň bez souboru (SYS-USE-20) program neumí: píseň bez
    souboru žádné rejstříky nenastaví. TODO: Potvrdit text „varhany zůstanou, jak jsou“.

## Nahrávání { #nahravani }

Nahrávání dnes jde spustit jen z konzole správce. Z ovladače ani ze základny
spustit nejde (požadavek se odmítne) a webová stránka k tomu nemá tlačítko.

1. V terminálu na počítači spusťte `midicontrol` (připojí se ke službě).
2. Zadejte `record`. Zapne se MIDI vstup „MIDIControl“; jsou-li varhany vypnuté,
   program je nejdřív zapne a počká `WarmupMs`.
3. Hrajte na varhany.
4. Nahrávání ukončete příkazem `stop`.

Soubor se uloží do `RecordFolder` pod jménem `RRRR-MM-DDTHH:MM:SS.mid`
(datum a čas začátku). Při shodě jmen se přidá `-2` až `-99`. Nahrávka není
automaticky písní: nepřehraje se, dokud ji nezkopírujete do `PlayFolder` pod
platným kódem (například `408.mid`).

Hlášky v logu: „Recording to <cesta>“; „Already recording; Stop first.“ (druhý
`record`); „MIDIControl MIDI input port not found; cannot record.“; „Cannot create a
recording file in <složka>“; „Failed to start arecordmidi (rc=N)“. Konzole při
neúspěchu napíše „Recording not started (see the log)“.

TODO: Jak se má z nahrávky stát hratelná píseň (kopírovat a přejmenovat do
`PlayFolder`, kdo to dělá, jak se vytvoří soubor rejstříků a kotvy)? Specifikace
(`SYS-REC-*`, D29) počítá se spuštěním ze základny a jménem `REC_RRRR-MM-DD_HHMM.mid`;
dnešní program to neumí. Který stav má příručka popisovat?

## Logy, konzole a diagnostika { #logy }

**Log služby.** `journalctl --user -u midicontrol -f`. Každý příkaz z konzole se
zapíše jako „console: <příkaz>“.

**Interaktivní konzole.** Spusťte `midicontrol` v terminálu. Program se připojí
ke běžící službě přes řídicí soket `$XDG_RUNTIME_DIR/midicontrol.sock` (přístup
jen pro vlastníka; záložní umístění `/tmp/midicontrol-<uid>/`). Zobrazí se
„MIDIControl - interactive control“ a výzva `> `. Příkaz `quit` konzoli odpojí, služba běží dál.
Jedna instance programu smí běžet jen jednou (zámek `midicontrol.lock`).

**Jeden příkaz.** `midicontrol --cmd "status"`. Návratový kód 0 je úspěch, 1 selhání.
Další parametry: `--config CESTA`, `--headless`, `--help`.

Příkazy:

| Příkaz | Co dělá |
|---|---|
| `play <kód>` | Spustí píseň. Selhání: „Cannot play <kód>: <důvod>“ (důvody „invalid song code“, „song not found“, „refused“). |
| `stop` | Zastaví píseň (nebo ukončí nahrávání). |
| `verse` | Přidá sloku: „Verse added: <název>“, nebo „Nothing is playing“. |
| `list` | Vypíše písně: „<N> songs: kód kód …“. |
| `record` | Spustí nahrávání (viz [Nahrávání](#nahravani)). |
| `on`, `off` | Ručně zapne a vypne varhany. |
| `status` | Stav spojení a přehrávání, stav základny. |
| `diag` | Poslední poruchy základny a ovladače a posledních 20 řádků logu. |
| `update <base\|remote> <soubor>` | Aktualizace firmwaru (ukazatel `[####    ] N%`, na konci „Firmware update complete.“ nebo „Firmware update FAILED.“). |
| `quit` | Odpojí konzoli. |

Když aktualizace základny selže v půli, základna zůstane v zavaděči USB
(ABCD:1235) a `status` napíše „Base is in its USB bootloader (ABCD:1235): a firmware
update did not finish; re-flash it with: update base <file>“. Spusťte uvedený příkaz.

**Diagnostika.** Program čte druhý sériový port základny „MIDIControl log“ a
každých 10 s se ptá ovladače na poruchy. Záznamy ukládá po zařízeních do složky
`Dir` ze sekce `[DIAG]` (soubory JSONL, tři soubory po 1 MiB, a `diag-ack.json` s
označením „viděno“). Zobrazují se na stránce Pokročilé, Diagnostics
(viz [Pokročilé](web.md#pokrocile)) a příkazem `diag`. Spojení se základnou se
kontroluje každé 2 s; po 6 s bez odpovědi se do logu zapíše „Base is not answering
keepalives (no reply for 6 s).“

**Varhany a vypínání.** Po písni program varhany vypne, pokud není `KeepPowerOn = true`.
Našel-li program při svém startu varhany zapnuté, nechá je zapnuté a vypne je po
30 minutách nečinnosti (hodnota je pevná v programu, není v konfiguraci).

## Zálohy { #zalohy }

Repozitář neobsahuje žádný postup ani skript zálohování. Veškerý stav programu leží
v těchto místech, a stačí tedy zálohovat je:

- konfigurační soubor `config.conf`,
- `PlayFolder` (soubory `.mid` a `.conf`; soubory `.conf` zapisuje webová stránka),
- `RecordFolder` (nahrávky),
- složku `Dir` z `[DIAG]` jen tehdy, chcete-li zachovat historii poruch.

TODO: Chce majitel doporučený postup nebo skript pro zálohování, a kam se má
zálohovat? Otázka pro majitele.
