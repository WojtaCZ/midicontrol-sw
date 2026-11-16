# Řešení problémů

Kapitola má tři části: příznaky bez hlášky, hlášky ovladače, hlášky základny a hlášky webové stránky. Texty displejů jsou psány bez diakritiky, jak je přístroj zobrazuje. Stále platí: když nevíte, dejte vědět správci a uveďte přesný text hlášky.

## Příznaky bez hlášky { #priznaky }

| Příznak | Možná příčina | Co dělat |
|---|---|---|
| Píseň se spustila (`Prehravam`), ale nehraje zvuk | Napájení varhan je vypnuté nebo se varhany ještě nezahřály; MIDI spojení základny s varhanami nefunguje. | Na základně v `Nastaveni` zkontrolujte políčko `Napajeni varhan` a svit LED `Proud`. Počkejte několik sekund po `Spoustim`. Když zvuk nepřijde, dejte vědět správci. TODO: Spojení MIDI základna - varhany (USB nebo DIN) a jeho kontrola; kód nezaručuje funkci vstupu a výstupu DIN, neslibovat. |
| Tabule je prázdná | Tabule není připojena nebo nehraje žádná píseň; po zastavení se tabule vymaže. | Spusťte píseň. Zkontrolujte na základně stránku `Displej` (`Pripojen: Ano`). Viz [Tabule](tabule.md). Dejte vědět správci, když zůstane prázdná při hře. |
| Ovladač se nepřipojuje (žádná ikona spojení nebo slabá ikona) | Základna je vypnutá, není spárovaná nebo je mimo dosah; ovladač čeká na základnu. | Zkontrolujte, že je základna zapnutá. V `Nastaveni`, `Bluetooth` ověřte `Sparovano` a `Zakladna`. Vypněte a zapněte ovladač. Pak případně [spárujte znovu](postupy.md#postup-prvni-instalace). |
| Na ovladači `Odpojeno` | Ovladač nemá základnu nebo základna nemá počítač. | Viz hlášky `Cekam na zakladnu` a `Cekam na PC` níže. |
| Displej ovladače je zhasnutý | Displej usíná po 30 s; ovladač se po 20 min bez stisku vypne; vybitá baterie. | Stiskněte libovolné tlačítko (první stisk jen probudí). Když nereaguje, zapněte ho tlačítkem ON/OFF nebo ho nabijte. |
| Ovladač se vypnul sám | 20 min bez stisku nebo vybitá baterie. | Zapněte ON/OFF. Zkontrolujte baterii v `Nastaveni`, `Baterie`. |
| Základna nereaguje na enkodér | Displej usnul. | První otočení nebo stisk displej jen probudí; opakujte. |
| Po zadání kódu se nic nestane | Ovladač je v jiném stavu než v přehrávači nebo nemá spojení. | Otevřete `Prehravac`. Zkontrolujte ikonu spojení. |
| Nelze pozastavit a pokračovat | Pozastavení neexistuje. | Viz [postup-pauza](postupy.md#postup-pauza). |
| Nahrávání z ovladače nebo základny nic nedělá | Zatím není vyrobeno; položka `Nahraj` nic nedělá. | Viz [postup-nahravani](postupy.md#postup-nahravani). |

## Hlášky ovladače { #hlasky-ovladace }

Hlášky v přehrávači a v seznamu písní zmizí po 2,5 s.

### Spuštění, přehrávač a seznam písní

| Text | Kde | Význam | Co dělat |
|---|---|---|---|
| `Zastaveno` | přehrávač | nic nehraje | normální stav |
| `Spoustim` | přehrávač | příkaz odeslán, čeká se na počítač | počkejte |
| `Prehravam` | přehrávač | píseň hraje | normální stav |
| `Zastavuji` | přehrávač | zastavení odesláno | počkejte na `Zastaveno` |
| `Neznama %s` (například `Neznama 999`) | přehrávač | počítač píseň s tímto kódem nenašel | zkontrolujte kód, otevřete seznam písní; [postup](postupy.md#postup-pisen-nenalezena) |
| `Nehraje se` | přehrávač | zastavení nebo přidání sloky, ale nic nehraje | nic; přehrávač se vrátí do `Zastaveno` |
| `PC nedostupne` | přehrávač, seznam písní | základna nemá spojení s počítačem | zkontrolujte počítač a kabel USB; viz [Ztráta spojení](ovladac.md#ztrata-spojeni) |
| `PC zaneprazdneno` | přehrávač | počítač nemůže příkaz teď přijmout | zkuste to za chvíli |
| `Odmitnuto` | přehrávač, seznam písní | počítač příkaz odmítl z jiného důvodu | zkuste to znovu; TODO: jaké důvody |
| `Bez odpovedi` | přehrávač, seznam písní | nikdo neodpověděl | zkuste to znovu, zkontrolujte spojení |
| `Nepripojeno` | přehrávač, seznam písní | ovladač nemá spojení se základnou | zkontrolujte základnu a dosah |
| `Zkuste znovu` | přehrávač | příkaz se nepodařilo odeslat | stiskněte OK znovu |
| `Nejvyse 1999` | přehrávač | zadané čtyřmístné číslo je větší než 1999 | zadejte číslo nejvýše 1999 |
| `Chybny kod` | přehrávač | neplatný kód | opravte kód |
| `Sloka: N`, `Sloka: N (xM)` | přehrávač | název části písně; M je počet přidaných opakování | informace; viz [přidání sloky](postupy.md#postup-pridat-sloku) |
| `Pisne` | seznam písní | titulek seznamu | informace |
| `Nacitani...` | seznam písní | seznam se načítá | počkejte nejvýše 3 s |
| `Zadne pisne` | seznam písní | ve složce nejsou písně s platným kódem | dejte vědět správci |
| `Zaneprazdneno` | seznam písní | příkaz nešlo odeslat | zkuste to znovu |
| `Cekam na PC` | seznam písní, `Odpojeno` | počítač není dostupný | zkontrolujte počítač |
| `Zpet` | nastavení, seznam písní | návrat | OK |

### Menu, nastavení a informační stránky

| Text | Kde | Význam | Co dělat |
|---|---|---|---|
| `MIDIControl` a `CONTROLLER` | úvodní obrazovka | název přístroje | počkejte 3 s nebo OK |
| `Hlavni menu` | hlavní menu | titulek | informace |
| `Prehravac` | hlavní menu | položka: přehrávač | OK |
| `Nastaveni` | menu | položka: nastavení | OK |
| `Baterie` | nastavení, stránka | informace o baterii | OK |
| `Bluetooth` | nastavení, stránka | informace o Bluetooth | OK |
| `Verze FW` | nastavení, stránka | verze firmwaru | OK |
| `Sparovat zakl.` | nastavení | spustí párování se základnou | viz [párování](postupy.md#postup-prvni-instalace) |
| `Napeti: %u.%02u V`, `Napeti: --` | `Baterie` | napětí baterie; `--` neměřeno | informace |
| `Uroven: %u %%` | `Baterie` | zbývající kapacita | při nízké nabijte |
| `Stav: Nabiji` | `Baterie` | nabíjí se | počkejte |
| `Stav: Nabito` | `Baterie` | nabito | odpojte kabel |
| `Stav: Nenabiji` | `Baterie` | USB je připojeno, ale nenabíjí | zkontrolujte nabíječku a kabel |
| `Stav: Vybiji` | `Baterie` | běží z baterie | informace |
| `USB: Pripojeno`, `USB: Odpojeno` | `Baterie` | přítomnost napájení USB | informace |
| `CC: 3.0 A`, `CC: 1.5 A`, `CC: USB (0.5 A)`, `CC: --` | `Baterie` | proud, který nabíječka nabízí | informace |
| `Modul: OK` | `Bluetooth` | Bluetooth modul je v pořádku | informace |
| `Modul: OK, bez RST` | `Bluetooth` | modul funguje, ale reset se nepotvrdil | dejte vědět správci, pokud spojení vázne |
| `Modul: neodpovida` | `Bluetooth` | modul neodpovídá | vypněte a zapněte ovladač; dejte vědět správci |
| `Modul: mlci` | `Bluetooth` | modul mlčí | vypněte a zapněte ovladač; dejte vědět správci |
| `Modul: ?` | `Bluetooth` | stav modulu není znám | dejte vědět správci |
| `Spojeni: Ano` / `Ne` | `Bluetooth` | je / není spojení | při `Ne` viz příznaky výše |
| `Sparovano: Ano` / `Ne` | `Bluetooth` | je / není spárováno | při `Ne` spárujte |
| `Zakladna: Ano` / `Ne` | `Bluetooth` | základna se ozývá / ne | při `Ne` zkontrolujte základnu |
| `MAC: %s`, `MAC: --` | `Bluetooth` | adresa základny | informace |
| `Revize:`, `Vetev:`, `Datum:`, `Cas:` | `Verze FW` | revize gitu, větev, datum a čas sestavení | pro správce |

### Párování, odpojení a vypnutí

| Text | Kde | Význam | Co dělat |
|---|---|---|---|
| `Parovani` | okno párování | titulek | viz [párování](postupy.md#postup-prvni-instalace) |
| `Cekam na zakladnu` | okno párování, `Odpojeno` | čeká na základnu | v okně párování do 60 s spusťte párování na základně; v `Odpojeno` zkontrolujte základnu |
| `Zbyva %lu s` | okno párování | zbývající čas | spusťte párování na základně |
| `Sparovano` | okno párování | párování se podařilo | OK |
| `Cas vyprsel`, `Nesparovano` | okno párování | okno 60 s vypršelo | zopakujte |
| `Sparovat?`, `Sparovat? Kod %06lu` | dotaz | při běžném párování se nezobrazí; jen informativní, odpověď nic nemění | nic, odpověď nemá vliv |
| `Ano`, `Ne` | tlačítka dotazu | odpověď na dotaz | podle TODO výše |
| `Odpojeno` | překryvná obrazovka | ovladač nemá základnu nebo počítač | viz [Ztráta spojení](ovladac.md#ztrata-spojeni) |
| `Stisk: Nastaveni` | `Odpojeno` | tip: OK otevře nastavení | informace |
| `Vypnuti` | vypínání | titulek | informace |
| `Drzte 5 s` | vypínání | držte ON/OFF 5 s | držte, nebo pusťte a vypnutí se zruší |
| `Slaba baterie` | před vypnutím | baterie je kriticky vybitá, ovladač se vypíná | nabijte; viz [Nabíjení](postupy.md#postup-nabijeni) |
| `Necinnost: vypnuti` | před vypnutím | vypnutí po 20 min bez stisku | zapněte ON/OFF |

### Aktualizace firmwaru

| Text | Kde | Význam | Co dělat |
|---|---|---|---|
| `Aktualizace` | titulek | probíhá aktualizace ovladače | nevypínejte |
| `Prijem dat` | aktualizace | přijímají se data | počkejte |
| `Instalace` | aktualizace | instaluje se | počkejte |
| `Chyba aktualizace` | aktualizace | aktualizace selhala | zopakujte aktualizaci; [postup](postupy.md#postup-aktualizace-firmwaru) |

### Hlášky bez textu na obrazovce

Ovladač nezobrazí nic při slabé baterii (varování před 3 450 mV zatím nic neukáže, vypnutí přijde až při kritickém napětí). Také se nic nezobrazí při tichém vypnutí podržením ON/OFF.

TODO: Zobrazí ovladač varování při slabé baterii před vypnutím? Webová stránka varuje od 15 %, ovladač ne. Majitel.

## Hlášky základny { #hlasky-zakladny }

### Menu, nastavení a informační stránky

| Text | Kde | Význam | Co dělat |
|---|---|---|---|
| `MIDIControl` a `BASE` | úvodní obrazovka | název přístroje | počkejte 3 s |
| revize a větev gitu | úvodní obrazovka | verze firmwaru | pro správce |
| `Hlavni menu` | hlavní menu | titulek | informace |
| `Sparuj ovladac` | hlavní menu, `Odpojeno` | základna nemá uložený ovladač | [spárujte](postupy.md#postup-prvni-instalace) |
| `Prehraj` | hlavní menu | otevře seznam písní | enkodér |
| `Nahraj` | hlavní menu | zatím nic nedělá | viz [Nahrávání](zakladna.md#nahravani) |
| `Nastaveni` | hlavní menu, titulek | nastavení | enkodér |
| `LED` | nastavení | hlavní vypínač LED | podle potřeby |
| `Ident. MIDI` | nastavení | barvy značky MIDI A/B | podle potřeby |
| `Napajeni varhan` | nastavení | spínání napájení varhan | zaškrtnuto = zapnuto |
| `Verze FW` | nastavení, stránka | verze firmwaru | informace |
| `Displej` | nastavení, stránka | stav tabule | informace |
| `Bluetooth` | nastavení, stránka | stav Bluetooth | informace |
| `Sparovat ovladac` | nastavení | spustí párování | [postup](postupy.md#postup-prvni-instalace) |
| `Zpet` | nastavení | návrat | enkodér |
| `Revize:`, `Vetev:`, `Datum:`, `Cas:` | `Verze FW` | revize, větev, datum a čas sestavení | pro správce |
| `Pripojen: Ano` / `Ne` | `Displej` | tabule je / není připojena | při `Ne` zkontrolujte kabel tabule |
| `Pisen: [..]` | `Displej` | číslice písně na tabuli | informace |
| `Sloka: [..]` | `Displej` | číslice sloky na tabuli | informace |
| `Pismeno: x` | `Displej` | písmeno písně | informace |
| `LED: cervena`, `zelena`, `modra`, `zluta`, `vyp` | `Displej` | barva LED na tabuli | informace; TODO: význam barev |
| `Modul: start...` | `Bluetooth` | modul se spouští | počkejte |
| `Modul: OK` | `Bluetooth` | modul je v pořádku | informace |
| `Modul: OK, bez RST` | `Bluetooth` | modul je nastaven, reset nepotvrzen | dejte vědět správci, pokud spojení vázne |
| `Modul: neodpovida` | `Bluetooth` | modul neodpovídá na příkazy | vypněte a zapněte základnu; dejte vědět správci |
| `Modul: mlci` | `Bluetooth` | modul mlčí | vypněte a zapněte základnu; dejte vědět správci |
| `Spojeni: Ano` / `Ne` | `Bluetooth` | Bluetooth spojení je / není | při `Ne` zkontrolujte ovladač |
| `Sparovano: Ano` / `Ne` | `Bluetooth` | protistrana přijata / ne | při `Ne` spárujte |
| `Ovladac: Ano` / `Ne` | `Bluetooth` | ovladač odpovídá / ne | při `Ne` zapněte ovladač |
| `MAC: <hex>`, `MAC: --` | `Bluetooth` | uložená adresa ovladače; `--` žádná | informace |

### Párování

| Text | Kde | Význam | Co dělat |
|---|---|---|---|
| `Parovani` | párování | titulek | informace |
| `Hledam ovladac...` | párování | hledá se ovladač, nejvýše 15 s | ovladač musí mít otevřené okno párování |
| `BLE nefunguje` | párování | Bluetooth modul neběží | vypněte a zapněte základnu; dejte vědět správci |
| `Sparovano` | výsledek párování | podařilo se | stiskněte enkodér |
| `Ovladac nenalezen` | výsledek párování | za 15 s se ovladač nenašel | otevřete okno párování na ovladači a opakujte |
| `Parovani selhalo` | výsledek párování | ovladač se našel, ale párování selhalo | opakujte od začátku |

### Seznam písní, přehrávání a odpojení

| Text | Kde | Význam | Co dělat |
|---|---|---|---|
| `Pisne` | seznam písní | titulek | informace |
| `Nacitani...` | seznam písní | čeká se na počítač | počkejte nejvýše 3 s |
| `Cekam na PC` | seznam písní, `Odpojeno`, aktualizace | počítač není dostupný | zkontrolujte počítač a kabel USB |
| `Zadne pisne` | seznam písní | ve složce nejsou písně | dejte vědět správci |
| `Prehravam` | přehrávání | píseň hraje | enkodér zastaví |
| `Zastavuji` | přehrávání | zastavení odesláno; po 5 s se vzdá | počkejte |
| `Zastaveno` | přehrávání | zastaveno (1,5 s) | informace |
| `Sloka: N`, `Sloka: N (xK)` | přehrávání | sloka; K je počet čekajících opakování | informace |
| `Predehra`, `Predehra (xK)` | přehrávání | předehra | informace |
| `Odpojeno` | překryvná obrazovka | počítač není připojen | zkontrolujte počítač; zmizí sama |
| `Stisk: Nastaveni` | `Odpojeno` | tip: enkodér otevře nastavení | informace |
| kódy písní, například `406A` | seznam, přehrávání | názvy z počítače | informace |
| název části písně z počítače | přehrávání | text posílá počítač | informace |
| `PC neodpovida` | důvod chyby | PC neodpovědělo po opakování | zkontrolujte počítač |
| `Neodeslano` | důvod chyby | fronta odesílání je plná | zkuste to znovu |
| `Pisen nenalezena` | důvod chyby | píseň neexistuje | zkontrolujte kód |
| `PC zaneprazdneno` | důvod chyby | počítač je zaneprázdněn | zkuste to za chvíli |
| `PC nedostupne` | důvod chyby | cílové zařízení není připojeno | zkontrolujte spojení |
| `Nehraje se` | důvod chyby | zastavení, ale nic nehraje | nic |
| `Odmitnuto` | důvod chyby | jiné odmítnutí | zkuste to znovu |

### Aktualizace firmwaru

| Text | Kde | Význam | Co dělat |
|---|---|---|---|
| `Aktualizace` | titulek | probíhá aktualizace | nevypínejte |
| `Prenos do ovladace` | aktualizace ovladače | základna posílá firmware ovladači | počkejte |
| `Instalace ovladace` | aktualizace ovladače | ovladač se instaluje | počkejte |
| `Chyba aktualizace` | aktualizace ovladače | selhala | zopakujte aktualizaci |
| `Cekam na PC` | zaváděcí režim | čeká na přenos z počítače | zahajte aktualizaci |
| `Nevypinejte` | zaváděcí režim | přijímání dat | nevypínejte |
| `Chyba, opakujte` | zaváděcí režim | přenos selhal nebo PC 20 s mlčí | zopakujte aktualizaci |

Základna svítí červeně třikrát při restartu po chybě; viz [LED](zakladna.md#led) a [Po výpadku proudu](zakladna.md#po-vypadku).

## Hlášky webové stránky { #hlasky-webu }

### Upozornění na stránce Přehled

| Text | Barva | Význam | Co dělat |
|---|---|---|---|
| `Přehrávač neodpovídá, údaje na stránce nemusí být aktuální.` | červená | stránka nedostává odpověď od přehrávače | zkontrolujte, že počítač a program běží; dejte vědět správci |
| `Probíhá aktualizace firmwaru základny.` | žlutá | základna se aktualizuje | počkejte |
| `Základna není připojena. Zkontrolujte USB kabel mezi počítačem a základnou.` | žlutá | počítač nevidí základnu | zkontrolujte kabel USB a napájení základny |
| `Základna neodpovídá.` | červená | základna je připojena, ale neodpovídá | počkejte; vypněte a zapněte základnu; dejte vědět správci |
| `Základna se po poruše firmwaru sama restartovala. Pokud vše hraje, stačí to říct správci.` | žlutá | základna hlásí restart po chybě firmwaru | dejte vědět správci |
| `Ovladač se po poruše firmwaru sám restartoval. Pokud vše hraje, stačí to říct správci.` | žlutá | ovladač hlásí restart po chybě firmwaru | dejte vědět správci |
| `Baterie ovladače je vybitá. Nabijte ho.` | červená | baterie je kriticky vybitá | nabijte; [postup](postupy.md#postup-nabijeni) |
| `Baterie ovladače je slabá. Nabijte ho.` | žlutá | do 15 % a nenabíjí se | nabijte |
| `Soubor písně <kód> má chybu a nemusí hrát správně.` nebo `Soubory písní <kódy> mají chybu a nemusí hrát správně.` plus `Dejte vědět správci.` | žlutá | soubor písně má chybu | dejte vědět správci |

U většiny upozornění je odkaz `Podrobnosti` na stránku `Pokročilé`.

### Panel Systém a přehrávání

| Text | Význam | Co dělat |
|---|---|---|
| `Načítám stav systému…` | čeká se na první odpověď | počkejte |
| `V pořádku` (zelená) | součást je v pořádku | nic |
| `Vyžaduje pozornost` (žlutá) | součást vyžaduje pozornost | přečtěte upozornění |
| `Chyba` (červená) | součást je v chybě | přečtěte upozornění |
| `Nedostupné` (šedá) | stav není znám | zkontrolujte spojení |
| `Počítač`, `Základna`, `Ovladač` | uzly řetězu | klepnutím otevřete podrobnosti |
| `Přehrávač běží` | program v počítači běží | nic |
| `spuštěn N s · N písní` | doba běhu a počet písní | informace |
| `Připojena` | základna je připojena | nic |
| `Varhany: neznámo`, `Varhany zapnuty`, `Varhany vypnuty` | stav napájení varhan | informace |
| `Stav neznámý`, `Připojen`, `Nepřipojen` | stav ovladače | při `Nepřipojen` zkontrolujte ovladač a základnu |
| `porucha firmwaru`, `vybitá baterie`, `slabá baterie` | doplněk stavu ovladače | viz upozornění výše |
| `Baterie N %`, `, nabíjí se`, `, vybitá` | baterie ovladače | nabijte podle potřeby |
| `Vše v pořádku`, `N upozornění` | souhrn | přečtěte upozornění |
| `Právě hraje`, `Připravuji varhany` | hraje; varhany se zahřívají | počkejte |
| `Píseň <kód>`, `Predehra`, `Sloka N` | hraná píseň a sekce | informace |
| `Nic nehraje` | nehraje nic | informace |
| `naposledy píseň <kód> (dohrála)` | poslední píseň skončila normálně | informace |
| `naposledy píseň <kód> (zastavena)` | zastavena | informace |
| `naposledy píseň <kód> (zastavena, základna se odpojila)` | zastavena kvůli odpojení základny | zkontrolujte základnu |
| `naposledy píseň <kód> (nepodařilo se ji přehrát)` | přehrání selhalo | zkuste to znovu; dejte vědět správci |
| `naposledy píseň <kód> (skončila)` | skončila | informace |

### Rejstříky písní

| Text | Význam | Co dělat |
|---|---|---|
| `Hledat píseň…` | pole hledání | napište číslo písně |
| `Vše`, `Nastavené`, `Nenastavené` | filtry seznamu | podle potřeby |
| `Řadit: číslo vzestupně`, `Řadit: číslo sestupně`, `Řadit: nejvíc rejstříků`, `Řadit: nenastavené a nejméně rejstříků` | řazení | podle potřeby |
| `nenastaveno` | píseň nemá soubor rejstříků | informace |
| `vše vypnuto` | prázdný výběr | informace |
| `N rejstřík`, `N rejstříky`, `N rejstříků` | počet nastavených rejstříků | informace |
| `Žádná píseň neodpovídá výběru.` | filtr nic nenašel | změňte filtr |
| `Ve složce nejsou žádné písně.` | prázdná složka | dejte vědět správci |
| `Zobrazeno X z Y`, `N písní`, `· nastaveno N` | počty | informace |
| `Píseň <kód>` | nadpis editoru | informace |
| `Ve složce nejsou žádné písně`, `Rejstříky nenačteny`, `Načítám rejstříky…` | stav editoru | počkejte nebo `Zkusit znovu` |
| `Spojky a pomocná zařízení`, `Pedál`, `I. manuál – hlavní stroj`, `II. manuál – žaluziový stroj` | skupiny rejstříků | informace |
| `N z M zapnuto` | počítadlo | informace |
| `neuložené změny` | změny nejsou uloženy | klepněte na `Uložit` |
| `nenastaveno: varhany zůstanou, jak jsou` | bez souboru | informace |
| `uloženo`, `uloženo: vše vypnuto` | uloženo | informace |
| `Uložit`, `Uloženo` | tlačítko a potvrzení | klepněte |
| `Vše vypnout` | uloží prázdný výběr | podle potřeby |
| `Zrušit nastavení` | smaže soubor rejstříků | podle potřeby |
| `Ukládám…` | ukládá se | počkejte |
| `Neuloženo: <důvod>` | uložení selhalo | viz důvody níže |
| `Rejstříky se nepodařilo načíst: <důvod>`, `Zkusit znovu` | načtení selhalo | klepněte na `Zkusit znovu` |
| `Zahodit neuložené změny písně <kód>?` | dotaz při přepnutí písně | potvrďte nebo zrušte |
| `Zrušit nastavení rejstříků písně <kód>?` | dotaz před smazáním | potvrďte nebo zrušte |
| varování prohlížeče při zavření s neuloženými změnami | zavíráte stránku s neuloženými změnami | uložte nebo potvrďte zavření |

Důvody po `Neuloženo:` a při načítání:

| Text | Význam |
|---|---|
| `chybný požadavek` | stránka poslala špatná data (HTTP 400) |
| `nenalezeno` | píseň neexistuje (404) |
| `nepodporovaný formát` | (415) |
| `chyba přehrávače při ukládání` | soubor nešlo uložit (500); dejte vědět správci |
| `přehrávač je zaneprázdněn` | (409), zkuste to znovu |
| `příliš velká data` | (413) |
| `chyba N` | jiná chyba |
| `přehrávač neodpovídá` | časový limit 5 s |
| `spojení s přehrávačem selhalo` | chyba sítě; zkontrolujte, že program běží |

### Aktualizace firmwaru na stránce Pokročilé (anglicky, pro správce)

Stránka `Pokročilé` je anglicky. Hlášky stránky:

| Text | Význam | Co dělat |
|---|---|---|
| `Base firmware`, `Controller firmware` | karty základny a ovladače | informace |
| `Running now`, `Built`, `not reported`, `uncommitted changes` | co právě běží | informace |
| `Drop <name> here or choose it` | přetáhněte soubor | použijte soubor `-update.bin` |
| `File`, `Built for`, `Version / size`, `Check: trailer, board ID and CRC OK` | kontrola souboru | pokračujte |
| `The file is empty.` | prázdný soubor | vyberte správný soubor |
| `This is an ELF file. Use the <name>-update.bin from the firmware's build folder.` | soubor `.elf` | použijte `-update.bin` |
| `This looks like an Intel hex file. ...` | soubor `.hex` | použijte `-update.bin` |
| `No update trailer: this looks like a plain .bin. ...` | `.bin` bez trailer | použijte `-update.bin` |
| `The update trailer is corrupt or of an unknown format.` | poškozený trailer | použijte jiný soubor |
| `This is the base firmware, not the controller firmware. Drop it on the Base card.` (a opačně) | soubor patří jinému přístroji | použijte správnou kartu |
| `Built for an unknown unit (board ID N).` | neznámý přístroj | použijte jiný soubor |
| `The image size in the trailer does not match the file.` | velikost nesedí | soubor je poškozen |
| `The image CRC does not match: the file is damaged.` | poškozený soubor | stáhněte znovu |
| `The file is too large for an update file (N bytes).` | nad 512 KiB | použijte správný soubor |
| `A firmware update is running.` | již běží aktualizace | počkejte |
| `A song is playing; stop it first.` | hraje píseň | zastavte ji |
| `The base is not connected.` | základna není připojena | zkontrolujte kabel USB |
| `The controller is not connected to the base.` | ovladač není spojen se základnou | zapněte ovladač |
| `Flash base`, `Flash controller` | tlačítka spuštění | po kontrole klepněte |
| `Flash <name> firmware <version> from <file>?` | potvrzení | potvrďte; základna se restartuje a přehrávání se na asi minutu zastaví |
| `Flashing N %`, `Sending the image: X of Y bytes`, `Image sent (N bytes)` | průběh | počkejte |
| `Last update finished <ago>` | aktualizace skončila | nic |
| `Last update FAILED <ago>` | aktualizace selhala | zopakujte |
| `Not flashed: <text>`, `Refused (N).` | server aktualizaci odmítl | přečtěte důvod níže |
| `The upload did not reach the player.` | přenos nedošel k přehrávači | zkontrolujte, že program běží |
| `Uploading…`, `Reading <file>…` | probíhá | počkejte |
| `Base is in its USB bootloader (ABCD:1235): a firmware update did not finish; re-flash it with: update base <file>` | základna zůstala v zaváděcím režimu | zopakujte aktualizaci |

Odpovědi serveru při aktualizaci (anglicky):

| Odpověď | Význam |
|---|---|
| 202 | přijato, aktualizace běží na pozadí |
| 422 `the file is empty`, `this is an ELF file; ...`, `this looks like an Intel hex file; ...`, `this is built for an unknown unit (board ID N); nothing was sent`, `this is the base/controller firmware, not the controller/base firmware; nothing was sent`, `no image trailer - ...`, `image trailer is corrupt or of an unknown format`, `image is built for the other unit (board ID mismatch)`, `image size in the trailer does not match the file`, `image CRC does not match the trailer` | soubor je nesprávný; viz hlášky výše |
| 409 `a firmware update is already running`, `a song is playing; stop it first`, `the base is not connected` | aktualizaci nelze teď spustit |
| 404, 415, 413, 411, 408 | jiný cíl, jiný typ obsahu, příliš velký soubor, chunked tělo, časový limit 60 s |

Ostatní hlášky stránky `Pokročilé` (`Loading…`, `no entries`, `no fault recorded`, `Mark seen`, `unseen`, `none recorded`) jsou popsány v kapitole [Pokročilé](web.md#pokrocile).

!!! note
    Hlášky do příručky doplnil autor z kódu. Další otázky: TODO: Význam barev LED tabule (`LED: cervena` a další). TODO: Jaké důvody se skrývají za `Odmitnuto`.
