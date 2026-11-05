# HMIModule (`moduleId: hmi`)

## Rôle

Couche d'orchestration HMI locale:
- lit/édite la configuration via `ConfigStoreService`
- maintient un menu de configuration paginé (6 lignes/page)
- délègue le rendu et les entrées au driver matériel interactif (`IHmiDriver`)
- pilote en option des sorties d'affichage annexes (LEDs façade, émetteur RF433 Venice)

En V1, le driver interactif embarqué est `NextionDriver`.
Une sortie déportée `TfaVeniceRf433Sink` peut aussi émettre la température d'eau
vers un récepteur TFA Venice compatible.
Le menu Nextion de configuration est activé dans le build FlowIO, mais il ne
rend `pageCfgMenu` qu'après une commande d'ouverture explicite depuis l'écran.
Le modèle menu est stateless côté RAM longue durée : il relit la page courante
depuis le `ConfigStore` et applique les changements simples immédiatement.

## Dépendances

- `loghub`
- `config`
- `eventbus`
- `datastore`
- `io`
- `alarms`
- `command`
- `time`
- `wifi`
- `i2c_bus`

## Service exposé

- `hmi` -> `HmiService`
  - `requestRefresh()`
  - `openConfigHome()`
  - `openConfigModule(module)`
  - `buildConfigMenuJson(out)`

## Fonctionnement menu config

- Mode browse: liste hiérarchique des topics immédiats via `listModules()`
- Mode édition: clés/valeurs de la branche sélectionnée via `toJsonModule()`
- Pagination: 6 lignes
- Actions UI: `Home`, `Back`, `Valider`, `Prev`, `Next`
- En mode browse, `Prev/Next` paginent les `tL0..tL5` et `tV0..tV5` sont masqués.
- En mode édition, `Prev/Next` paginent les attributs `tL0..tL5` / `tV0..tV5`.
- Les changements simples (`Switch`, `Select`, `Slider`, `Text`) sont appliqués immédiatement via un patch JSON ciblé `applyJson()`.
- Les valeurs de la page d'édition courante sont rafraîchies toutes les `5s`; la page complète n'est pas rerendue en continu.
- `Valider` est réservé pour une évolution ultérieure; le modèle léger actuel ne conserve pas de cache `dirty`.
- FlowIO ne change pas la page Nextion; le `Preinitialize Event` de `pageCfgMenu` doit envoyer `printh 23 02 50 0A` (ou `printh 23 06 50 0A` + `print <ctx_ref_u32_le>`) pour activer le rendu du menu côté FlowIO.
- Le `Page Exit Event` Nextion de `pageCfgMenu` doit envoyer `printh 23 02 51 06` pour désactiver le rendu actif du menu côté FlowIO.

Le modèle de menu est volontairement stateless côté RAM longue durée:
- pas de cache persistant des modules;
- pas de cache persistant des lignes de configuration;
- une page est reconstruite à la demande depuis `ConfigStore`.

Le chemin courant est exposé sous forme breadcrumb:
`flow > cfg > <module>`

## Typage des champs

Types de widget supportés:
- `Text`
- `Switch`
- `Select`
- `Slider`

Par défaut, le type est déduit depuis JSON (bool/int/float/texte).
Des hints peuvent forcer le widget et les contraintes (bornes/options).

## Driver Nextion (V1)

- Transport: mappé via `src/Board/BoardSerialMap.h`
  - mode normal: logs -> `Serial`, Nextion -> `Serial2` (RX16/TX17)
  - inversion: définir `FLOW_SWAP_LOG_HMI_SERIAL=1` au build pour
    logs -> `Serial2` (RX16/TX17), Nextion -> `Serial`
  - note: en mode inversé, UART0 reste utilisé par les messages ROM boot ESP32
- Rendu page config sur objets Nextion conventionnels:
  - `tPath`, `tL0..tL5`, `tV0..tV5`
  - `bHome`, `bBack`, `bValid`, `bPrev`, `bNext`, `bR0..bR5`
  - `vaEditType0..vaEditType5` (variables numeriques locales de `pageCfgMenu`)
  - `vaCtxRef` (variable numerique locale de `pageCfgMenu`)
  - `nPage`, `nPages`
- Entrées:
  - protocole binaire custom `# <len> <opcode> <payload...>`
