# Quality Gates modules - Waveshare ESP32-S3

Cette page suit l'état qualité des modules du firmware `Waveshare-ESP32-S3`.
Elle remplace l'ancienne matrice historique `FlowIO` / `Supervisor` par une
vue centrée sur le profil réellement compilé et instancié par
`src/Profiles/Waveshare`.

Dernière vérification: 2026-08-09.
Commande de vérification: `~/.platformio/penv/bin/pio run -e Waveshare-ESP32-S3`.

Résultat build:

- statut: succès
- génération cfgdocs: `docs=624`, `cfgmods=107`, `modules web chunks=134`
- image: `2 055 846` octets sur `6 553 600` octets, soit `31,4 %` de la
  partition applicative
- RAM PlatformIO: `106 916` octets sur `327 680`, soit `32,6 %`
- DIRAM résumé linker: `191 354` octets utilisés sur `341 760`, soit `55,99 %`

Conclusion globale: le profil est compilable et structurellement cohérent avec le
TFT actif. La partition OTA 16 Mio conserve une marge applicative confortable; les
modules réseau, interface web, MQTT, OTA, IO et logique piscine restent à surveiller
pour leur consommation de RAM interne.

## Périmètre Waveshare

Le profil `Waveshare-ESP32-S3` utilise `FLOW_PROFILE_WAVESHARE=1` et
`FLOW_BOARD_WAVESHARE_ESP32_S3=1`. Il instancie `ModuleInstances` dans
`src/Profiles/Waveshare/WaveshareModuleInstances.cpp` puis enregistre les modules
dans `src/Profiles/Waveshare/WaveshareBootstrap.cpp`.

Modules enregistrés au runtime:

1. `loghub`
2. `activitylog`
3. `log.bootcapture` si `FLOW_ENABLE_BOOT_LOG_CAPTURE=1` (actif dans cet env)
4. `log.dispatcher`
5. `log.sink.serial`
6. `eventbus`
7. `config`
8. `datastore`
9. `cmd`
10. `hmi.udp.server`
11. `hmi`
12. `hmi.buzzer`
13. `alarms`
14. `ethernet`
15. `wifi`
16. `wifiprov`
17. `webinterface`
18. `fwupdate`
19. `tft.s3` si `FLOW_ENABLE_TFT_S3=1` (actif dans cet env)
20. `time`
21. `mqtt`
22. `ha` seulement si `mqtt/enabled=true` dans NVS au boot
23. `system`
24. `io`
25. `poollogic`
26. `pooldev`
27. `sysmon`

Modules compilés mais non enregistrés par ce profil:

- `log.sink.alarm`: présent dans le build, absent de `registerModules()`.

Modules exclus explicitement par `build_src_filter` de `Waveshare-ESP32-S3`:

- profils `FlowIO`, `Supervisor`, `FlowConnectDisplay`, `Micronova`
- modules `Micronova/*`
- modules `FlowConnectDisplay/*`
- `i2ccfg.server`, `i2ccfg.client`
- `SupervisorHMIModule`

Notes Waveshare importantes:

- Ethernet W5500 activé par build flag `ENABLE_ETHERNET=1`; câblage dans
  `src/Board/WaveshareBoard.h`.
- TFT ST7789 local activé par `FLOW_ENABLE_TFT_S3=1`; GPIO21, 45, 1, 47, 2 et
  48 sont réservés à l'affichage.
- HMI locale principale: UART2 Nextion et endpoint UDP remote HMI, pilotés par
  `HMIModule` + `HmiUdpServerModule`.
- Buzzer Waveshare dédié: `HMIBuzzerModule`, GPIO46, actif haut.
- IO Waveshare: 16 endpoints analogiques runtime, 13 entrées digitales,
  16 sorties digitales, DS18B20 sur GPIO20/19.

## Grille Quality Gate

Chaque module est noté sur 10 critères, 0 à 3 points par critère.

