# Local API V1 — Water Pump Automation V2.3

Base URL: `http://192.168.4.1`. All browser calls use the same origin. The API is available only on the Pump Controller SoftAP; it does not require internet or an external router.

## `GET /api/v1/status`

Returns current Pump Controller state and most recent Tank sample. `sensor_fresh` is calculated from local receive time and configured timeout. Treat `level_percent` as current only when `sensor_fresh` and `sensor_valid` are both true. `sensor_status` is `NO_DATA`, `INVALID`, `STALE`, or `OK`.

Important fields include `mode`, `pump_on`, `level_percent`, `distance_cm`, `sensor_received`, `sensor_valid`, `sensor_fresh`, `sensor_age_ms`, `float_full`, `float_enabled`, `ultrasonic_full`, `pump_runtime_ms`, `runtime_lockout`, `stop_reason`, `uptime_ms`, and `free_heap`.

## `POST /api/v1/tank`

Required JSON fields retained for V2.2 compatibility:

```json
{"level_percent":34.5,"float_full":false,"sensor_valid":true}
```

V2.3 Tank Controller also sends `distance_cm`, `boot_id`, `seq`, and `firmware`. The sequence increases for each new sensor reading, not each retry/heartbeat. `boot_id` and `seq` must be sent together. Repeated or decreasing sequence numbers from the same boot ID are rejected with HTTP 409 so a replayed old reading cannot keep Tank data fresh. A new boot ID resets the sequence comparison. Older clients without identity fields remain accepted.

A successful response contains the accepted reading and a `tank_config` object. The Tank Controller applies that configuration through the same outgoing HTTP connection; it does not need a fixed client IP or inbound web server.

## `POST /api/v1/control`

Request: `{"action":"ON"}` or `{"action":"OFF"}`.

- `ON` is accepted only when the physical selector is MANUAL, Tank data is received/fresh/valid, ultrasonic level is below the configured stop threshold, and runtime lockout is clear. The controller loop checks these requirements again before switching the SSR. If the request cannot be applied, the API returns HTTP 409 with a `reason`.
- In both AUTO and MANUAL, ultrasonic stops the pump at the configured `auto_stop_percent` threshold. Its default is 100%. `auto_stop_percent` must be greater than `auto_start_percent`.
- A dashboard ON request is stopped if its Tank data becomes stale or invalid while it is running.
- `OFF` is accepted in either mode and receives priority over a queued dashboard ON. Physical buttons remain independent of HTTP processing.

Possible rejection reasons include `physical_selector_must_be_manual`, `tank_data_not_received`, `tank_data_stale`, `ultrasonic_sensor_invalid`, `tank_full_ultrasonic`, `tank_full_float`, `runtime_lockout_active`, and `controller_ack_timeout`.

## `GET/POST /api/v1/config`

GET returns current settings and `saved_to_nvs:false`. POST accepts any subset of these fields:

| Field | Allowed values |
|---|---|
| `auto_start_percent` | 1–80 |
| `auto_stop_percent` | Greater than `auto_start_percent`, up to 100 |
| `max_runtime_minutes` | 1–60 (default 25) |
| `tank_timeout_seconds` | 5–60 |
| `tank_height_cm` | 30–500 |
| `distance_empty_cm` | Must exceed full distance and fit sensor range |
| `distance_full_cm` | At least sensor minimum distance |
| `sensor_min_distance_cm` | At least 20 and below sensor maximum |
| `sensor_max_distance_cm` | Above sensor minimum, at most 1000 |
| `sensor_interval_ms` | 500–10000 |
| `filter_count` | 3, 5, 7, or 9 |

The calibration is rejected unless empty distance is greater than full distance, full distance is at least the sensor minimum, and calibration distances fit within the configured sensor range. The minimum sensor distance cannot be set below 20 cm. Tank timeout must be at least the sensor interval plus 4 seconds to allow for sensor acquisition and HTTP delays. The AUTO stop threshold must be greater than the AUTO start threshold. Values are volatile RAM settings and return to firmware defaults after reboot. They are not written to NVS in V2.3. Network credentials remain compile-time settings; the API does not change the AP while clients are connected.

Configuration meanings: `auto_start_percent` starts AUTO filling at or below the selected level; `auto_stop_percent` stops the pump in both AUTO and MANUAL at or above the selected level. `max_runtime_minutes` caps one continuous pump run in both modes (default 25 minutes). `tank_timeout_seconds` defines when Tank communication is stale. `tank_height_cm` is a geometry reference and calibration sanity check; the current level percentage is calculated from `distance_empty_cm` (0%) and `distance_full_cm` (100%). `sensor_min_distance_cm` and `sensor_max_distance_cm` bound accepted echo readings. `sensor_interval_ms` controls Tank sampling cadence. `filter_count` is the median filter window; larger windows reduce spikes but slow response. Float cutoff is currently disabled in V2.3.

## `GET /api/v1/device`

Returns Pump firmware, uptime, free heap, AP SSID/IP/client count, and last-known Tank firmware/boot ID/sequence. Tank information is marked unknown for older compatible Tank firmware that does not send identity fields.

## `GET /api/v1/logs`

Returns up to 24 recent events from RAM with `uptime_ms` and `message`. Events are lost on reboot; no wall-clock time is available without a time source.

## `POST /api/v1/device/restart`

Queues a restart request. The Pump Controller control loop forces the SSR output OFF before restarting. The dashboard asks the user to confirm first. This is not a substitute for an independent physical emergency stop or overflow interlock.
