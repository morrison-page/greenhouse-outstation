# Controls Implementation

Two new MQTT Topics which will be used to implement controls:

- `devices/{device_id}/controls`
- `devices/{device_id}/status`

Status is the control status which will be logged into InfluxDB, sent at the same interval as the sensor data.
This will require a new MQTT Topic to be configured in Telegraf's `telegraf.conf`

## Control Types

Double Status Controls:

- Local/Remote | Main Control for local/remote operation

Binary Status Controls:

- Set Lights | On/Off or 1/0
- Set Heating | On/Off or 1/0
- Set Irrigation | On/Off or 1/0
- Set Ventilation | On/Off or 1/0

## Grafana Side of things

Grafana will have a controls dashboard which will log controls out the same as sensor data to be consumed from InfluxDB.

Dashboard Items:

- Single Current Status of Controls
    - Local/Remote
    - Lights
    - Heating
    - Ventilation
    - Ventilation

**Check specification**
- Time Series Graph of all historic control values (May not be required/wont visualise nice as lots of overlapping values 1/0 etc...)