| Code | Critère | 3 points | 2 points | 1 point | 0 point |
|---|---|---|---|---|---|
| QG1 | Build et périmètre | Compile dans l'env et statut runtime clair | Compile mais statut conditionnel | Compile indirectement seulement | Cassé ou ambigu |
| QG2 | Dépendances | Dépendances déclarées et minimales | Dépendances lisibles mais partielles | Couplage implicite notable | Couplage opaque |
| QG3 | Config/NVS | Config typée, bornée, documentée | Config typée mais doc partielle | Config fragile ou implicite | Pas de contrat |
| QG4 | Services/API | Service exposé/consommé stable | API interne claire | API dispersée | API floue |
| QG5 | Runtime/events | DataStore/EventBus/MQTT maîtrisés | Flux majoritairement maîtrisés | Flux incomplets | Flux opaques |
| QG6 | Tâches/ressources | Cadence, stack, core, buffers explicites | Ressources explicites mais risque moyen | Risque ressource élevé | Non maîtrisé |
| QG7 | Observabilité | Logs, compteurs, runtime UI ou diagnostic | Logs suffisants | Diagnostic limité | Aveugle |
| QG8 | Résilience/sécurité | Timeouts, retries, clamps, états d'erreur | Protections principales présentes | Protections partielles | Fragile |
| QG9 | Documentation/i18n | Fiche module + textes cfgdocs/i18n/runtime | Textes présents, fiche partielle | Textes ou fiche manquants | Non documenté |
| QG10 | Tests/validation | Tests automatisés dédiés ou scénario vérifié | Build + validation indirecte | Build seulement | Non vérifié |

Niveaux:

- A: `26..30` - Gate solide
- B: `22..25` - Gate acceptable, dette maîtrisée
- C: `18..21` - Gate fragile, action prioritaire avant extension
- D: `<18` - Gate bloquante

## Synthèse module par module

| Module | Runtime Waveshare | Score | Gate | Verdict court |
|---|---:|---:|---|---|
| `loghub` | oui | 24/30 | B | Infrastructure saine, manque surtout validation dédiée. |
| `log.bootcapture` | oui | 19/30 | C | Utile au diagnostic boot, mais fiche module absente et contrat qualité récent. |
| `log.dispatcher` | oui | 23/30 | B | Tâche dédiée et découplage corrects, couverture test faible. |
| `log.sink.serial` | oui | 22/30 | B | Stable et simple, observabilité bonne, peu testé. |
| `eventbus` | oui | 25/30 | B | Très structurant, capacités et drops visibles, pas encore A faute de tests charge. |
| `config` | oui | 24/30 | B | Contrat central solide, mais surface `ConfigStore` large et validation indirecte. |
| `datastore` | oui | 23/30 | B | Modèle runtime clair, dépendance EventBus simple, tests indirects. |
| `cmd` | oui | 23/30 | B | Registry bornée et claire, risque capacité à surveiller avec les nouveaux modules. |
| `hmi.udp.server` | oui | 21/30 | C | Protocole robuste, mais pas de fiche module et pas de tâche propre déclarée. |
| `hmi` | oui | 22/30 | B | Fonctionnellement riche; périmètre Waveshare/UDP/buzzer/LED à mieux documenter. |
| `hmi.buzzer` | oui | 20/30 | C | Module simple et borné, mais documentation runtime et tests quasi absents. |
| `alarms` | oui | 25/30 | B | Bon moteur de sécurité, reset/latch documentés, manque tests de transitions. |
| `ethernet` | oui | 22/30 | B | W5500 intégré avec timeouts/retry; fiche module manquante. |
| `wifi` | oui | 24/30 | B | Machine d'état claire, DataStore propre, doc existante. |
| `wifiprov` | oui | 21/30 | C | Fonction critique et complexe; portail AP bien protégé mais doc module absente. |
| `webinterface` | oui | 20/30 | C | Très utile mais très gros module, impact flash/heap et doc module absente. |
| `fwupdate` | oui | 20/30 | C | Surface critique OTA/SPIFFS, commandes présentes, besoin de tests de panne. |
| `time` | oui | 24/30 | B | Bonne gestion NTP/RTC/manuelle, doc utilisateur forte, tests à renforcer. |
| `mqtt` | oui | 23/30 | B | Capacités Waveshare explicites, buffer centralisé, marge mémoire à surveiller. |
| `ha` | conditionnel | 22/30 | B | One-shot adapté à la mémoire, mais dépend du boot MQTT et reste peu testé. |
| `system` | oui | 23/30 | B | Commandes simples, runtime UI système, factory reset encore incomplet. |
| `io` | oui | 23/30 | B | Très complet et aligné board Waveshare, mais surface driver énorme sans tests ciblés. |
| `poollogic` | oui | 24/30 | B | Module métier le mieux validé, mais complexité haute et scénarios safety à étendre. |
| `pooldev` | oui | 23/30 | B | Interlocks et uptime solides, tests automatisés absents. |
| `sysmon` | oui | 22/30 | B | Diagnostic précieux, mais dépend de beaucoup de services optionnels. |

