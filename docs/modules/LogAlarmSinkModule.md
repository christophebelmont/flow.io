# LogAlarmSinkModule (`moduleId: log.sink.alarm`)

## Rôle

Sink de logs vers le moteur d'alarmes:
- observe les logs entrants (`Warn` et `Error`)
- maintient deux fenêtres temporelles de détection:
  - warning vu récemment
  - error vu récemment
- expose ces conditions via deux alarmes dédiées

Type: module passif.

## Dépendances

- `loghub`
- `alarms`

## Services consommés

- `logsinks` pour enregistrer son sink
- `alarms` pour enregistrer les conditions d'alarme

## Services exposés

Aucun.

## Config / NVS

Aucune variable `ConfigStore`.

## EventBus / DataStore / MQTT

- aucun abonnement EventBus
- aucune écriture DataStore
- aucune publication MQTT directe

Les alarmes émises par `AlarmModule` restent publiées normalement sur `rt/alarms/*`.

## Détails runtime

### Filtrage anti-boucle

Certains tags sont ignorés pour éviter un auto-feedback:
- `AlarmMod`
- `LogAlmSn`

### Fenêtres de maintien

- `WARN_HOLD_MS = 60000` (60 s)
- `ERROR_HOLD_MS = 120000` (120 s)

Quand un log de niveau correspondant est vu, le timestamp est mémorisé.
La condition reste `true` tant que `nowMs - lastSeenMs <= holdMs`.

### Alarmes enregistrées

- `AlarmId::LogWarningSeen`
  - code: `log_warning`
  - label: `Warning log detected`
  - sévérité: `Warning`
- `AlarmId::LogErrorSeen`
  - code: `log_error`
  - label: `Error log detected`
  - sévérité: `Alarm`

Dans les deux cas:
- `latched = false`
- `on_delay_ms = 0`
- `off_delay_ms = 1000`
- `repeat_ms = 10000`
- `activityLogEnabled = false` afin que ces alarmes techniques ne polluent pas
  le journal d'activité utilisateur

## Thread-safety

Le sink est appelé depuis le dispatcher de logs.
Les timestamps internes sont protégés par `portMUX_TYPE` (`portENTER_CRITICAL/portEXIT_CRITICAL`).
