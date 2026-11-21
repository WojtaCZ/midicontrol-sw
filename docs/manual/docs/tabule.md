# Tabule

Tabule je ukazatel v kostele, který návštěvníkům ukazuje, jaká píseň se hraje. Je připojena k základně a řídí ji základna i případné ruční ovladače.

!!! note "TODO"
    TODO: Jak tabule vypadá, kde visí a jak se připojuje? (Informace nejsou ve firmwaru; doplnit obrázek.)

## Co tabule ukazuje při písni { #tabule-pisen }

Když se hraje píseň, tabule ukazuje:

- číslo písně: čtyři místa, na každém číslice 0–9 nebo prázdno; nejvyšší číslo je 1999,
- písmeno A–D, nebo prázdno,
- číslo sloky: dvě místa,
- barevnou kontrolku: červenou, žlutou, zelenou, modrou, nebo vypnuto.

Číslo písně a písmeno odpovídají kódu zadanému na ovladači, například `406A`. Číslo sloky se mění podle hrané části písně. Před první slokou (předehra) je sloka prázdná.

TODO: Kdo vybírá barvu kontrolky a co která barva znamená? Firmware pole `led` jen přenáší; význam barev není nikde uveden.

TODO: Čtyři místa čísla písně: jak se zobrazuje kód s méně než čtyřmi číslicemi (zarovnání, nuly)?

Základna posílá údaje na tabuli při každé změně a znovu každých 0,5 s. Když tabule není připojena, základna nic neposílá.

## Ruční ovladače na stejné tabuli { #tabule-sdilena }

Na tabuli může psát více zařízení najednou: základna i ruční ovladače, které s ní sdílejí vedení. Proto číslo na tabuli nemusí pocházet z písně, kterou spustil počítač. Může je zadat jiný ovladač.

Základna číslo od jiného ovladače přijme a ukáže na stránce `Displej` ve svém nastavení (viz [Základna](zakladna.md)). Přehrávání kvůli němu nemění; píseň dál hraje počítač.

TODO: Zobrazuje ovladač nebo displej základny číslo z jiného ovladače na obrazovce přehrávače? Podle kódu zatím ne; je to záměr?

!!! note "Ověření"
    Skutečný provoz s tabulí v kostele nebyl na lavici ověřen, protože lavice tabuli nemá. Chování popsané zde vychází z firmwaru.

## Co se stane po zastavení písně { #tabule-stop }

Po zastavení písně nebo jejím konci počítač tabuli vymaže. Číslo, písmeno i sloka zmizí.

Vymaže se jen to, co tabuli nastavil počítač. Pokud mezitím jiný ovladač zapsal na tabuli vlastní číslo, může tabule po zastavení buď zůstat s tímto číslem, nebo být vymazána; základna to nerozlišuje.

TODO: Má tabule po zastavení zhasnout vždy, i když na ní mezitím psal jiný ovladač? Specifikace (SYS-BUS-07) říká, že se má vymazat jen číslo, které tabuli nastavil počítač; kód to zatím neověřuje.

Při přidání sloky (viz [Ovladač](ovladac.md#pridani-sloky)) zůstává číslo sloky na tabuli beze změny, dokud se opakovaná sloka hraje.
