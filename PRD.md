# BLE Scooter Detector - Product Requirements Document

## Yhteenveto

ESP32-pohjainen laite, joka havaitsee sähköpotkulautoja BLE-mainosten perusteella ja raportoi havainnot LoRaWAN-verkon kautta. Tarkoitettu pysäköintialueiden käyttöasteen seurantaan.

## Tausta ja motivaatio

Helsingissä sähköpotkulaudoille on määritelty keskustan alueella pysäköintialueet. Tavoitteena on seurata reaaliajassa kuinka paljon potkulautoja on pysäköintialueen läheisyydessä. Potkulaudat lähettävät BLE-mainoksia, joista ne voidaan tunnistaa device namen perusteella (esim. "Scooter1" Ryde-operaattorilla).

## Laitevaatimukset

### Laitteisto
- ESP32 (BLE + LoRa -tuella)
- LoRa-radio (SX1276/SX1278)
- 18650-akku + mahdollinen aurinkopaneeli
- GPS (valinnainen, myöhemmässä vaiheessa)

### Verkko
- Digita LoRaWAN (Netmore-taustajärjestelmä)
- OTAA-aktivointi

## Toiminnalliset vaatimukset

### F1: BLE-skannaus

| Ominaisuus | Vaatimus |
|------------|----------|
| Skannausjakso | Konfiguroitava (oletus 10s) |
| Tunnistusperuste | BLE advertisement "Complete Local Name" tai "Shortened Local Name" |
| Filtteri | Lista hyväksytyistä device name -prefikseistä (esim. "Scooter", "TIER", "Voi") |
| RSSI-raja | Konfiguroitava minimiarvo (oletus -90 dBm), heikommat signaalit hylätään |

### F2: Laitteiden seuranta

| Ominaisuus | Vaatimus |
|------------|----------|
| Tunniste | MAC-osoitteen 3 viimeistä tavua (24 bittiä) - riittää uniikkiuteen paikallisesti |
| Tila per laite | MAC (3 tavua) + ensimmäinen havainto (aikaleima) + viimeisin havainto (aikaleima) |
| Maksimi laitemäärä | Mahdollisimman suuri, tavoite ≥100 laitetta |
| Persistenssi | RTC-muisti (säilyy deep sleepin yli) |

### F3: Tilasiirtymät ja laskenta

Laite voi olla kolmessa tilassa suhteessa seurantajaksoon:

```
┌─────────────┐
│   UUSI      │  Havaittu ensimmäistä kertaa tällä raportointijaksolla
└─────────────┘
       │
       ▼ (seuraava skannaus löytää)
┌─────────────┐
│  PAIKALLA   │  Havaittu aiemmin, edelleen näkyvissä
└─────────────┘
       │
       ▼ (ei havaittu N skannaukseen)
┌─────────────┐
│  POISTUNUT  │  Ei enää näkyvissä, poistetaan listalta
└─────────────┘
```

**Poistumislogiikka:** Laite merkitään poistuneeksi kun sitä ei ole havaittu X peräkkäisessä skannauksessa (X konfiguroitava, oletus 3).

### F4: LoRaWAN-payload

#### Payload-rakenne (maksimi 51 tavua)

```
Tavu 0:     Versio + liput (1 tavu)
              - bitit 0-3: payload-versio (0x01)
              - bitti 4: 1 = laitelista tyhjennettiin (reboot/overflow)
              - bitit 5-7: varattu

Tavu 1:     Skannausten määrä tällä raportointijaksolla (1 tavu)

Tavu 2:     Uusien laitteiden määrä (1 tavu)

Tavu 3:     Paikalla olevien laitteiden määrä (1 tavu)

Tavu 4:     Poistuneiden laitteiden määrä (1 tavu)

Tavu 5:     Pisimmän pysäköinnin kesto (1 tavu)
              - Yksikkö: raportointijaksot (esim. 5 min)
              - Maksimi 255 jaksoa

Tavu 6-50:  Varattu laajennuksille (45 tavua)
              - Myöhemmin: yksittäisten laitteiden MAC + kesto
```

