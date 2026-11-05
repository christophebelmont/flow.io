# Protocole ESP / Nextion (FlowIO)

## Objet

Ce document décrit le contrat actuel entre `FlowIO` et un écran Nextion piloté
par `HMIModule` / `NextionDriver`.

Le protocole est volontairement mixte :
- `ESP -> Nextion` : commandes Nextion natives (`.txt`, `.val`, `page ...`)
- `Nextion -> ESP` : protocole binaire compact `# <len> <opcode> <payload...>`

Le port série utilisé côté FlowIO est `Serial2` sur `RX16 / TX17` à `115200`.

## Principes

- Les valeurs métier affichées (`pH`, `ORP`, températures, message d'erreur)
  sont formatées côté ESP puis envoyées vers des variables texte globales.
- L'heure affichée est formatée côté ESP puis envoyée vers `tTime.txt`. La date
  reste calculée localement par le Nextion depuis son RTC; l'ESP fournit le
  texte du jour de semaine et du mois.
- Les états discrets et indicateurs compacts sont envoyés en `.val`.
- L'écran Nextion n'est pas la source de vérité : il émet des intentions ou des
  commandes UI, puis `FlowIO` rafraîchit l'affichage à partir du `DataStore`,
  du `ConfigStore` et des états runtime.
- Les objets Nextion utilisés par l'ESP doivent être considérés comme faisant
  partie du contrat de firmware HMI.

## Objets Nextion pilotés par l'ESP

### Page Home

Variables texte globales :
- `globals.vaVersion.txt`
- `globals.vaWaterTemp.txt`
- `globals.vaAirTemp.txt`
- `globals.vaPhValue.txt`
- `globals.vaOrpValue.txt`
- `globals.vaDayText.txt`
- `globals.vaMonthText.txt`
- `globals.vaErrMsg.txt`

Objets numériques :
- `vapHPercent.val`
- `vaOrpPercent.val`
- `globals.vaStates.val`
- `globals.vaAlarms.val`

Sémantique :
- `globals.vaVersion.txt` : version du TFT au format `M.m.p`, 1 à 2 chiffres
  par segment, par exemple `6.0.0`; longueur maximale `8` caractères
- `globals.vaWaterTemp.txt` : exemple `27.4°C`
- `globals.vaAirTemp.txt` : exemple `21.8°C`
- `globals.vaPhValue.txt` : exemple `7.18`
- `globals.vaOrpValue.txt` : exemple `650`
- `globals.vaDayText.txt` : jour de semaine, exemple `Vendredi`
- `globals.vaMonthText.txt` : mois, exemple `Avril`
- `globals.vaErrMsg.txt` : message court, limite recommandée `31` caractères
- `vapHPercent.val` : jauge `0..280`, `140` à la consigne pH
- `vaOrpPercent.val` : jauge `0..280`, `140` à la consigne ORP
- `globals.vaStates.val` : bitmap d'état compact
- `globals.vaAlarms.val` : bitmap d'alarmes compact

### Bitmap `globals.vaStates`

Bits actuellement utilisés :
- bit `0` : filtration ON
- bit `1` : pompe pH ON
- bit `2` : pompe ORP ON
- bit `3` : mode auto global
- bit `4` : mode auto pH
- bit `5` : mode auto ORP
- bit `6` : mode hiver
- bit `7` : WiFi connecté
- bit `8` : MQTT connecté
- bit `9` : robot ON
- bit `10` : éclairage ON
- bit `11` : chauffage ON
- bit `12` : remplissage ON

### Bitmap `globals.vaAlarms`

Bits actuellement utilisés :
- bit `0` : niveau eau bas
- bit `1` : bidon pH bas
- bit `2` : bidon chlore bas
- bit `3` : alarme temps max pompe pH
- bit `4` : alarme temps max pompe ORP
- bit `5` : alarme PSI

### Page de configuration

Le driver rend la configuration sur la page `pageCfgMenu` après réception
de l'annonce de page `printh 23 02 50 0A` (ou variante contextualisée
`printh 23 06 50 0A <ctx_ref_u32_le>`) depuis Nextion.
Le modèle côté ESP est volontairement léger en RAM : il ne garde pas de cache
complet des modules ou des lignes, et reconstruit la page courante à la demande
depuis le `ConfigStore`.

