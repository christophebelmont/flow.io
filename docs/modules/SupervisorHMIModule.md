# SupervisorHMIModule (`moduleId: hmi.supervisor`)

## Rôle

Interface HMI locale du firmware Supervisor:
- rendu statut réseau / lien Flow / firmware update sur TFT ST7789
- gestion rétroéclairage automatique avec capteur PIR
- reset WiFi local sur appui long bouton poussoir

## Dépendances

- `loghub`
- `config`
- `wifi`
- `wifiprov`
- `fwupdate`
- `i2ccfg.client`

## Comportement

- écran rafraîchi périodiquement sans blocage
- extinction backlight après timeout d'inactivité PIR (configurable dans `elink/lcd/auto_off_60s`)
- rallumage du backlight sur détection de présence par le capteur PIR
- GPIO de détection de mouvement configurable dans `elink/lcd/motion_gpio`
- backlight forcé ON pendant update firmware
- appui long sur l'entrée `factoryResetPin` (5s par défaut dans le profil Supervisor): reset `wifi.ssid`/`wifi.pass`, notification provisioning, reboot

## Source de vérité et priorité des valeurs (important)

Quand plusieurs valeurs semblent définies à plusieurs endroits, l'ordre réel est:

1. `src/Board/SupervisorBoardRev1.h` (`kSupervisorBoardRev1Display`, `kSupervisorBoardRev1Inputs`, `kSupervisorBoardRev1Update`)  
   C'est la source de vérité hardware pour le profil Supervisor.
2. `src/Profiles/Supervisor/SupervisorProfile.cpp` (`runtimeOptions`)  
   C'est la source de vérité des timings runtime (timeout PIR, durée appui long reset).
3. `ConfigStore` (`elink/lcd/auto_off_60s`, `elink/lcd/motion_gpio`)  
   Ces variables persistent des overrides runtime pour l'extinction auto et le GPIO mouvement.
4. `src/Modules/SupervisorHMIModule/SupervisorHMIModule.cpp` (`makeDriverConfig_`)  
   Le module copie les valeurs board/profile vers `St7789SupervisorDriverConfig` avant de créer le driver.
5. `src/Modules/SupervisorHMIModule/SupervisorHMIModule.cpp` (`kFallback`)  
   Utilisé uniquement si `board.supervisor == nullptr` (mode secours/compatibilité).
6. `src/Modules/SupervisorHMIModule/Drivers/St7789SupervisorDriver.h` (`St7789SupervisorDriverConfig`)  
   Ce sont des valeurs par défaut de structure C++, pas la config effective du profil Supervisor.

## Clarification SDA/SCL vs MOSI/SCLK (LCD)

Le ST7789 est utilisé en SPI.  
Sur certains modules TFT, les broches sont sérigraphiées `SDA/SCL`, mais ici cela correspond à:

- `SDA` du TFT -> `MOSI` (champ `mosiPin`)
- `SCL` du TFT -> `SCLK` (champ `sclkPin`)

Ce n'est pas le bus I2C `SDA/SCL` interlink.

## Où sont les defaults I2C interlink (ce n'est pas le LCD)

Source de vérité unique: `Board`.

- `FlowIO`: `src/Board/FlowIODINBoard.h` -> bus I2C `interlink` (`SDA=5`, `SCL=15`, `400000 Hz`)
- `Supervisor`: `src/Board/SupervisorBoardRev1.h` -> bus I2C `interlink` (`SDA=27`, `SCL=13`, `400000 Hz`)
- `i2ccfg.server` et `i2ccfg.client` lisent ce bus `interlink` au constructeur pour initialiser `cfgData_.sda/scl/freq_hz`
- `sda/scl/freq_hz` ne sont plus des variables de config persistantes de `elink/server` et `elink/client` (pas d'override NVS concurrent)
- si le bus `interlink` n'existe pas dans la board, le module logue une erreur explicite et ne démarre pas le lien

## Valeurs effectives actuelles (profil Supervisor)

Référence: `src/Board/SupervisorBoardRev1.h` + `src/Profiles/Supervisor/SupervisorProfile.cpp`

- TFT: `resX=240`, `resY=320`, `rotation=1`, `colStart=0`, `rowStart=0`
- TFT GPIO: `backlight=14`, `cs=15`, `dc=4`, `rst=5`, `miso=35`, `mosi=18`, `sclk=19`
- TFT rendu: `swapColorBytes=false`, `invertColors=true`, `spiHz=8000000`, `minRenderGapMs=80`
- Entrées Supervisor: `pirPin=36`, `pirDebounceMs=120`, `pirActiveHigh=true`, `factoryResetPin=23`, `factoryResetDebounceMs=40`
- Pilotage update: `flowIoEnablePin=25`, `flowIoBootPin=26`, `nextionRebootPin=12`, `nextionUploadBaud=115200`
- Interlink I2C (defaults `elink/client` injectés depuis Board): `sda=27`, `scl=13`, `freq_hz=400000`
- Timings runtime: `pirTimeoutMs=60000`, `factoryResetHoldMs=5000`

## Où modifier quoi

- Changer le câblage TFT/PIR/bouton/update: `src/Board/SupervisorBoardRev1.h`
- Changer timeout rétroéclairage ou durée appui long: `src/Profiles/Supervisor/SupervisorProfile.cpp`
- Ne pas utiliser `kFallback` ou les defaults de `St7789SupervisorDriverConfig` pour un changement board normal.
