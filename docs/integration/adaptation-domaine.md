# Adapter le projet à un autre domaine

Cette page décrit les points de modification utilisés aujourd'hui pour recâbler le projet ou réaffecter ses modules à un autre domaine que la piscine, sans réécrire le noyau applicatif.

## Vue d'ensemble

Les changements se répartissent dans cinq zones du dépôt:

| Zone | Rôle | Fichiers typiques |
|---|---|---|
| Build | choix du firmware compilé et des sources incluses | `platformio.ini` |
| Board | brochage physique, UART, I2C, 1-Wire, GPIO | `src/Board/*.h` |
| Domain | rôles métier, capteurs, équipements, defaults | `src/Domain/*` |
| Profile | instances et ordre d'initialisation des modules | `src/Profiles/*` |
| Assembly IO | binding entre rôles métier et ports physiques | `src/Profiles/FlowIO/FlowIOIoLayout.h`, `src/Profiles/FlowIO/FlowIOIoAssembly.cpp` |

## 1. Activer ou désactiver un firmware

Le choix du firmware se fait dans `platformio.ini`.

### Environnements présents

- `env:FlowIO`
- `env:FlowIOWokwi`
- `env:Supervisor`
- `env:SupervisorWokwi`

### Sélection du profil compilé

Macros utilisées:

- `FLOW_PROFILE_FLOWIO`
- `FLOW_PROFILE_SUPERVISOR`

Elles sont transformées dans `src/App/BuildFlags.h` puis résolues dans `src/App/Bootstrap.cpp`.

## 2. Activer ou désactiver des familles de modules

Le câblage complet d'un module passe par trois niveaux.

### a. Inclusion des sources à la compilation

Le premier filtre est `build_src_filter` dans `platformio.ini`.

Exemples actuels:

- l'environnement `FlowIO` exclut `Profiles/Supervisor` et les modules `WebInterface`, `WifiProvisioning`, `FirmwareUpdate`, `SupervisorHMIModule`
- l'environnement `Supervisor` exclut `Profiles/FlowIO`, `IOModule`, `HMIModule`, `PoolLogicModule`, `PoolDeviceModule`, `HAModule`, `MQTTModule`

### b. Présence dans le profil

Chaque profil possède sa structure `ModuleInstances`:

- `src/Profiles/FlowIO/FlowIOProfile.h`
- `src/Profiles/Supervisor/SupervisorProfile.h`

Un module doit exister dans cette structure pour pouvoir être enregistré.

### c. Enregistrement au boot

L'ordre d'activation se fait dans:

- `src/Profiles/FlowIO/FlowIOBootstrap.cpp`
- `src/Profiles/Supervisor/SupervisorBootstrap.cpp`

Les appels `ctx.moduleManager.add(&modules.xxxModule)` définissent l'ordre d'initialisation et de démarrage.

Selon le module, il faut aussi vérifier:

- les appels `registerRuntimeProvider(...)`
- les appels `registerRuntimeUiProvider(...)`
- les synchronisations Home Assistant
- les helpers de post-init du profil

## 3. Modifier le câblage physique

Les broches réelles se changent dans les fichiers `Board`.

### `FlowIO`

Fichier de référence:

- `src/Board/FlowIODINBoard.h`

On y trouve:

- les UART
- le bus I2C principal
- les bus 1-Wire
- les `IoPointSpec` de la carte

Exemple de modification:

- changer un relais de GPIO 32 à GPIO 5: modifier `kFlowIODINv1IoPoints`
- changer le bus I2C principal: modifier `kFlowIODINv1I2c`

### `Supervisor`

Fichier de référence:

- `src/Board/SupervisorBoardRev1.h`

On y trouve:

- le TFT ST7789
- les UART `bridge` et `panel`
- le PIR
- les broches de contrôle du `FlowIO`
- le bus I2C interlink

## 4. Modifier le mapping métier

Le rôle des entrées et sorties n'est pas porté par `Board`, mais par `Domain`.

Fichier actuel du domaine piscine:

- `src/Domain/Pool/PoolDomain.h`

Le domaine définit:

- `DomainIoBinding`: association `BoardSignal -> DomainRole`
- `PoolDevicePreset`: équipements logiques exposés au module `pooldev`
- `DomainSensorPreset`: capteurs exposés au module `io`

Types de modification:

| Besoin | Fichier principal |
|---|---|
| renommer un rôle métier | `src/Domain/*` |
| changer l'affectation d'un relais à un autre rôle | `src/Domain/*` |
| changer les capteurs ou équipements exposés | `src/Domain/*` |
| créer un nouveau domaine | nouveau dossier `src/Domain/<NouveauDomaine>/` puis nouveau profil |

## 5. Modifier le binding IO sans changer le module

Le module IO actuel est configuré par le profil `FlowIO`.

### Catalogue des ports physiques

Fichier:

- `src/Profiles/FlowIO/FlowIOIoLayout.h`

Ce fichier déclare:

- les `PhysicalPortId`
- la table `kBindingPorts`
- les affectations par défaut des rôles analogiques
- les affectations par défaut des entrées digitales
- les affectations par défaut des sorties digitales

### Instanciation concrète des endpoints

Fichier:

- `src/Profiles/FlowIO/FlowIOIoAssembly.cpp`

Ce fichier:

- lit les presets du domaine actif
- construit les `IOAnalogDefinition`
- construit les `IODigitalInputDefinition`
- construit les `IODigitalOutputDefinition`
- appelle `defineAnalogInput`, `defineDigitalInput`, `defineDigitalOutput`

En pratique:

| Besoin | Fichier principal |
|---|---|
| changer le backend d'un capteur | `FlowIOIoLayout.h` |
| changer le port logique par défaut d'un rôle | `FlowIOIoLayout.h` |
| changer les callbacks runtime ou la discovery HA | `FlowIOIoAssembly.cpp` |
| changer l'inventaire réel des endpoints créés | `FlowIOIoAssembly.cpp` et le domaine associé |

## 6. Ajouter ou retirer un module sans réécrire le noyau

La procédure actuelle est la suivante:

1. ajouter ou retirer les sources concernées dans `platformio.ini`
2. ajouter ou retirer l'instance dans `ModuleInstances`
3. ajouter ou retirer `ctx.moduleManager.add(...)` dans le bootstrap du profil
4. ajuster les dépendances indirectes du profil:
   - providers MQTT runtime
   - providers Runtime UI
   - synchronisation Home Assistant
   - post-init du profil

Exemples dans le code actuel:

- `FlowIOBootstrap.cpp` enregistre `mqtt`, `ha`, `io`, `poollogic`, `pooldev`
- `SupervisorBootstrap.cpp` enregistre `wifiprov`, `i2ccfg.client`, `webinterface`, `fwupdate`, `hmi.supervisor`

## 7. Fichiers à modifier selon le type de changement

| Changement | Fichiers |
|---|---|
| choisir un autre firmware | `platformio.ini` |
| changer une pin ou un bus | `src/Board/*.h` |
| changer le rôle d'un relais ou d'une sonde | `src/Domain/*.h` |
| changer les ports IO proposés au runtime | `src/Profiles/FlowIO/FlowIOIoLayout.h` |
| changer la manière dont les endpoints IO sont instanciés | `src/Profiles/FlowIO/FlowIOIoAssembly.cpp` |
| changer la liste des modules d'un produit | `src/Profiles/*Profile.h`, `src/Profiles/*Bootstrap.cpp`, `platformio.ini` |
| créer un nouveau produit | nouveau dossier `src/Profiles/<Nom>/`, éventuel nouveau `Board`, éventuel nouveau `Domain`, nouvel environnement PlatformIO |

## 8. Référence rapide des fichiers de composition actuels

- `src/App/Bootstrap.cpp`
- `src/App/BuildFlags.h`
- `platformio.ini`
- `src/Profiles/FlowIO/FlowIOProfile.cpp`
- `src/Profiles/FlowIO/FlowIOProfile.h`
- `src/Profiles/FlowIO/FlowIOBootstrap.cpp`
- `src/Profiles/FlowIO/FlowIOIoLayout.h`
- `src/Profiles/FlowIO/FlowIOIoAssembly.cpp`
- `src/Profiles/Supervisor/SupervisorProfile.cpp`
- `src/Profiles/Supervisor/SupervisorProfile.h`
- `src/Profiles/Supervisor/SupervisorBootstrap.cpp`
- `src/Board/FlowIODINBoard.h`
- `src/Board/SupervisorBoardRev1.h`
- `src/Domain/Pool/PoolDomain.h`
