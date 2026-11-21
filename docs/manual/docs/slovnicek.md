# Slovníček

| Pojem | Význam |
|---|---|
| Ovladač | Ruční jednotka varhaníka s displejem a tlačítky. Zadává se na ní číslo písně. Viz [Ovladač](ovladac.md). |
| Základna | Jednotka u varhan. Spojuje ovladač, počítač, varhany a tabuli a spíná napájení varhan. Viz [Základna](zakladna.md). |
| Počítač (program `midicontrol`) | Program, který hledá a přehrává písně a posílá údaje na tabuli. Běží na pozadí. |
| Tabule | Ukazatel v kostele s číslem písně, písmenem a slokou. Viz [Tabule](tabule.md). |
| Sběrnice tabule | Společné vedení, po kterém základna posílá údaje na tabuli. Mohou na ni zapisovat i ruční ovladače. |
| Varhany | Johannus Opus 910 se 42 rejstříky. |
| Kód písně | Číslo o 1 až 4 číslicích (nejvýše 1999) s volitelným písmenem A až D, například `406B`. `12` a `012` jsou různé kódy. |
| Soubor písně | Soubor `<kód>.mid` ve složce písní na počítači. |
| Sloka | Část písně. Ovladač ji ukazuje jako `Sloka: N`. |
| Předehra | Úvodní část písně; ukazuje se jako `Predehra`. |
| Přidání sloky | Opakování právě hrané sloky po jejím konci. Viz [postup](postupy.md#postup-pridat-sloku). |
| Rejstřík | Hlasový registr varhan. Číslují se 1 až 42. |
| Nastavení rejstříků písně | Seznam rejstříků, které se zapnou při startu písně. Nastavuje se na webové stránce. Viz [postup](postupy.md#postup-rejstriky). |
| Napájení varhan | Spínání napájení varhan základnou (relé SSR). Po každém restartu základny je vypnuto. |
| Zahřátí | Krátká prodleva (asi 3 s) po zapnutí varhan před začátkem hry. |
| Párování | Spojení ovladače a základny. Základna si pamatuje jediný ovladač. Viz [postup](postupy.md#postup-prvni-instalace). |
| Bluetooth | Bezdrátové spojení ovladače se základnou. |
| USB | Kabel mezi počítačem a základnou. USB-C ovladače slouží jen k nabíjení. |
| MIDI | Standard pro přenos hry mezi počítačem, základnou a varhanami. |
| DIN | Pětikolíkový konektor MIDI na základně. |
| Enkodér | Otočný ovladač s tlačítkem na základně. |
| OLED | Displej ovladače a základny. Po 30 s se uspí. |
| LED | Svítivé indikátory na základně. Viz [LED](zakladna.md#led). |
| Firmware | Program uvnitř ovladače nebo základny. Aktualizuje se z webové stránky. |
| Zaváděcí režim (bootloader) | Stav základny při aktualizaci firmwaru, ve kterém čeká na data z počítače. |
| Konzole | Příkazový řádek programu v počítači; používá ji správce. |
| Správce | Osoba, která instaluje a udržuje systém. Viz [Pro správce](sprava.md). |
| Webová stránka | Stránka programu na adrese `http://<počítač>:50118/`. Viz [Webová stránka](web.md). |
| Pokročilé | Anglická část webové stránky pro správce. |
| KeepPowerOn | Volba programu: varhany se po písni nevypnou. Nastavuje správce. |