**Packet loss -käsittely:**
- Jokainen paketti sisältää absoluuttiset lukumäärät (ei delta-arvoja)
- Backend voi rekonstruoida tilanteen yhdestä paketista
- `skannausten_määrä` kertoo kuinka monta skannausta jakso sisälsi
- "Lista tyhjennettiin" -lippu ilmaisee epäjatkuvuuskohdan

### F5: Virransäästö

| Tila | Kesto | Virrankulutus |
|------|-------|---------------|
| BLE-skannaus | ~10s | ~100 mA |
| LoRa TX | ~1s | ~120 mA |
| Deep sleep | loppuaika | ~10 µA |

**Sykli:**
1. Herää deep sleepistä
2. Suorita BLE-skannaus
3. Päivitä laitelista (RTC-muistissa)
4. Jos raportointiväli täynnä → lähetä LoRaWAN
5. Siirry deep sleepiin

### F6: Konfiguraatio

Ensimmäisessä versiossa kovakoodatut arvot:

```cpp
// Ajoitus
#define SCAN_DURATION_SEC       10      // BLE-skannauksen kesto
#define SCAN_INTERVAL_SEC       60      // Skannausten väli (sis. sleep)
#define REPORT_INTERVAL_SEC     300     // LoRaWAN-lähetysväli

// Tunnistus
#define RSSI_THRESHOLD          -90     // Minimi RSSI (dBm)
#define MISSING_SCANS_LIMIT     3       // Montako skannausta ennen poistoa

// Device name -filtterit
const char* DEVICE_FILTERS[] = {
    "Scooter",
    "TIER",
    "Voi",
    "Lime",
    "Bird"
};
```

**Myöhemmin (V2+):** BLE-pohjainen konfigurointi tai WiFi-hotspot + web-UI.

## Ei-toiminnalliset vaatimukset

### NF1: Muistinkäyttö

- RTC slow memory: 8 KB käytettävissä ESP32:lla
- Laiterakenne: 3 (MAC) + 4 (aikaleimat) = 7 tavua/laite
- Maksimi laitemäärä: ~1000 laitetta (7 KB)
- Käytännössä rajoitetaan 200 laitteeseen (varmuusmarginaali)

### NF2: Luotettavuus

- Watchdog timer estää jumittumisen
- Graceful degradation: jos LoRaWAN-lähetys epäonnistuu, data säilytetään seuraavaan yritykseen
- Overflow-käsittely: jos laitelista täyttyy, vanhin poistetaan ja lippu asetetaan

### NF3: Kehitysympäristö

- PlatformIO + VSCode/Cursor
- Arduino-framework (tutumpi, riittävä tähän käyttöön)
- Kirjastot:
  - `NimBLE-Arduino` (kevyempi kuin ESP32 BLE Arduino)
  - `MCCI LoRaWAN LMIC library` tai `LMIC-node`

## Rajaukset (V1)

Seuraavat ominaisuudet eivät kuulu ensimmäiseen versioon:

- GPS-paikannus
- Manufacturer data -pohjainen tunnistus
- Ajonaikainen konfigurointi (BLE/WiFi)
- OTA-päivitykset
- Usean solmun koordinointi

## Avoimet kysymykset

1. **Testaus talvella:** Miten testataan kun potkulaudat eivät liiku? Ryde-varasto?
2. **MAC-osoitteiden satunnaisuus:** Käyttävätkö operaattorit random MAC:ia? Jos kyllä, tunnistus ei toimi pelkän MAC:in perusteella.
3. **LoRaWAN-porttinumero:** Mikä FPort käytetään?

## Seuraavat askeleet

1. [ ] Luo PlatformIO-projekti ESP32 + LoRa -laudalle
2. [ ] Implementoi BLE-skannaus ja device name -filtteröinti
3. [ ] Testaa Ryde-varaston lähellä
4. [ ] Lisää laitteiden seuranta RTC-muistiin
5. [ ] Implementoi LoRaWAN-lähetys
6. [ ] Integroi deep sleep -sykli
7. [ ] Kenttätestaus