- `tPath`
- `bHome`
- `bBack`
- `bValid`
- `bPrev`
- `bNext`
- `nPage`
- `nPages`
- `tL0` .. `tL5`
- `tV0` .. `tV5`
- `bR0` .. `bR5`
- `vaEditType0` .. `vaEditType5` (variables locales de `pageCfgMenu`)
- `vaCtxRef` (variable locale numerique de `pageCfgMenu`)

Sémantique :
- `tPath` : breadcrumb courant
- `tLx` : libellé de ligne et zone tactile d'entrée dans une branche
- `tVx` : dual-state button utilisé comme valeur de ligne en mode édition;
  masqué en mode browse
- `bRx` : zone tactile/action spéciale de ligne, visible uniquement si la
  ligne existe
- `bValid` : réservé ; dans le modèle léger actuel, les changements simples
  sont appliqués immédiatement clé par clé au `ConfigStore`
- `vaEditTypeN.val` : type de saisie clavier pour la ligne `N`
  - `0` : texte
  - `1` : entier
  - `2` : decimal/float
  - `3` : booleen
- `vaCtxRef.val` : token de contexte courant fourni par FlowIO pour permettre
  un retour clavier vers la vue exacte en cours d'edition.
- Mode browse : seuls les topics immédiats sont affichés dans `tLx`.
- Mode édition : les attributs de la branche sélectionnée sont affichés dans
  `tLx/tVx`; un refresh léger des valeurs est fait toutes les `5s`.
- Pour une valeur non-switch, FlowIO envoie `tsw tVx,0`, `tVx.val=0` puis
  `tVx.txt`.
- Pour un `Switch`, FlowIO envoie `tsw tVx,1`, `tVx.val=0/1` puis
  `tVx.txt=" OFF"` ou `tVx.txt=" ON"`.

## Protocole Nextion -> ESP

## RTC Nextion

L'écran Nextion expose une RTC locale utilisée comme source de secours pour
FlowIO lorsque l'heure NTP n'est pas disponible au boot.

Registres RTC Nextion utilisés :
- `rtc0` : année, par exemple `2026`
- `rtc1` : mois `1..12`
- `rtc2` : jour `1..31`
- `rtc3` : heure `0..23`
- `rtc4` : minute `0..59`
- `rtc5` : seconde `0..59`

Séquence au boot côté ESP :
- si l'heure ESP est déjà synchronisée NTP, l'ESP ne lit pas la RTC Nextion ;
- si WiFi ou NTP est désactivé, l'ESP lit immédiatement `rtc0..rtc5` ;
- si WiFi et NTP sont actifs mais que l'heure n'est toujours pas synchronisée
  après `30s`, l'ESP lit `rtc0..rtc5` ;
- si la lecture RTC est valide, `TimeService` est réglé depuis cette heure et
  les fonctions métier peuvent utiliser le scheduler normalement ;
- tant que l'heure provient de la RTC Nextion, elle est marquée comme source
  externe et n'est pas utilisée pour réécrire immédiatement la RTC Nextion.

Synchronisation ESP -> Nextion :
- dès que l'ESP obtient une heure synchronisée, il écrit `rtc0..rtc5` pour
  remettre la RTC Nextion à l'heure ;
- ensuite, tant que l'heure ESP reste synchronisée, l'ESP réécrit la RTC
  Nextion une fois par jour local ;
- les valeurs écrites sont en heure locale, pas en UTC.

Commandes Nextion utilisées par l'ESP :

```text
get rtc0
get rtc1
get rtc2
get rtc3
get rtc4
get rtc5
rtc0=<year>
rtc1=<month>
rtc2=<day>
rtc3=<hour>
rtc4=<minute>
rtc5=<second>
```

## Format binaire

Format :

```text
0x23 <len> <opcode> <payload...>
```

Où :
- `0x23` est le caractère `#`
- `len` est le nombre d'octets qui suivent
- `opcode` est le type de message
- `payload` dépend de l'opcode

Exemple :

```text
23 03 60 01 01
```

Décodage :
- `23` : début de trame
- `03` : trois octets suivent
- `60` : opcode `HOME_ACTION`
- `01` : action `FILTRATION_SET`
- `01` : valeur `true`

## Opcodes supportés

### `0x50` : `PAGE`

Payload :
- `page_code` (`uint8`, code protocolaire choisi cote Nextion)
- optionnel : `ctx_ref` (`uint32` little-endian) pour reprise de contexte menu

