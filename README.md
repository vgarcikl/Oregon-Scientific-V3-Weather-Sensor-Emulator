# **Oregon Scientific V3 Weather Sensor Emulator**
🌐 **English** | [Español](README-ES.md)

**Emulator of weather sensors compatible with the Oregon Scientific V3 protocol. This project was born from the need to replace the original sensors with an alternative solution capable of reproducing their behavior and ensuring compatibility with existing receivers.**

## Table of contents

- [Emulated sensors](#emulated-sensors)
- [Goals](#goals)
- [Features](#features)
- [Hardware](#hardware)
- [Connections](#connections)
- [Reception by the original receivers](#reception-by-the-original-receivers)
- [Emulation configuration](#emulation-configuration)
- [Findings](#findings)
- [Future work](#future-work)
- [Conclusion](#conclusion)
- [License](#license)
- [Credits and acknowledgements](#credits-and-acknowledgements)

## Emulated sensors

- **Temperature and humidity sensors (oregon-thgr810.ino)**
    - Oregon Scientific THGR810
    - Oregon Scientific THGR800
    - Oregon Scientific THGN800
    - Oregon Scientific THGN801
- **Rain gauge (oregon-pcr800.ino):**
    - Oregon Scientific PCR800
- **Anemometer and wind vane (oregon-wgr800.ino):**
    - Oregon Scientific WGR800
- **UV sensor (oregon-uvn800.ino):**
    - Oregon Scientific UVN800

## Goals

- **Robust timing without individual calibration: ensure correct operation on different Arduino boards based on the ATmega328 or similar, minimizing the impact of oscillator frequency variations.**
- **Low power consumption: optimize the emulator's operation to reduce consumption and extend autonomy in battery-powered applications.**
- **High-fidelity emulation: reproduce, as precisely as possible, the same frames as the original sensors.**

## Features

- **Two timing modes:**
    - *Optimized* (`enable_opt`): asynchronous Timer2 with a 32.768 kHz crystal. No calibration required and deep sleep between transmissions (idle consumption of 3 µA).
    - *Standard*: based on `micros()` / `delayMicroseconds()`, compatible with any Arduino.
- **Configurable synchronization modes** for pairing with original receivers.
- **Low battery detection** by reading VCC against the internal reference.
- Configurable transmission interval, channel and identifier.
- Software-controlled power for the RF transmitter: it is only energized during transmission.

## Hardware

- Microcontroller:
    - ATmega328PB.
    - ATmega328P.
- RF transmitter:
    - 433 MHz OOK module FS1000A

With this hardware, two configurations are possible:

- **Optimized mode** (`enable_opt`)

    **This mode requires an ATmega328 or similar**, since it depends on the sleep library, the way Timer2 is configured in asynchronous mode and other parameters.

    To use this mode you need the following components:

    - **32.768 kHz crystal:** it must be soldered to pins "XTAL1 and XTAL2".
    - **A pair of capacitors:** typically 12 pF (each one connected from XTAL1 and XTAL2 to GND).

    The diagram below shows the wiring:

    ```
                      ┌────────┤ ├────────┐      ← 32.768 kHz crystal
                      │                   │
        XTAL1 ────────┤                   ├──────── XTAL2
                      │                   │
                     ─┴─ C1              ─┴─ C2
                     ─┬─                 ─┬─
                      │                   │
                     GND                 GND
    ```

    **Important:** these pins are normally used for an external 16 MHz/8 MHz oscillator, so when using the 32 kHz crystal, **the Arduino's internal oscillator must be used as the main clock**. Configure the fuses or bootloader so that no external crystal is used for the system clock.

- **Standard mode**

    This mode requires no additional hardware and is compatible with any Arduino, but the `V3_PULSE_LENGHT_US` parameter must be calibrated so that the duration of the Manchester pulses matches what the original receiver actually accepts. More information in the configuration section.

## Connections

| Pin | Constant | Function |
| --- | --- | --- |
| 8 | `RF433_TX_PIN` | Data to the 433 MHz RF transmitter |
| 7 | `RF433_VCC` | Transmitter power supply (active only during transmission) |
| 5 | `LED_PIN` | Transmission and error indicator |

## Reception by the original receivers

This section covers the basic concepts and main variables involved in how Oregon Scientific V3 sensors are received. It gives an overview of how the signal is picked up and of the parameters needed to establish communication, without going into details such as data encoding, checksum calculation or the internal structure of the frames.

This program is designed so that **any Oregon V3 receiver station can receive the sensors.** The ones I have used to test the program are:

- **Oregon Scientific WMR86**
- **Oregon Scientific WMR88**
- **Oregon Scientific WMR180**

### Key concepts

Within the V3 protocol, the following key concepts must be kept in mind:

- **Pulse duration (`V3_PULSE_LENGHT_US`):** the time a signal stays in a given logic state (`HIGH` or `LOW`), measured in microseconds (µs). Receivers require an average time between: `487.7 - 489.0 µs`.
- **Transmission period (`V3_TX_INTERVAL_SECS`):** determines how often the sensor sends data (in seconds). It varies depending on the sensor type and the configured channel.
- **Sensor identity (`sensor_id`)**: indicates the sensor type (UVN800, WGR800, THGN810).
- **Sensor channel (`V3_CHANNEL`):** indicates the channel the sensor is transmitting on. It is used on temperature sensors.
- **Sensor session code (`rcode`)**: also called "**Rolling Code**", it is used to identify a specific sensor among others of the same type/channel.
- **Pairing mode (`pair_mode`):** responsible for signaling whether a sensor has just been powered on or restarted.

### Receiver scan types

The original receivers have different mechanisms for searching for and recognizing sensors. The behavior depends on the state of the receiver and on whether a sensor is already linked. In this implementation, two main situations are distinguished:

- **Initial scan:** happens when the receiver is initialized.
    - The receiver checks the sensor identifier and the transmission channel.
    - If it recognizes a compatible sensor, it stores its session code (*rolling code*) and associates it with the identifier and channel combination.
    - Once this association is established, sensors with the same identifier and channel but a different session code are ignored.
- **Search mode:** activated when the user selects the sensor search function on the receiver station. In this mode, the behavior depends on whether a previous link exists:
    - **If no sensor is linked:**
        - The receiver looks for compatible sensors and, when it detects a valid one, performs the same identification and linking procedure as during the initial scan.
    - **If a sensor is already linked:**
        - The receiver checks whether it has lost communication with the associated sensor. In this case, communication is considered lost when roughly five minutes pass without receiving data.
        - If communication has been lost, the receiver checks whether the newly detected sensor has pairing mode enabled.
            - **If pairing mode is enabled**, the receiver may accept the new session code and associate it with the corresponding identifier and channel, restoring the link.
            - **If pairing mode is disabled**, the receiver ignores the data from the sensor with the new session code and keeps waiting for the previously linked sensor.

### Reception problems

When a receiver does not pick up the messages sent by a sensor, apart from factors such as distance and message corruption, the cause may be:

- **Pulse duration:** if the expected sensor is transmitting and the receiver has just been powered on or has performed a Search and does not pick it up, one possible problem is the duration of the `HIGH` or `LOW` pulse. In original sensors, this is usually due to degradation of the 32 kHz crystal they use. In emulated sensors, it can happen in the following cases:
    - The Arduino's internal oscillator is being used to generate the pulses (this is the case when the original external crystal has been removed and `enable_opt` mode is not being used).
    - An external oscillator is being used to generate the pulses and `V3_PULSE_LENGHT_US` has not been adjusted.
    - `enable_opt` is being used but the 32 kHz crystal is badly out of calibration or damaged.
- **Transmission period:** if the receiver picks up the sensor's messages during the initial scan or Search mode but loses the signal afterwards, it is usually because `V3_TX_INTERVAL_SECS` is not appropriate for the sensor type / channel.

## Emulation configuration

This section details how to configure the program to get the expected functionality.

### Program mode (`mod_program`)

Defines the pairing and *rolling code* behavior. The following table shows the function of each mode:

| **Value** | **Pairing mode** | **Rolling code** | **Description** |
| --- | --- | --- | --- |
| `0` | 30 min after startup | Random | Faithful emulation of the original sensor. After a restart, the receiver requires a new *Search* within the first 30 min. |
| `1` | 30 min after startup | Fixed | **Recommended.** After a restart or power outage, the receiver detects the sensor again without intervention. |
| `2` | Always active | Fixed | Any receiver can register it at any time. With several sensors of the same type and channel, the receiver may synchronize with another one. |
| `3` | Disabled | Fixed | If the *rolling code* changes, the receiver must be reset; a *Search* is not enough. |
| `4` | Disabled | Random | Not recommended. |

In modes with a fixed *rolling code* (`1`, `2` and `3`), change `rcode` when there are several devices with the same `sensor_id` and channel, to avoid collisions.

### Power optimization (`enable_opt`)

The program has two operating modes, selected with the `enable_opt` directive. This option determines how the Manchester pulses are generated and how the microcontroller's power consumption is managed.

- **Optimized mode (`#define enable_opt`):** enables the use of the 32.768 kHz crystal for timing the Manchester pulses through Timer2 and allows sleep functions to be used to reduce the microcontroller's power consumption.
    - It should only be enabled if the hardware has the 32.768 kHz crystal and the configuration needed to use it.
    - It requires the [Low-Power-Canique](https://github.com/canique/Low-Power-Canique) library for low power, specifically the `master` branch, which adds support for the ATmega328PB.
    - This mode makes it possible to dispense with `micros()` and `delayMicroseconds()` for pulse timing, using instead the timer configured with the 32.768 kHz crystal.
- **Standard mode (`// #define enable_opt`):** when the option is disabled, the program uses `micros()` and `delayMicroseconds()` to control the duration of the Manchester pulses. In this configuration, the sleep mechanism associated with the optimized mode is not used.
    - **`V3_PULSE_LENGHT_US`:** defines the reference duration of each half of the Manchester bit. Its value must be adjusted to achieve timing compatible with what the Oregon Scientific V3 receiver stations expect.
    - **Timing adjustment:** it is recommended to change this parameter in steps of ±0.2 µs and then check whether the receiver station correctly recognizes the transmitted messages. If the sensor is not picked up, or is picked up only sometimes, keep adjusting this value.
    - There is no universal value that guarantees compatibility with all receivers and configurations. The result depends on:
        - The calibration of the external oscillator you use.
        - The receiver's tolerance, which is approximately ±1.3 µs.
    - In the future, I will release an OregonV3 receiver program for Arduino that will estimate the approximate **`V3_PULSE_LENGHT_US`** being emitted by the transmitter.

    > Using internal oscillators to generate the Manchester pulses is not recommended: small variations in voltage or temperature can push the pulse durations out of range.

In both modes, the goal is to keep timing precise enough for the receiver stations to correctly decode the transmitted data. The main difference is that the optimized mode uses the timer tied to the 32.768 kHz crystal and reduces power consumption, while the standard mode relies on Arduino's usual timing functions.

### Identity and transmission

| Parameter | Description |
| --- | --- |
| `sensor_id` | Device identifier |
| `V3_CHANNEL` | Channel (0–15); each sensor type comes with a specific channel. |
| `rcode` | Fixed *rolling code* *(explained in Program mode)* |
| `V3_TX_INTERVAL_SECS` | Seconds between transmissions (depends on the sensor type and channel) |
| `V3_PAYLOAD_NIBBLES` | Payload nibbles included in the checksums |
| `V3_TX_BYTES` | Total frame length in bytes |

### Low battery detection

| Parameter | Default value | Description |
| --- | --- | --- |
| `check_voltaje_batery` | `1` | `1` enables the check; `0` disables it (battery is always reported as OK) |
| `vol_low` | `2.7` | Threshold (V) below which low battery is reported |
| `vol_calibrate` | `0` | Offset (V) to adjust the VCC reading to the real voltage |

### THGR810 sensor-specific parameters

The temperature sensor has the following extra variables:

- **Transmission interval configuration (`V3_Default_INTERVALS_SECS`):**
    - The program allows choosing between two ways of configuring the sensors' transmission interval through the `V3_Default_INTERVALS_SECS` variable. This parameter determines whether the default transmission intervals of the original Oregon Scientific sensors are used or whether the time between transmissions is set manually.
        - **Default intervals (`V3_Default_INTERVALS_SECS = 0`):** the program emulates the transmission intervals of the original sensors. These vary depending on the configured channel, so not all sensors transmit with the same periodicity.
        - **Manual interval (`V3_Default_INTERVALS_SECS = 1`):** allows setting a custom transmission interval through the `V3_TX_INTERVAL_SECS` variable.
- **Temperature and humidity trend calculation (`calculate_trends`)**
    - The program includes an algorithm to calculate temperature and humidity trends, in order to emulate the trend indicators *(arrows)* shown on Oregon Scientific V3 receiver weather stations. These indicators show whether temperature or humidity are stable, rising or falling compared with previous measurements. The parameters to modify are the following:
        - **Calculation enable (`calculate_trends`):** enables or disables the trend algorithm.
            - `0`: disabled.
            - `1`: enabled.
        - Threshold configuration:
            - The algorithm lets you adjust the sensitivity with which temperature and humidity variations are detected through the following parameters:

                | Variable | Default value | Function |
                | --- | --- | --- |
                | `temp_th_estable` | `0.05` | Threshold associated with temperature stability. |
                | `temp_th_up` | `0.08` | Threshold for detecting a rising temperature trend. |
                | `temp_th_down` | `-0.08` | Threshold for detecting a falling temperature trend. |
                | `hum_th_estable` | `0.15` | Threshold associated with humidity stability. |
                | `hum_th_up` | `0.3` | Threshold for detecting a rising humidity trend. |
                | `hum_th_down` | `-0.3` | Threshold for detecting a falling humidity trend. |

            These values let you adapt the algorithm's behavior to the variations in the measurements. In general, changing the thresholds changes the sensitivity with which trends are recognized, although their exact effect depends on how they are used in the calculation functions.

## Findings

During the research phase of this project, the analysis of numerous frames transmitted by original Oregon Scientific sensors made it possible to identify several behaviors related to how the receiver stations interpret the data. Some of these findings, partially described in earlier sections, emerged from experimental tests carried out during the development of the emulator.

It has not been possible to determine whether all of these behaviors had already been documented. Therefore, they are presented as results obtained during this research, distinguishing between experimental observations and the conclusions derived from them.

### Multiple uses of the low-battery nibble

While analyzing transmissions from different original sensors, it was observed that the nibble initially associated with the battery status could take different values depending on the time elapsed since the sensor was restarted.

In the first observations, the nibble showed the value `0x8` immediately after restarting the device. However, roughly 30 minutes later, it changed to `0x0`, while the battery indicator remained in the correct state. This behavior was repeated in all the sensors analyzed.

To check whether these values were also related to the battery status, tests were carried out lowering the supply voltage below 2.7 V in order to trigger the low battery indication. Under these conditions, the value `0xC` was observed during the first 30 minutes, later changing to `0x4`.

The repetition of this pattern made it possible to establish a correspondence between the two most significant bits of this field and two independent states: the battery level and the synchronization state. Specifically, the values `0x8` and `0xC` appeared associated with synchronization enabled, while `0x0` and `0x4` corresponded to synchronization disabled.

This hypothesis was reinforced by tests with a modification of the `oregon-uvn800.ino` program, developed by Lars Wessels. During these tests, it was observed that the original receiver stations did not allow switching directly from a UVN800 sensor with a given *Rolling Code* to another using a different code without first restarting the station.

From this behavior, it was proposed that the value `0x8` could be related to the sensor's synchronization, recognition or linking process. Later tests with the emulator supported this interpretation: when starting a new emulation with a different *Rolling Code*, it was possible to wait for the receiver to stop receiving the previously linked sensor and then start a scan using *Search* mode.

The experimental results made it possible to establish the following correspondence between the nibble value, the battery status and synchronization:

| Hexadecimal value | Battery status | Synchronization |
| --- | --- | --- |
| `0x0` | Battery OK | Disabled |
| `0x4` | Battery low | Disabled |
| `0x8` | Battery OK | Enabled |
| `0xC` | Battery low | Enabled |

Therefore, this field should not be interpreted solely as a low-battery indicator. Its values also provide information related to the sensor's synchronization, which is relevant for reproducing the behavior of the original devices with an emulator.

### Temperature and humidity trend nibbles

While analyzing the frames of a THGN800 sensor, it was observed that the nibble associated with battery and synchronization status could contain values different from those initially identified. In particular, values such as `0x1` and `0x2` appeared, which did not fit the initial interpretation of the field.

When comparing these transmissions with the information shown on the receiver console, it was found that these values coincided with the presence of the temperature trend arrow. This observation led to investigating whether the field contained additional information about the evolution of the temperature.

Through various tests with the emulator, it was verified that the bits corresponding to the temperature trend also played a part in the interpretation of the field. By testing all possible combinations, the following correspondence was obtained between the transmitted hexadecimal value and the information shown by the receiver:

| Hexadecimal value | Battery status | Synchronization | Temperature trend |
| --- | --- | --- | --- |
| `0x0` | OK | Disabled | Steady |
| `0x1` | OK | Disabled | Rising |
| `0x2` | OK | Disabled | Falling |
| `0x3` | OK | Disabled | No trend arrow |
| `0x4` | Low | Disabled | Steady |
| `0x5` | Low | Disabled | Rising |
| `0x6` | Low | Disabled | Falling |
| `0x7` | Low | Disabled | No trend arrow |
| `0x8` | OK | Enabled | Steady |
| `0x9` | OK | Enabled | Rising |
| `0xA` | OK | Enabled | Falling |
| `0xB` | OK | Enabled | No trend arrow |
| `0xC` | Low | Enabled | Steady |
| `0xD` | Low | Enabled | Rising |
| `0xE` | Low | Enabled | Falling |
| `0xF` | Low | Enabled | No trend arrow |

The table shows that interpreting the field requires considering the battery status, the synchronization and the temperature trend together. In addition, some combinations were identified that are not normally used to represent a specific trend: in those cases, the original console does not display the corresponding arrow.

Next, the humidity trend field was examined. To locate it, the nibbles immediately following the data representing the humidity were analyzed. By comparing the transmitted values with the information shown on the receiver, four possible combinations were identified:

| Hexadecimal value | Humidity trend | Notes |
| --- | --- | --- |
| `0x0` | Steady | Humidity remains stable. |
| `0x1` | Rising | Humidity is increasing. |
| `0x2` | Falling | Humidity is decreasing. |
| `0x3` | No trend arrow | Not indicated |

These results indicate that the messages transmitted by the THGN800 contain information in addition to the temperature and humidity measurements themselves. Some of the nibbles that, at first glance, might look like independent or reserved fields take part in representing the sensor's status and the trends shown by the console.

Identifying these combinations makes it possible to reproduce more accurately the information displayed by the original receivers and to improve the emulator's compatibility. However, the correspondences described are based on the tests carried out and do not necessarily imply that all Oregon Scientific models use an identical encoding.

### Other

While studying the behavior of the PCR800, experimental tests were carried out varying the time interval between two consecutive tips of the rain gauge's tipping mechanism. In particular, intervals shorter than five seconds were analyzed, with the aim of generating high rain intensity values and checking how they correspond to the transmitted data.

One of the messages obtained was the following:

`A2 91 40 87 93 39 32 80 00 04 4F`

In the initial interpretation, the nibbles `2 91 4` correspond to the sensor identifier, followed by the channel (`0`) and the Rolling Code (`87`). Next, the sequence `93 39 3` lies in the region of the message associated, according to the protocol interpretation, with the synchronization and battery status, as well as the `pHourly` value.

However, the interpretation of these last fields raises doubts. The nibble `9`, located in the position attributed to the battery and synchronization indicator, does not match the expected values (`0`, `4`, `8` or `C`). One hypothesis is that this result could be related to the numerous activations of the mechanism during testing, although there is still not enough evidence to confirm this relationship.

On the other hand, the sequence `3 39 3`, initially interpreted as the `pHourly` value, would produce a numerical combination that does not match the rain intensity expected from the tipping intervals used. Moreover, when taking measurements with even shorter intervals, no consistent increase in the transmitted value was observed.

Provisional conclusion: the captures obtained could indicate a relationship between a high tipping rate and the battery/sync nibble being `9`. Therefore, although there is not enough evidence, it could be concluded that this value in this sensor may indicate that `pHourly` is out of range.

## Future work

- [ ]  Obtain the encoding of the WMR100a anemometer (which includes a temperature/humidity sensor).
- [ ]  Improve documentation, code and compatibility.
- [ ]  Create experimental modifications.
- [ ]  Others…

## Conclusion

This project shows that it is possible to emulate Oregon Scientific V3 sensors (THGR810/800, THGN800/801, PCR800, WGR800 and UVN800) with high fidelity using inexpensive hardware: an ATmega328/328P/328PB and a 433 MHz OOK transmitter. The emulator works with the original receiver stations (WMR86, WMR88 and WMR180) and offers two timing modes: an optimized one, with a 32.768 kHz crystal, no calibration and an idle consumption of about 3 µA, and a standard one, compatible with any Arduino but requiring manual calibration of `V3_PULSE_LENGHT_US`.

Beyond the implementation, the research provides findings about the protocol: the battery nibble also encodes the synchronization state, and the trend nibbles determine the temperature and humidity arrows on the console. These results are experimental and are not guaranteed to hold for all models of the brand.

The project remains open to improvements and suggestions: decoding the WMR100a anemometer, extending compatibility, refining the code and documentation, and releasing an Arduino receiver that measures the real `V3_PULSE_LENGHT_US` of a transmitter, which would make calibrating the standard mode easier.

## License

This project is distributed under the MIT license. See the [LICENSE](LICENSE) file for more information.

## Credits and acknowledgements

This project builds on the work of **Lars Wessels** and his repository [oregon-v3-sensor-emulation](https://github.com/lrswss/oregon-v3-sensor-emulation)
(MIT license), which includes emulators for the PCR800, THGN801, UVN800 and WGR800 sensors
using an Arduino Uno and an FS1000A transmitter. Thank you for publishing your code and your research
on the Oregon Scientific V3 protocol.