- Ouverture menu:
  - bouton paramètres côté Home: `page pageCfgMenu`
  - `Preinitialize Event` de `pageCfgMenu`: `printh 23 02 50 0A`
  - `Page Exit Event` de `pageCfgMenu`: `printh 23 02 51 06`
- Navigation:
  - mode browse: seuls les topics immédiats sont visibles dans `tL0..tL5`; `tV0..tV5` sont masqués
  - clic `tLN`: `printh 23 02 52 0N` pour entrer dans la branche
  - clic `bRN`: `printh 23 02 55 0N` pour éditer les attributs de la branche
  - mode édition: `tL0..tL5` affichent les clés, `tV0..tV5` affichent les valeurs et servent de dual-state button pour les booléens; refresh valeurs toutes les `5s`
  - `vaEditTypeN.val` indique le clavier a ouvrir pour la ligne `N`
    (`0` texte, `1` entier, `2` decimal/float, `3` booleen)
  - valeur non-switch: FlowIO désactive `tVN` avec `tsw tVN,0`, force `tVN.val=0`, puis met `tVN.txt`
  - valeur switch: FlowIO active `tVN` avec `tsw tVN,1`, met `tVN.val=0/1`, puis `tVN.txt` à `OFF/ON`
  - changement switch `tVN`: `printh 23 02 53 0N`
  - fermeture recommandée: `bBack` à la racine + changement de page local Nextion
- RTC:
  - lit `rtc0..rtc5` au boot si l'heure NTP n'est pas disponible
  - écrit `rtc0..rtc5` quand l'ESP dispose d'une heure NTP fiable
- Référence de protocole et objets:
  - voir `docs/integration/nextion-esp-protocol.md`

## Événements internes

Sur `EventId::ConfigChanged`, si le module affiché est impacté, l'écran est resynchronisé automatiquement.

## Config HMI

Trois branches de config dédiées sont exposées:

- `hmi/nextion`
  - `enabled`: active/désactive l'écriture vers l'écran Nextion
- `hmi/leds`
  - `enabled`: active/désactive le bandeau de LEDs PCF8574A dédié
  - `waveshare_enabled`: active/désactive la LED WS2812 Waveshare d'état système
- `hmi/venice`
  - `enabled`: active/désactive l'émetteur RF433 Venice
  - `tx_gpio`: GPIO TX du module 433 MHz
  - la période, le canal et l'identifiant capteur restent fixés aux valeurs par défaut du driver

Le bandeau frontal est piloté directement par `HMIModule` via le bus I2C
partagé. Son PCF8574AM/TR est fixé à l'adresse `0x3C` et ses huit sorties sont
actives à l'état bas. Quand `hmi/leds/enabled=false`, les huit LEDs sont
explicitement éteintes. Cette puce dédiée n'est pas exposée comme expander IO.

## Identité visuelle LED WS2812 Waveshare

La LED WS2812 Waveshare affiche un seul état système à la fois. Les services ne
pilotent pas directement la LED: ils déclarent des conditions actives ou
inactives, puis le HMI choisit l'état visible selon une priorité fixe.

Priorité d'affichage:

| Priorité | État | Signification | Couleur / animation |
| --- | --- | --- | --- |
| 1 | `AlarmActive` | Au moins une alarme active | Rouge, clignotement rapide |
| 2 | `DomainSlotError` | Au moins un domain slot affecté est en erreur | Orange, breathe lent |
| 3 | `CaptivePortalActive` | Portail captif actif, action utilisateur attendue | Cyan/turquoise, breathe lent |
| 4 | `OtaInProgress` | Mise à jour OTA en cours | Violet, breathe |
| 5 | `NetworkLost` | Aucun réseau attendu n'est opérationnel | Bleu, pulse rapide |
| 6 | `Booting` | Démarrage en cours | Blanc, breathe lent |
| 7 | `Normal` | Aucun état prioritaire actif | Vert, breathe très lent et faible luminosité |

Le mode `Booting` est actif par défaut au démarrage et disparaît après
`setBootComplete()`. Le mode `Normal` n'est pas demandé par les services: il est
calculé automatiquement quand aucune condition prioritaire n'est active.

Les transitions entre états se font par fondu non bloquant. Les animations sont
basées sur `millis()` et ne doivent pas utiliser `delay()`.
