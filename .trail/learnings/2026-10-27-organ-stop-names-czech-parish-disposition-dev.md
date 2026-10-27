---
title: "Organ stop names: Czech parish-disposition terminology mapped from the Opus 910's Dutch console labels"
author: Dev
date: 2026-10-27
branch: device-info
cost: { minutes: 7, tokens: 94525 }
id: l-655c6156
scope: repo
status: active
src: agent
touches:
  - src/organ.cpp
  - src/web_page.cpp
---

## Decided
User (2026-10-27): stop names and group headers on the page are Czech, in organ slang, matched to this organ. Defaults in src/organ.cpp kDefaultStops follow Czech parish dispositions (e.g. farnost-sporilov.cz/varhany-parametry): noun first ("Flétna trubicová 8'", "Flétna dutá 8'"), mixtures by ranks ("Mixtura 3řadá", "Kornet 4řadý"), pedal "Principálbas 8'", couplers "Spojka II/I, I/P, II/P", "Tremolo I/II"; divisions "Pedál", "I. manuál – hlavní stroj", "II. manuál – žaluziový stroj". Each line carries the Dutch console label (Hoofdwerk/Zwelwerk/Pedaal, Roerfluit, Holpijp, Dulciaan, Bazuin...) the mapping came from.
## Tried and ruled out
Literal translations of the English Johannus names ("Zobcová flétna", "Krumhorna", "Hlavní stroj (I)"): not how Czech organists write a disposition, and English Cromorne/Contra Trumpet hid the console's Dulciaan/Bazuin. Keeping German names (Rohrflöte, Gedackt) as in big concert organs: parish practice and this user want Czech.
## Watch out
Names are shown only on the page; NameN / Groups in [ORGAN] override them per organ. Group labels may contain an en dash: toGroups splits the range on the first '-' after the colon, so a label dash is safe but a label colon is not.