Répartition:

- A: 0 module
- B: 19 modules
- C: 6 modules
- D: 0 module

Les modules C ne bloquent pas le build actuel, mais ce sont les zones où une
extension fonctionnelle risque de créer des régressions peu visibles:
`log.bootcapture`, `hmi.udp.server`, `hmi.buzzer`, `wifiprov`,
`webinterface`, `fwupdate`.

## Analyse détaillée

### `loghub` - 24/30 - Gate B

Rôle: registre de modules de logs, filtrage par niveau, suivi des truncations et
service `LogHubService`.

Points forts:

- module passif centralisé, enregistré très tôt
- service stable pour `shouldLog`, `registerModule`, résolution de nom et
  réglage des niveaux
- cfgdocs/i18n présents via `log/levels`
- base nécessaire au diagnostic de tout le firmware

Risques:

- aucune validation automatisée dédiée sur saturation, truncation, niveaux ou
  concurrence
- `LogModuleIdValue` ne contient pas d'ID dédié pour `TftS3` et mappe
  `hmi.buzzer` sur `HMIModule`, ce qui est acceptable aujourd'hui mais limite la
  granularité d'observabilité HMI

Actions recommandées:

- ajouter un test host de filtrage niveau/truncation
- séparer les IDs log `hmi`, `hmi.buzzer`, `tft.s3` si l'analyse terrain demande
  une distinction fine

### `log.bootcapture` - 19/30 - Gate C

Rôle: capturer les logs de boot pour consultation ultérieure par l'interface web.
Actif dans Waveshare via `FLOW_ENABLE_BOOT_LOG_CAPTURE=1`.

Points forts:

- enregistré juste après `loghub`, donc bien placé dans la séquence boot
- intégré à `WebInterfaceModule` pour exposition HTTP/WebSocket
- améliore fortement le diagnostic des échecs réseau/provisioning

Risques:

- pas de fiche `docs/modules/BootLogCaptureModule.md`
- contrat mémoire/capacité non documenté dans la Quality Gate précédente
- pas de test sur wraparound, saturation ou format JSON de restitution

Actions recommandées:

- documenter capacité, politique de rétention et format de lecture
- tester le ring de capture et le dump web

### `log.dispatcher` - 23/30 - Gate B

Rôle: tâche de dispatch asynchrone des logs du hub vers les sinks.

Points forts:

- tâche dédiée, découplée de la production de logs
- dépendances faibles: `loghub` et registry de sinks
- stack en RAM interne si configurée par le module
- documentation existante

Risques:

- tests de charge absents
- comportement en saturation à documenter plus explicitement côté exploitation

Actions recommandées:

- test de pression log avec sink lent
- seuils de queue à reporter dans `sysmon` ou Runtime UI si besoin terrain

### `log.sink.serial` - 22/30 - Gate B

Rôle: sortie série des logs, avec résolution des noms de modules et timestamp
quand le service temps est disponible.

Points forts:

- module simple, passif, faible surface
- documenté
- consommation tardive optionnelle du service `time`

Risques:

- pas de tests sur formatage et couleurs
- dépendance indirecte au timing de disponibilité de `TimeService`

Actions recommandées:

- test host de formatage d'une entrée log
- documenter le comportement avant/après synchronisation temps

### `eventbus` - 25/30 - Gate B

Rôle: bus d'événements interne, dispatch actif, publication de `SystemStarted`.

Points forts:

- capacités explicites: queue `40`, payload `48` octets, abonnés `50`
- logs de drops, saturation, callbacks lentes et rejets d'abonnement
- tâche dédiée sur core 1, stack `2560`, priorité `1`
- documentation de diagnostic détaillée

Risques:

- pas de tests automatisés de saturation queue/abonnés
- payload max faible: correct pour l'embarqué, mais toute nouvelle structure
  d'événement doit être contrôlée

Actions recommandées:

- test de rejet payload trop grand
- test de dispatch ordre + callbacks lentes

### `config` - 24/30 - Gate B

Rôle: exposition du `ConfigStore` global et service de patch/export JSON.

Points forts:

- service central `ConfigStoreService`
- génération cfgdocs massive validée au build (`514` docs)
- notifications `ConfigChanged` indirectes sur changement persistant
- documentation existante

Risques:

- surface large et historique: certains consommateurs gardent encore des accès
  directs au `ConfigStore`
- tests de migration/NVS non visibles dans ce profil

Actions recommandées:

- tests host pour `applyJson`, masquage secrets, inventaire modules
- préciser les accès directs encore tolérés

### `datastore` - 23/30 - Gate B

Rôle: instance partagée `DataStore`, connectée à `EventBus`.

Points forts:

- service `DataStoreService` simple
- modèle runtime généré limité aux modules utiles Waveshare:
  `io`, `ha`, `mqtt`, `time`, `wifi`, `pool`
- émissions `DataChanged` centralisées par helpers runtime

Risques:

- tests indirects seulement
- croissance du `RuntimeData` à surveiller en DIRAM

Actions recommandées:

- test host sur notification `DataChanged`
- revue mémoire à chaque ajout de champ runtime

### `cmd` - 23/30 - Gate B

Rôle: registry de commandes.

Points forts:

- capacité statique claire: `MAX_COMMANDS = 48`
- service minimal et stable
- alias métier documentés

Risques:

- Waveshare enregistre beaucoup de commandes (`system`, `time`, `alarms`,
  `pooldev`, `poollogic`, `fwupdate`); la marge de capacité doit rester suivie
- pas de test de duplicate/capacité

Actions recommandées:

- compteur runtime de commandes utilisées
- test host pour saturation et refus doublon

### `hmi.udp.server` - 21/30 - Gate C

Rôle: endpoint UDP fiable pour affichage HMI distant.

Points forts:

- token d'appairage persistant `hmi/nextion_udp/token`
- queues bornées: événements `8`, sortie `12`, payload sortie `64`
- retry fiable: `150 ms`, `7` tentatives
- timeouts offline/sleep explicites
- API dédiée pour textes Home, jauges, bits d'état, config menu, RTC

Risques:

- pas de fiche module dans `docs/modules/`
- `loop()` est vide: le service réel dépend de `HMIModule::tick`, ce couplage
  mérite d'être explicite dans la doc
- pas de tests protocole CRC/token/retry/offline

Actions recommandées:

- créer `docs/modules/HmiUdpServerModule.md`
- tester acceptation token, ACK/retry, overflow de queue et passage offline

### `hmi` - 22/30 - Gate B

Rôle: orchestration HMI locale et distante, menu de configuration, Nextion UART,
remote UDP, LED/venice selon board.

Points forts:

- service `HmiService` stable
- documentation historique riche sur Nextion et menu config
- support runtime/config large
- sur Waveshare, intégration avec UART2, UDP remote HMI, LED WS2812 et buzzer
  actif local

Risques:

- doc actuelle parle surtout du FlowIO/Nextion V1 et pas assez du profil
  Waveshare actuel
- module volumineux, avec drivers et comportements multiples
- tests automatisés absents sur menu, protocole et sorties annexes

Actions recommandées:

- compléter la fiche `HMIModule.md` avec un chapitre Waveshare-S3
- tests host du `ConfigMenuModel`
- scénario Wokwi ou banc pour Nextion/UDP

### `hmi.buzzer` - 20/30 - Gate C

Rôle: feedback sonore local Waveshare sur GPIO46.

Points forts:

- module dédié, simple, actif sur core 1, stack `2048`
- configuration persistante `hmi/buzzer/enable`
- câblage board clair dans `WaveshareBoard.h`

Risques:

- pas de fiche `docs/modules/HMIBuzzerModule.md`
- log module actuellement rattaché à `HMIModule`, ce qui réduit le diagnostic
  spécifique
- pas de tests sur priorités de patterns, enable/disable, file d'attente sonore

Actions recommandées:

- créer la fiche module
- ajouter tests host de priorité et séquencement de patterns
- envisager un `LogModuleIdValue::HMIBuzzerModule` dédié

### `alarms` - 25/30 - Gate B

Rôle: moteur d'alarmes central, latch/reset, délais ON/OFF, événements.

Points forts:

- service `AlarmService` complet
- commandes `alarms.*`
- Runtime UI et cfgdocs présents
- sémantique latch/reset bien documentée
- utilisé comme interlock sécurité par `PoolLogic`

Risques:

- pas de tests automatisés de transitions `Unknown`, latch, reset impossible,
  delay ON/OFF
- dépend de l'ordre d'enregistrement pour que HA puisse enrichir les alarmes

Actions recommandées:

- tests unitaires du moteur de transitions
- test d'intégration avec `PoolLogic` sur défaut PSI/bidons

### `ethernet` - 22/30 - Gate B

Rôle: connectivité W5500 DHCP et service `NetworkAccessService`.

Points forts:

- dépendances déclarées: `loghub`, `datastore`, `eventbus`
- machine d'état `Disabled/Starting/WaitingIp/Connected/ErrorWait`
- retry erreur `3000 ms`
- W5500 board Waveshare documenté: MOSI13, MISO14, SCLK15, CS16, INT12,
  RST39, SPI 8 MHz
- service réseau unifié pour WebInterface/Provisioning/FirmwareUpdate

Risques:

- pas de fiche `docs/modules/EthernetModule.md`
- pas de test de fallback Ethernet vers AP provisioning
- dépendance à événements Arduino réseau et timing DHCP difficile à valider hors
  matériel

Actions recommandées:

- créer la fiche module
- scénario banc: câble absent, DHCP lent, câble reconnecté, conflit WiFi/AP

### `wifi` - 24/30 - Gate B

Rôle: connectivité WiFi STA et publication runtime réseau.

Points forts:

- machine d'état documentée
- service `WifiService`
- config persistante `wifi/enabled`, `wifi/ssid`, `wifi/pass`
- runtime `WifiReady`, `WifiIp`, RSSI exposé côté web/runtime

Risques:

- pas de tests automatisés de transitions
- dépendance indirecte à `wifiprov`, `mqtt`, `time`, `sysmon`

Actions recommandées:

- tests de machine d'état avec mock WiFi
- vérifier masquage du secret WiFi dans tous les exports web/config

### `wifiprov` - 21/30 - Gate C

Rôle: portail de provisioning WiFi/AP et orchestration fallback réseau.

Points forts:

- dépendances minimales: `wifi`, `loghub`
- logique `NetworkManager` claire: Ethernet prioritaire, délai Ethernet
  `7000 ms`, délai WiFi `12000 ms`
- garde-fous AP: précheck heap interne, retry, délai de stabilité, grace period
  client
- integration HMI pour état portail

Risques:

- pas de fiche `docs/modules/WifiProvisioningModule.md`
- module critique au boot, difficile à diagnostiquer sans scénario terrain
- pas de tests automatisés sur les raisons de portail

Actions recommandées:

- créer fiche module avec diagramme états réseau
- tests host de `FlowNetwork::NetworkManager`
- scénario: credentials absents, Ethernet OK, WiFi timeout, client AP connecté

### `webinterface` - 20/30 - Gate C

Rôle: serveur HTTP/WebSocket local, assets SPIFFS, logs web, config, runtime,
commandes et endpoints update.

Points forts:

- tâche dédiée core 0, stack `4096` en Waveshare
- `canStart()` attend un `NetworkAccessService` en mode Station ou AP
- start delay Waveshare `3000 ms`
- service `WebInterfaceService`: pause, health, activité, source WebSocket
- nombreux garde-fous heap/asset/build-busy
- build SPIFFS validé dans la chaîne PlatformIO

Risques:

- pas de fiche `docs/modules/WebInterfaceModule.md`
- module très volumineux et contributeur probable à la pression flash
- surface HTTP/WS large sans tests automatisés visibles
- dépendances Waveshare déclarées volontairement minimales (`loghub`, `wifi`) mais
  nombreux services sont attachés dynamiquement; ce compromis doit être documenté

Actions recommandées:

- créer fiche module avec routes principales, modes AP/STA et budget mémoire
- tests HTTP host ou Wokwi sur endpoints critiques: `/health`, config, runtime,
  logs, update status
- suivre taille binaire par module ou au moins par familles réseau/web

### `fwupdate` - 20/30 - Gate C

Rôle: mise à jour firmware/SPIFFS/Nextion et service `FirmwareUpdateService`.

Points forts:

- tâche dédiée core 0, stack `6144` en Waveshare
- start delay Waveshare `6000 ms`
- commandes `fw.update.*`, `fw.nextion.reboot`, `fw.flowio.hw_reboot`
- service pour start/status/busy/config/manifest
- état interne explicite: `Idle/Queued/Downloading/Flashing/Rebooting/Done/Error`
- pins downstream désactivés proprement dans le board Waveshare (`-1`)

Risques:

- pas de fiche `docs/modules/FirmwareUpdateModule.md`
- module critique sécurité/fiabilité: OTA, SPIFFS, reboot, pause web/HMI
- pas de tests de panne réseau, URL invalide, manifest invalide, partition pleine
- noms historiques `flowio/supervisor/nextion` pas toujours naturels pour un
  profil Waveshare local

Actions recommandées:

- créer fiche module spécifique Waveshare/Waveshare
- tests de parsing URL/manifest et transitions d'erreur
- vérifier UX web: update impossible quand marge flash ou partition insuffisante

### `time` - 24/30 - Gate B

Rôle: temps système, NTP, RTC interne PCF85063, heure manuelle, scheduler.

Points forts:

- doc utilisateur très complète
- service `TimeService` et `TimeSchedulerService`
- qualité temps explicite: NTP, RTC trusted, manuel, RTC untrusted, invalid
- pour Waveshare, flags `FLOW_RTC_PCF85063=1` et
  `FLOW_TIME_PREFER_INTERNAL_RTC=1`
- stack `4096`, core 1

Risques:

- pas de tests automatisés visibles sur DST/timezone/scheduler
- logique très longue et sensible aux corrections d'heure

Actions recommandées:

- tests host scheduler/fuseau/DST
- scénario RTC absent, RTC incohérent, NTP tardif, heure manuelle

### `mqtt` - 23/30 - Gate B

Rôle: transport MQTT, producteurs runtime/config, commandes entrantes.

Points forts:

- capacités Waveshare dans `WaveshareBoard.h`: stack `7168`, RX queue `8`,
  producteurs `24`, jobs `192`, queues `80/80/128`
- buffers Waveshare explicites: topic `70`, dynamic topic `160`, rx payload
  `384`, ack/reply/publish `1536`