Usage :
- utilisé par Nextion pour informer l'ESP de la page actuellement affichée
- `page_code=0x0A` ouvre le rendu du menu configuration cote FlowIO
- FlowIO ne change pas la page Nextion; il rend seulement le contenu si
  `pageCfgMenu` a annonce son entree

Exemple Nextion :

```text
printh 23 02 50 0A
```

Exemple Nextion contextualisé :

```text
printh 23 06 50 0A
print globals.vaKbdCtxRef.val
```

Exemple :
- `0A` = page config

### `0x51` : `NAV`

Payload :
- `nav_id` (`uint8`)

Valeurs supportées :
- `0x01` : `HOME`
- `0x02` : `BACK`
- `0x03` : `VALIDATE`
- `0x04` : `NEXT_PAGE`
- `0x05` : `PREV_PAGE`
- `0x06` : `CONFIG_EXIT`, a envoyer dans le `Page Exit Event` de
  `pageCfgMenu` pour demander a FlowIO d'arreter le rendu actif du menu

Exemple Nextion :

```text
printh 23 02 51 04
```

### `0x52` : `ROW_ACTIVATE`

Payload :
- `row` (`uint8`, `0..5`)

Exemple :

```text
printh 23 02 52 03
```

Active la ligne `3`.
En mode browse, cette commande entre dans la sous-branche affichée par `tL3`.
En mode édition, elle conserve le comportement d'action simple de la ligne
quand le widget le permet.

### `0x53` : `ROW_TOGGLE`

Payload :
- `row` (`uint8`, `0..5`)

Exemple :

```text
printh 23 02 53 01
```

Toggle la ligne `1`.

### `0x54` : `ROW_CYCLE`

Payload :
- `row` (`uint8`, `0..5`)
- `dir` (`uint8`)

Valeurs `dir` :
- `0x00` ou `0xFF` : précédent
- toute autre valeur : suivant

Exemple :

```text
printh 23 03 54 02 01
```

Fait avancer la ligne `2`.

### `0x55` : `ROW_EDIT`

Payload :
- `row` (`uint8`, `0..5`)

Exemple :

```text
printh 23 02 55 03
```

Passe en mode édition des attributs de la branche affichée par la ligne `3`.
Cette commande est prévue pour un geste/objet distinct du clic `tLx` qui, lui,
sert à entrer dans la branche.

### `0x56` : `ROW_SET_TEXT`

Payload :
- `row` (`uint8`, `0..5`)
- `text` (`bytes`, texte non terminé obligatoirement par zéro)

Exemple :

```text
printh 23 06 56 00 74 65 73 74
```

Définit la valeur texte de la ligne `0` à `test`.

Limite pratique :
- Nextion UART direct : `47` caractères utiles max (buffer `HmiEvent.text`)
- Flow Connect Display UDP : `47` caractères utiles max (payload `HmiUdpEventPayload.text`)

### `0x57` : `ROW_SET_SLIDER`

Payload :
- `row` (`uint8`, `0..5`)
- soit `value` (`uint8`, `0..255`)
- soit `value` (`float`, 4 octets natifs little-endian ESP32)

Exemple valeur courte :

```text
printh 23 03 57 00 64
```

Définit le slider de la ligne `0` à `100`.

### `0x60` : `HOME_ACTION`

Payload :
- `action_id` (`uint8`)
- `value` (`uint8`)

Actions supportées :
- `0x01` : `FILTRATION_SET`
- `0x02` : `AUTO_MODE_SET`
- `0x03` : `SYNC_REQUEST`
- `0x04` : `CONFIG_OPEN` (compatibilite no-op cote FlowIO; ne change pas la page Nextion)
- `0x05` : `PH_PUMP_SET`
- `0x06` : `ORP_PUMP_SET`
- `0x07` : `PH_PUMP_TOGGLE`
- `0x08` : `ORP_PUMP_TOGGLE`
- `0x09` : `FILTRATION_TOGGLE`
- `0x0A` : `AUTO_MODE_TOGGLE`
- `0x0B` : `ORP_AUTO_MODE_TOGGLE`
- `0x0C` : `PH_AUTO_MODE_TOGGLE`
- `0x0D` : `WINTER_MODE_TOGGLE`
- `0x0E` : `LIGHTS_TOGGLE`
- `0x0F` : `ROBOT_TOGGLE`
- `0x10` : `DISPLAY_WIFI_FACTORY_RESET` (consommee localement par Flow Connect Display)

Valeur :
- `0x00` : faux / OFF
- `0x01` : vrai / ON

