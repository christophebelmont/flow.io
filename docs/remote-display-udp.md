# Flow Connect Display UDP

Le firmware `FlowConnectDisplay` pilote physiquement le Nextion en UART. Le
firmware `FlowIO` conserve la logique piscine, le modèle HMI et le traitement des `HmiEvent`.
L'échange réseau est un transport UDP binaire léger sur `WiFiUDP`, sans HTTP,
WebSocket ni JSON côté `FlowIO`.

## Profils

- `FlowIO` garde le `NextionDriver` local par défaut.
- `FlowIO` peut utiliser le driver distant avec `hmi/nextion_udp/enabled=true`
  ou le define `FLOW_HMI_REMOTE_UDP=1`.
- `FlowConnectDisplay` contient seulement Wi-Fi, logs série, config minimale,
  `NextionDriver` et `FlowConnectDisplayUdpClientModule`.

## Protocole

Les paquets utilisent `HmiUdpHeader` dans
`src/Core/Hmi/HmiUdpProtocol.h`:

- magic `FH`, version `3`
- port UDP `42110`
- taille maximale `192` octets
- CRC16 Modbus sur l'en-tête sans champ CRC puis le payload

Messages principaux:

- `Hello` en broadcast depuis `FlowConnectDisplay`
- `Welcome` depuis `FlowIO`
- `Ping` / `Pong` toutes les 2 secondes
- `HomeText`, `HomeGauge`, `HomeStateBits`, `HomeAlarmBits`
- Les écrans Nextion V1 et V2 utilisent le même transport `HomeGauge` pour les
  jauges pH/ORP. `HomeV2Needles` reste décodable pour compatibilité, mais
  FlowIO ne l'émet plus.
- `ConfigStart`, `ConfigRow`, `ConfigEnd`
- `HmiEvent` depuis `FlowConnectDisplay` vers `FlowIO`
- `RtcReadRequest`, `RtcReadResponse`, `RtcWrite`

`HomeText` transporte jusqu'à `31` caractères utiles plus le zéro terminal.
Le message d'erreur Home est donc volontairement court et publié dans
`globals.vaErrMsg.txt`.

`HmiEvent.text` transporte jusqu'à `47` caractères utiles plus le zéro
terminal. Cela permet d'éditer les champs texte du menu config sans troncature
en mode Flow Connect Display (UDP), avec la même limite pratique qu'en mode
Nextion UART direct.

`FlowConnectDisplay` lit `globals.vaVersion.txt` dans son Nextion local au démarrage.
Cette variable doit contenir une version texte au format `M.m.p` avec 1 à 2
chiffres par segment, par exemple `6.0.0` ou `12.34.56` (8 caractères max).
Quand la lecture réussit, `Hello` transporte cette version et le flag
`HMI_UDP_HELLO_FLAG_NEXTION_VERSION_VALID`. `FlowIO` applique alors la même
validation que pour un Nextion directement attaché, en comparant cette chaîne
à la version TFT attendue par le firmware.

Quand le lien est établi, `FlowConnectDisplay` relit cette version toutes les
60 secondes hors rendu de menu/configuration, puis renvoie un `Hello` unicast à
`FlowIO`. Si la version annoncée change, `FlowIO` force un rafraîchissement Home
complet et `HMIModule` relance la validation de compatibilité.

`HmiEvent` et `HomeStateBits` sont envoyés avec `ACK_REQUIRED`. `FlowIO`
répond `Ack` aux événements; `FlowConnectDisplay` répond `Ack` au bitmap d'état. Les
émetteurs réessaient après environ 150 ms. `FlowIO` ignore un doublon
d'événement déjà traité mais renvoie toujours l'ACK.

## Découverte et lien

Au démarrage, `FlowConnectDisplay` diffuse `Hello` sur `255.255.255.255:42110`.
`FlowIO` mémorise l'adresse source, répond `Welcome`, puis demande un
rafraîchissement complet de l'écran via le chemin HMI existant.

Si aucun paquet n'est vu pendant environ 9 secondes:

- `FlowIO` marque le display offline.
- `FlowConnectDisplay` affiche `Connexion Flow.io perdue` via le Nextion et reprend les
  `Hello` broadcast.
- `FlowConnectDisplay` cache l'objet Nextion `tFConnectState`.

Quand `Welcome.accepted=1` est reçu, `FlowConnectDisplay` affiche `tFConnectState`.

## Token

Le token partagé optionnel côté `FlowIO` est configuré dans `hmi/nextion_udp/token`.
Côté `FlowConnectDisplay`, il est configuré dans `fcd/udp/token`. Il n'est
jamais envoyé en clair: `FlowConnectDisplay` place seulement `tokenCrc` dans
`Hello`. Si le token `FlowIO` est vide, tout Flow Connect Display est accepté;
sinon le CRC doit correspondre.

## RTC

`FlowIO` peut synchroniser l'heure avec le Nextion distant comme avec un
Nextion local. `RemoteHmiUdpDriver::readRtc()` envoie `RtcReadRequest`;
`FlowConnectDisplay` lit `rtc0` à `rtc5` sur le Nextion local puis répond avec
`RtcReadResponse` acquitté. `RtcWrite` reste envoyé de `FlowIO` vers
`FlowConnectDisplay` et appliqué localement sur le Nextion.