- service `MqttService`, Runtime UI, DataStore runtime MQTT
- producteurs runtime `poollogic`, `io`, `pooldev` enregistrés au boot si MQTT
  est activé

Risques:

- module mémoire/surface réseau important dans une image déjà à `92,8 %`
- tests automatisés absents sur backpressure, oversize, parse fail, HA one-shot
- `ha` est conditionnel au NVS MQTT, donc certaines entités ne sont présentes
  qu'après activation

Actions recommandées:

- tests de queues et payload oversize
- dashboard sysmon: occupation jobs/queues MQTT
- revue des buffers à chaque ajout HA/runtime

### `ha` - 22/30 - Gate B

Rôle: Home Assistant Discovery via MQTT.

Points forts:

- capacités Waveshare board: sensors `48`, binary sensors `16`, switches `16`,
  numbers `30`, buttons `24`, selects `6`
- mode one-shot activé par `FLOW_HA_ONESHOT_DISCOVERY=1`, adapté à la RAM
- service `HAService`
- discovery alimenté par IO/PoolLogic/PoolDevice

Risques:

- module enregistré seulement si `mqtt/enabled=true` dans NVS
- one-shot libère les tables; il faut éviter les ajouts tardifs d'entités
- pas de tests automatisés de payload discovery et cleanup

Actions recommandées:

- test de génération discovery sur profil Waveshare
- journaliser un résumé entités publiées/libérées en mode one-shot

### `system` - 23/30 - Gate B

Rôle: commandes système et locale/langue.

Points forts:

- module passif simple
- commandes `system.ping`, `system.reboot`, `system.factory_reset`
- service `Locale`
- Runtime UI système firmware/uptime/heap

Risques:

- `factory_reset` est encore documenté comme reboot ACK; purge NVS à finaliser
- tests absents sur normalisation langue et commandes

Actions recommandées:

- implémenter ou renommer clairement le factory reset réel
- test host des commandes système sans reboot effectif

### `io` - 23/30 - Gate B

Rôle: couche IO Waveshare, drivers, endpoints, registry, snapshots runtime.

Points forts:

- board Waveshare très détaillée: bus I2C GPIO42/41, OneWire GPIO20/19,
  TCA9554 sorties EXIO, entrées digitales, capteurs ADS/BME/BMP/SHT/INA/DS18B20
- service `IOServiceV2`
- Runtime UI et runtime snapshot MQTT
- drivers et endpoints séparés
- capacités board explicites: `16` analogiques, `13` entrées digitales,
  `16` sorties digitales, slots config alignés

Risques:

- très grande surface de drivers, timing I2C/OneWire et calibration
- build seulement; pas de tests driver/endpoints visibles
- risques terrain: bus bloqué, capteur absent, adresses I2C concurrentes,
  saturation runtime MQTT/HA

Actions recommandées:

- tests host endpoints calibration/polarité/momentary
- scénario Wokwi I2C/OneWire minimal
- doc Waveshare IO à garder alignée avec `WaveshareBoard.h`

### `poollogic` - 24/30 - Gate B

Rôle: logique métier piscine: filtration, modes, régulations, sécurité,
chauffage, robot, refill, O2.

Points forts:

- module métier très documenté
- Runtime UI et runtime snapshot
- commandes nombreuses
- interlocks alarmes/PSI/bidons/eau
- tests unitaires existants sur `FiltrationWindow`
- intégration scheduler/time, IO, PoolDevice, Alarm, HA/MQTT

Risques:

- complexité élevée et beaucoup de branches de config
- tests existants encore concentrés sur la fenêtre filtration
- scénarios safety complets non automatisés

Actions recommandées:

- étendre tests à pH, désinfection, O2, heat assist, refill, winter mode
- tests d'intégration avec `PoolDevice` et `Alarm`

### `pooldev` - 23/30 - Gate B

Rôle: pilotage des équipements physiques, états demandés/réels, interlocks,
uptime quotidien, refill.

Points forts:

- service `PoolDeviceService`
- Runtime UI et runtime snapshot
- dépendances et interlocks par slots
- commandes write/refill/uptime reset
- liaison domain presets -> IO bindings dans bootstrap Waveshare

Risques:

- pas de tests automatisés sur dépendances, max uptime, refill, erreurs IO
- module sensible aux bindings de domaine/board

Actions recommandées:

- tests host `dependenciesSatisfied_`, max uptime et refill
- vérifier tous les presets pool contre `WaveshareIoLayout`

### `sysmon` - 22/30 - Gate B

Rôle: supervision heap, réseau, tasks, NVS, santé web/update.

Points forts:

- tâche dédiée core 0
- inspecte `ModuleManager` pour watermarks de stack
- traces périodiques utiles en terrain
- lit plusieurs services optionnels: WiFi, NetworkAccess, WebInterface,
  FirmwareUpdate, Command, ConfigStore, HA

Risques:

- dépend de nombreux services pouvant apparaître tard ou conditionnellement
- pas de tests automatisés sur format de health/stack
- trace périodique peut masquer les signaux critiques si elle est trop bavarde

Actions recommandées:

- ajouter un résumé compact des modules C et des marges flash/RAM au boot
- tests de format stack/heap pour éviter régressions de parsing terrain

## Modules hors gate runtime Waveshare

### `log.sink.alarm`

Statut: compilé, non enregistré par `WaveshareBootstrap`.

Impact:

- pas d'effet runtime Waveshare actuellement
- peut augmenter légèrement la taille binaire car il est compilé
- si l'intention est de ne jamais l'utiliser sur Waveshare, l'exclure du
  `build_src_filter` améliorerait la clarté et potentiellement la marge flash

Action recommandée:

- décider explicitement: soit l'enregistrer comme sink d'alarmes, soit l'exclure
  de l'environnement Waveshare

## Priorités Quality Gate

Priorité 1 - réduire les gates C:

1. Créer les fiches manquantes:
   `BootLogCaptureModule.md`, `HmiUdpServerModule.md`,
   `HMIBuzzerModule.md`, `WifiProvisioningModule.md`,
   `WebInterfaceModule.md`, `FirmwareUpdateModule.md`,
   `EthernetModule.md`.
2. Ajouter tests host simples:
   `NetworkManager`, `ConfigMenuModel`, `HmiUdpServer` token/retry,
   `CommandRegistry`, transitions `AlarmModule`.
3. Documenter le couplage volontaire Waveshare:
   `WebInterface` démarre tôt avec dépendances minimales, puis attache les
   services dynamiquement.

Priorité 2 - surveiller la marge binaire:

- la partition applicative est à `92,8 %`
- toute nouvelle page web, tout nouveau driver IO ou toute extension OTA/MQTT
  doit être accompagnée d'une mesure de taille
- envisager d'exclure `log.sink.alarm` si non utilisé par Waveshare

Priorité 3 - renforcer les scénarios critiques:

- boot Ethernet OK / Ethernet absent / WiFi OK / WiFi timeout / portail AP
- OTA firmware et SPIFFS: URL invalide, manifest invalide, réseau coupé, reboot
- alarmes de sécurité piscine: PSI bas/haut, bidons bas, niveau eau bas,
  reset latch
- IO: capteur absent, bus I2C bloqué, DS18B20 absent, sortie TCA9554 inactive

## Historique de l'ancienne matrice

L'ancienne page comparait `FlowIO` et `Supervisor`. Pour retrouver les fiches
module générales, utiliser `docs/modules/`. Pour le profil Waveshare, la source
de vérité runtime est désormais:

- `platformio.ini`, section `[env:Waveshare-ESP32-S3]`
- `src/Profiles/Waveshare/WaveshareProfile.cpp`
- `src/Profiles/Waveshare/WaveshareModuleInstances.cpp`
- `src/Profiles/Waveshare/WaveshareBootstrap.cpp`
- `src/Profiles/Waveshare/WaveshareIoAssembly.cpp`
- `src/Board/WaveshareBoard.h`