Exemples :

Filtration ON :

```text
printh 23 03 60 01 01
```

Filtration OFF :

```text
printh 23 03 60 01 00
```

Mode auto ON :

```text
printh 23 03 60 02 01
```

Demande de resynchronisation Home :

```text
printh 23 03 60 03 01
```

Demande logique d'ouverture du menu configuration, sans changement de page
cote FlowIO. Le flux recommande reste de changer la page localement cote
Nextion puis d'envoyer `PAGE=0x0A` depuis `pageCfgMenu`.

```text
printh 23 03 60 04 01
```

Forçage pompe pH ON :

```text
printh 23 03 60 05 01
```

Forçage pompe chlore (ORP) ON :

```text
printh 23 03 60 06 01
```

Toggle pompe pH :

```text
printh 23 03 60 07 01
```

Toggle pompe chlore (ORP) :

```text
printh 23 03 60 08 01
```

### Commandes toggle recommandées sur la page Home

Les toggles utilisent `HOME_ACTION` (`0x60`) avec `value=01` (valeur ignorée
pour les toggles, conservée pour homogénéité de trame).

- Filtration toggle : `printh 23 03 60 09 01`
- Auto global toggle : `printh 23 03 60 0A 01`
- ORP auto toggle : `printh 23 03 60 0B 01`
- pH auto toggle : `printh 23 03 60 0C 01`
- Hivernage toggle : `printh 23 03 60 0D 01`
- Eclairage toggle : `printh 23 03 60 0E 01`
- Robot toggle : `printh 23 03 60 0F 01`
- Pompe pH toggle : `printh 23 03 60 07 01`
- Pompe chlore toggle : `printh 23 03 60 08 01`

### Commande locale Flow Connect Display

La reinitialisation Wi-Fi du firmware ecran deporte est locale au Display :
elle n'est pas envoyee a FlowIO. Elle efface les identifiants Wi-Fi du
Display, redemarre l'ESP, puis le portail captif revient au boot.

```text
printh 23 03 60 10 01
```

## Mapping vers les commandes ESP

Les actions Home sont routées via `CommandService` :

- `FILTRATION_SET`
  - commande : `poollogic.filtration.write`
  - payload envoyé par l'ESP :

```json
{"value":true}
```

- `AUTO_MODE_SET`
  - commande : `poollogic.auto_mode.set`
  - payload envoyé par l'ESP :

```json
{"value":false}
```

- `SYNC_REQUEST`
  - ne passe pas par `CommandService`
  - demande simplement à `HMIModule` de republier toutes les données Home
- `CONFIG_OPEN`
  - ne passe pas par `CommandService`
  - compatibilité uniquement; FlowIO ne change plus la page Nextion
  - le rendu du menu démarre uniquement lorsque `pageCfgMenu` envoie
    `printh 23 02 50 0A`
- `PH_PUMP_SET`
  - `value=1` : désactive `ph_auto_mode` puis force l'état de la pompe pH (`pooldevice.write`)
  - `value=0` : repasse la pompe pH OFF (`pooldevice.write`) sans réactiver l'auto
- `ORP_PUMP_SET`
  - `value=1` : passe `disinfection_type=3` (`Désactivé`) puis force l'état de la pompe ORP (`pooldevice.write`)
  - `value=0` : repasse la pompe ORP OFF (`pooldevice.write`) sans réactiver l'auto
- `PH_PUMP_TOGGLE`
  - inverse l'état réel de la pompe pH
  - si la cible est `ON`, désactive d'abord `ph_auto_mode`, puis force la pompe
- `ORP_PUMP_TOGGLE`
  - inverse l'état réel de la pompe ORP
  - si la cible est `ON`, passe `disinfection_type=3` (`Désactivé`), puis force la pompe
- `FILTRATION_TOGGLE`
  - inverse l'état réel de filtration (`poollogic.filtration.write`)
- `AUTO_MODE_TOGGLE`
  - inverse le mode auto global (`poollogic.auto_mode.set`)
- `ORP_AUTO_MODE_TOGGLE`
  - inverse `dis_auto_mode`
- `PH_AUTO_MODE_TOGGLE`
  - inverse `ph_auto_mode`
- `WINTER_MODE_TOGGLE`
  - inverse `winter_mode`
- `LIGHTS_TOGGLE`
  - inverse l'état runtime du device éclairage (`pooldevice.write`)
