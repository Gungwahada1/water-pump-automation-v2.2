# Local API V1 — Water Pump Automation V2.4

Base URL: `http://192.168.4.1`. API runs on Pump Controller SoftAP only; internet or external router is not required.

## `GET /api/v1/status`

Returns current Pump state and last Tank sample. Sensor values are current only when `sensor_fresh` and `sensor_valid` are true. `sensor_status` is `NO_DATA`, `INVALID`, `STALE`, or `OK`.

## `POST /api/v1/tank`

Required fields remain compatible with V2.2: `level_percent` (0–100 number), `float_full` (boolean), and `sensor_valid` (boolean). V2.4 Tank also sends `distance_cm`, `boot_id`, `seq`, `firmware`, and `config_version`. `boot_id` and `seq` must be sent together. Repeated/decreasing sequence values from the same boot are rejected with HTTP 409.

The response includes the Pump's desired `tank_config` plus a `config_version`. Tank validates and writes this configuration to its own Preferences NVS before applying it. Tank reports the stored version in its next POST as `config_version`; Pump only then writes and applies the paired settings. Old Tank firmware does not acknowledge versions, so a V2.4 save requires both V2.4 firmwares.

## `POST /api/v1/control`

Request: `{"action":"ON"}` or `{"action":"OFF"}`. Dashboard ON requires physical MANUAL, fresh valid Tank data, a level below the configured stop threshold, and no runtime lockout. AUTO and MANUAL stop at `auto_stop_percent`. OFF works in either mode.

## `GET/POST /api/v1/config`

GET returns active settings plus `save_state`, `pump_config_saved`, `tank_config_saved`, `config_version`, and `saved_to_nvs`. POST accepts any subset of the numeric fields below; invalid field types and invalid combined configurations are rejected before staging.

| Field | Range/default |
|---|---|
| `auto_start_percent` | 1–80%; default 20% |
| `auto_stop_percent` | Greater than start and at most 100%; default 100% |
| `max_runtime_minutes` | 1–60; default 25 minutes |
| `tank_timeout_seconds` | 5–60; default 15 seconds; must be at least sample interval + 4 seconds |
| `tank_height_cm` | 30–500; default 120 |
| `distance_empty_cm` | Greater than full distance, at most 1000, within sensor range; default 100 |
| `distance_full_cm` | At least sensor minimum and less than empty distance; default 25 |
| `sensor_min_distance_cm` | At least 20 and below sensor maximum; default 20 |
| `sensor_max_distance_cm` | Above minimum, at most 1000; default 450 |
| `sensor_interval_ms` | 500–10000; default 1000 |
| `filter_count` | 3, 5, 7, or 9; default 5 |

`auto_start_percent` starts AUTO fill at or below its value. `auto_stop_percent` stops AUTO and MANUAL at or above its value. `max_runtime_minutes` limits one continuous run in both modes. Tank height is a geometry reference; level is calibrated from empty/full distances. Min/max distance bound acceptable echo results; interval sets measurement cadence; filter count is the median-filter window. Float cutoff remains compile-time disabled and is not a dashboard setting in V2.4.

### Configuration save state

POST stages the validated candidate and returns HTTP 202. Save requires fresh, valid Tank data. Pump sends the candidate and version in its normal Tank POST response. Tank writes Preferences and applies it, then acknowledges the version in the next heartbeat. Only after that acknowledgment does Pump write its own NVS and apply its runtime settings. Dashboard polls GET until `save_state` becomes `saved` or `failed`.

Possible states: `pending_tank_ack`, `saved`, `failed`, `tank_sync_required`, and `unsaved_defaults` (safe defaults active but not yet persisted on Pump). If Tank is disconnected/invalid before saving starts, POST returns HTTP 503. If Tank does not acknowledge within 30 seconds, configuration is marked failed and not reported as saved. Dashboard waits up to 30 seconds for confirmation.

`POST /api/v1/config/restore-defaults` stages the documented defaults and follows the same two-device acknowledgment process. Dashboard requires confirmation before calling it.

## NVS layout

Pump namespace: `pumpcfg`, schema version 1. Keys store AUTO start/stop, maximum runtime, Tank timeout, tank height, empty/full calibration distance, sensor min/max distance, read interval, and filter count. The initial Pump defaults are not written automatically; NVS is written only after a confirmed dashboard Save/Restore.

Tank namespace: `tankcfg`, schema version 1. Keys store tank height, empty/full calibration distance, sensor min/max distance, read interval, filter count, and the acknowledged configuration version. Tank writes only when a new valid version arrives.

Each device validates the complete stored configuration at boot. Missing, unsupported-schema, partial, or invalid data causes safe compile-time defaults to be used. Firmware marks the schema invalid before updating the individual keys and marks it valid only after all writes succeed, so interrupted writes are rejected at next boot. SSR is initialized OFF before config loading/network startup. Pump never persists pump ON state.

## Other endpoints

- `GET /api/v1/device`: Pump/Tank identity and runtime information.
- `GET /api/v1/logs`: recent RAM-only events.
- `POST /api/v1/device/restart`: queues a Pump restart after forcing SSR OFF.

Float switch messages over Wi-Fi are not an independent overflow interlock. Use an independent hardwired maximum-level protection for permanent installation.