- `ROBOT_TOGGLE`
  - inverse l'état runtime du device robot (`pooldevice.write`)

Il n'y a pas d'ACK synchrone formel vers Nextion dans cette V1.
Le retour attendu est le rafraîchissement de l'état réel après exécution.

## Événements Touch Standard

Les événements Nextion `Send Component ID` (`0x65`) ne sont plus interprétés
par `FlowIO`. Utiliser uniquement les trames `printh 23 ...` de ce document.

## Pages et identifiants recommandés

Codes protocole recommandés côté Nextion :
- `0x00` : page Home
- `0x0A` : page `pageCfgMenu`

Ces codes ne sont pas les IDs internes des pages Nextion. Ce sont seulement
les valeurs envoyées volontairement dans `printh`.

L'opcode `PAGE` peut être émis dans le `Preinitialize Event` de chaque page.
Pour `pageCfgMenu`, c'est le signal qui demande a FlowIO d'ouvrir le menu et
d'envoyer le rendu courant.

Exemples :

Home :

```text
printh 23 02 50 00
```

Config :

```text
printh 23 02 50 0A
```

## Intégration Nextion recommandée

### Home

Les widgets Home peuvent rester entièrement locaux pour le rendu, mais les
boutons d'action doivent envoyer des trames binaires.

Exemples :
- bouton filtration ON : `printh 23 03 60 01 01`
- bouton filtration OFF : `printh 23 03 60 01 00`
- bouton auto ON : `printh 23 03 60 02 01`
- bouton sync : `printh 23 03 60 03 01`
- bouton paramètres : `page pageCfgMenu` cote Nextion, puis
  `pageCfgMenu` annonce son entree avec `printh 23 02 50 0A` (ou avec
  contexte `printh 23 06 50 0A` + `print <ctx_ref_u32_le>`).

### Config

FlowIO ne pilote pas la navigation visuelle vers ou hors de `pageCfgMenu`.
L'écran Nextion doit changer de page localement, puis annoncer l'entrée/sortie
a FlowIO.

Dans le `Preinitialize Event` de `pageCfgMenu` :

```text
printh 23 02 50 0A
```

Variante retour clavier avec contexte :

```text
if(globals.vaKbdCtxRef.val>0)
{
  printh 23 06 50 0A
  print globals.vaKbdCtxRef.val
  globals.vaKbdCtxRef.val=0
}else
{
  printh 23 02 50 0A
}
```

Dans le `Page Exit Event` de `pageCfgMenu` :

```text
printh 23 02 51 06
```

La page config peut s'appuyer sur :
- `NAV`
- `ROW_ACTIVATE`
- `ROW_EDIT`
- `ROW_TOGGLE`
- `ROW_CYCLE`
- `ROW_SET_TEXT`
- `ROW_SET_SLIDER`

Exemples :
- `bHome` : `printh 23 02 51 01`
- `bBack` : `printh 23 02 51 02`
- `bValid` : `printh 23 02 51 03`
- `bNext` : `printh 23 02 51 04`
- `bPrev` : `printh 23 02 51 05`
- `tL0` : `printh 23 02 52 00`
- `tL1` : `printh 23 02 52 01`
- `bR0` / commande édition ligne 0 : `printh 23 02 55 00`
- `bR1` / commande édition ligne 1 : `printh 23 02 55 01`
- changement switch `tV0` : `printh 23 02 53 00`
- changement switch `tV1` : `printh 23 02 53 01`
- valeur texte ligne 0 : `printh 23 <len> 56 00 <texte>`
- valeur slider ligne 0 : `printh 23 03 57 00 <valeur_0_255>`

Si `bBack` est pressé à la racine du menu, `HMIModule` considère que le menu
configuration est fermé. Le changement de page visuel vers Home doit alors être
fait localement côté Nextion, par exemple avec `page <nom_de_la_page_home>`.

Le `Page Exit Event` de `pageCfgMenu` doit aussi envoyer `printh 23 02 51 06`.
Cette trame est l'arret explicite du mode menu cote FlowIO; sans elle, FlowIO
peut continuer a considerer le menu actif et tenter de mettre a jour les objets
du menu.

## Limitations actuelles

- Pas de handshake de version d'affichage encore implémenté.
- Pas d'ACK binaire structuré renvoyé vers Nextion.
- `globals.vaStates` et `globals.vaAlarms` sont des contrats de firmware : toute
  modification d'ordre de bits doit être synchronisée entre ESP et Nextion.
