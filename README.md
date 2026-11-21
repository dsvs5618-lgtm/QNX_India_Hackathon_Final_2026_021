# QNX_India_Hackathon_Final_2026_021
AquaGuard: Redundant Sensor Validation and Fault-Tolerant Water Treatment Control
# AquaGuard: SIL-3 Fault-Tolerant Water Treatment & Safety Controller

![Target](https://img.shields.io/badge/Target-Raspberry%20Pi%204B%20(ARM%20Cortex--A72)-blue)
![RTOS](https://img.shields.io/badge/RTOS-QNX%20Neutrino%208.0-red)
![Standard](https://img.shields.io/badge/Safety-IEC%2061508%20SIL--3%20%2F%20MISRA--C-green)
![Language](https://img.shields.io/badge/Language-C99%20%2F%20POSIX-orange)
![IPC](https://img.shields.io/badge/IPC-QNX%20Native%20Message%20Passing-purple)

AquaGuard is a hard real-time, fault-tolerant water treatment and hydraulic safety controller built on **QNX Neutrino RTOS 8.0**. Designed according to **IEC 61508 SIL-3** principles, it provides automated contaminant detection, Triple Modular Redundancy (2oo3 TMR) sensor fusion, hydraulic overpressure mitigation, and self-healing fault recovery on multi-core ARM embedded hardware.

---

## Key Features

- **2oo3 Triple Modular Redundancy (TMR):** Continuous pairwise consensus across 3 independent analog TDS probes with median voting.
- **Graceful State Degradation:** Automatically transitions across `NORMAL` (3oo3) &rarr; `DEGRADED-1` (2oo3) &rarr; `DEGRADED-2` (1oo1 fallback) &rarr; `SAFE-STATE` (failsafe divert) upon successive sensor dropouts.
- **Self-Healing Probation Filter:** Reconnected or recalibrated sensors undergo a 6-cycle (300 ms) stability observation before being re-admitted to the voting pool.
- **Hard Core Isolation (Symmetric Multiprocessing):** Dedicated CPU core pinning (`ThreadCtl(_NTO_TCTL_RUNMASK)`) isolates safety-critical control and actuation from disturbances and logging.
- **Flow-Gated Motor Protection:** Both clean and filtration pumps remain strictly clamped to 0.0V (Standby) until physical water inflow is sensed via the YF-S401 turbine or simulated inflow (`'f'`) is triggered, running for strictly 3.0 seconds to prevent dry-running.
- **Independent Safety Watchdog:** A dedicated thread on Core 2 enforces a 500 ms deadman timer via non-blocking pulses, directly clamping valves to failsafe if the supervisor locks up.
- **Zero-Copy Native IPC:** Strict microkernel message passing (`MsgSend`/`MsgReceive`/`MsgReply`) with QNX priority inheritance eliminates priority inversion.

---

## System Architecture

AquaGuard runs as deterministic POSIX real-time threads pinned to dedicated hardware cores of the Raspberry Pi 4B:

```
+-----------------------------------------------------------------------------------------+
|                                    USER SPACE (Ring 3)                                  |
|                                                                                         |
|  +--------------------+     +-----------------------+     +--------------------------+  |
|  |   Sensor Streamer  |     | Validation Supervisor |     |  Valve/Relay Controller  |  |
|  |  (Core 2 / Prio 35)|     |  (Core 1 / Prio 55)   |     |    (Core 0 / Prio 60)    |  |
|  +---------+----------+     +-----------+-----------+     +-------------+------------+  |
|            | MsgSend                    | MsgSend                       |               |
|            | (20 Hz Sensor Data)        | (Actuator Commands)           |               |
|            v                            v                               v               |
|  +-----------------------------------------------------------------------------------+  |
|  |                QNX Microkernel Message Bus (Channels & Connections)               |  |
|  +-----------------------------------------------------------------------------------+  |
|            ^                                    ^                                       |
|            | MsgSend (Fault Injections)         | MsgSendPulse (100ms Heartbeat)        |
|            |                                    v                                       |
|  +---------+----------+              +---------------------+                            |
|  | Disturbance CLI/HUD|              |   Safety Watchdog   |                            |
|  | (Core 3 / Prio 5)  |              | (Core 2 / Prio 45)  |                            |
|  +--------------------+              +---------------------+                            |
|                                         (500ms Deadman Timeout -> Override to Safe)     |
+-----------------------------------------------------------------------------------------+
|                                   KERNEL SPACE (Ring 0)                                 |
|  +-----------------------------------------------------------------------------------+  |
|  |  QNX Neutrino Microkernel (procnto-smp-instr): Scheduler, MMU Memory Protection  |  |
|  +-----------------------------------------------------------------------------------+  |
+-----------------------------------------------------------------------------------------+
```

### Multi-Core Pinning & Scheduling Hierarchy

| Subsystem Thread | Core | Runmask | Priority (`SCHED_FIFO`) | Responsibility |
| :--- | :---: | :---: | :---: | :--- |
| `valve_actuator_worker` | **Core 0** | `0x01` | **60** | Direct TB6612FNG H-bridge GPIO control, LED driving, and emergency valve clamping. |
| `supervisor_worker` | **Core 1** | `0x02` | **55** | 2oo3 TMR voting engine, probation hysteresis, state transitions, and IPC orchestration. |
| `watchdog_worker` | **Core 2** | `0x04` | **45** | 500 ms deadman pulse supervisor; triggers direct emergency safe state on timeout. |
| `sensor_adapter_worker`| **Core 2** | `0x04` | **35** | 20 Hz (50 ms) ADS1115 I2C ADC sampling, HX710B pressure, and YF-S401 flow pulse counting. |
| `fault_injector_worker`| **Core 3** | `0x08` | **5** | Interactive CLI menu, live telemetry HUD rendering, and operator fault commands. |
| `cpu_stress_worker` | **Core 3** | `0x08` | **1** | Background CPU stress hog; proves safety cores remain unaffected under 100% core load. |

---

## Hardware Pinout (Raspberry Pi 4B 40-Pin Header)

| Component | Function | BCM GPIO | Header Pin | Notes |
| :--- | :--- | :---: | :---: | :--- |
| **TB6612FNG Motor Driver** | Clean Pump (PWMA) | GPIO 17 | **Pin 11** | 5V DC Submersible Pump (Clean Tank) |
| **TB6612FNG Motor Driver** | Filter Pump (PWMB) | GPIO 27 | **Pin 13** | 5V DC Submersible Pump (Filtration Chamber) |
| **YF-S401 Flow Turbine** | Inflow Pulse Signal | GPIO 22 | **Pin 15** | Pull-up input, 5.5 pulses per mL |
| **Status LED: Green** | Normal Operation | GPIO 21 | **Pin 40** | Active-High (Clean Water Pumping / Ready) |
| **Status LED: Orange** | Degraded / Outlier | GPIO 20 | **Pin 38** | Active-High (1 Sensor Faulted / Diverting) |
| **Status LED: Red** | Safe State / Alarm | GPIO 16 | **Pin 36** | Active-High (Consensus Loss / Overpressure) |
| **Acoustic Buzzer** | Warning / Alarm | GPIO 26 | **Pin 37** | Active-High (Pulse chirp or 2s solid tone) |
| **ADS1115 ADC (I2C1)** | I2C Data (SDA) | GPIO 2 | **Pin 3** | Connects to 3x Analog TDS Probes (A0, A1, A2) |
| **ADS1115 ADC (I2C1)** | I2C Clock (SCL) | GPIO 3 | **Pin 5** | 100 kHz standard mode |
| **Power & Ground** | Logic & Sensor Rail | 3.3V / 5V | **Pins 1, 2, 6** | System VCC & Common Ground |

---

## Repository Structure

```
aquaguard/
├── momentics_all_in_one_src/     # Unified production source tree for QNX Momentics IDE
│   ├── aquaguard_main.c          # System entry point: thread initialization & core pinning
│   ├── aquaguard_config.h        # Centralized thresholds, timings, and hardware pins
│   ├── aquaguard_ipc.h           # QNX native IPC message types, pulse codes, and structs
│   ├── priority_config.h         # SCHED_FIFO priority constants and core runmasks
│   ├── supervisor_worker.c       # Core 1 safety supervisor and IPC message handler
│   ├── supervisor.h              # Supervisor context, state enums, and prototypes
│   ├── state_machine.c           # Deterministic safety state machine & flow gate logic
│   ├── voting_engine.c           # 2oo3 TMR consensus and median selection algorithm
│   ├── probation_mgr.c           # 6-cycle sensor re-admission hysteresis manager
│   ├── valve_actuator_worker.c   # Core 0 motor driver actuation and fail-safe logic
│   ├── watchdog_worker.c         # Core 2 safety watchdog with TimerTimeout deadman check
│   ├── sensor_adapter_worker.c   # Core 2 ADS1115 I2C acquisition & flow pulse tracking
│   ├── fault_injector_worker.c   # Core 3 CLI telemetry HUD & disturbance injector
│   ├── rpi_hardware.c            # Direct hardware register driver (mmap BCM2711 GPIO)
│   ├── rpi_hardware.h            # Hardware driver header
│   └── unified_workers.h         # Subsystem worker thread prototypes
├── tests/
│   └── test_core_engine.c        # Automated unit test suite (12 SIL-3 verification tests)
└── README.md                     # Project documentation
```

---

## Building and Running

### Prerequisites
- **QNX Software Development Platform (SDP) 8.0**
- **QNX Momentics IDE** (Windows, Linux, or macOS host)
- **Target Hardware:** Raspberry Pi 4B running QNX Neutrino RTOS 8.0 (`aarch64le`) with `qconn` running.

### Method A: Build via QNX Momentics IDE (Recommended)
1. Open **QNX Momentics IDE**.
2. Select **File &rarr; New &rarr; C/C++ Project &rarr; QNX C/C++ Project**.
3. Name the project `finalday` and select the **aarch64le** target architecture.
4. Copy all files from `momentics_all_in_one_src/` into the project's `src/` directory.
5. Right-click the project &rarr; **Build Project**.
6. Right-click &rarr; **Run As &rarr; QNX C/C++ Application** (select your connected Pi target).

### Method B: Cross-Compiling via QNX SDP Command Line
From a terminal with your QNX environment sourced (`qnxsdp-env.sh` or `qnxsdp-env.bat`):

```sh
# Compile for Raspberry Pi 4B (64-bit ARM)
qcc -Vgcc_ntoaarch64le -Wall -Wextra -O2 -I momentics_all_in_one_src \
    momentics_all_in_one_src/*.c -o finalday -lm -lpthread

# Transfer binary to Raspberry Pi
scp finalday root@<pi-ip-address>:/tmp/

# Run on target
ssh root@<pi-ip-address>
cd /tmp
./finalday
```

---

## Interactive Disturbance CLI

While running, AquaGuard displays a real-time telemetry HUD updating every second. You can inject faults and evaluate the system response directly:

```
========================================================================================
       AQUAGUARD SIL-3 WATER TREATMENT & SAFETY CONTROLLER | QNX NEUTRINO RTOS 8.0      
========================================================================================
 [SYSTEM]  State: STATE_NORMAL                     | Mode: NORMAL CLEAN WATER
 [STATUS]  Flow Inflow Gate: STANDBY (Pumps Idle)  | Consensus Purity: 70 ppm (<= 300 ppm)
----------------------------------------------------------------------------------------
 [2oo3 TRIPLE MODULAR REDUNDANCY TDS PROBES (ADS1115 I2C Channels)]
   Probe 0 (Pin A0):  71.2 ppm  |  Status: ACTIVE     |  Health:  98%  |  Probation: 0/6
   Probe 1 (Pin A1):  69.8 ppm  |  Status: ACTIVE     |  Health:  97%  |  Probation: 0/6
   Probe 2 (Pin A2):  70.5 ppm  |  Status: ACTIVE     |  Health:  99%  |  Probation: 0/6
----------------------------------------------------------------------------------------
 [ACTUATOR OUTPUTS (TB6612FNG Motor Driver)]
   Clean Pump  (Pin 11 / PWMA): [   OFF   ] 0.0V  |  Clean Valve:      OPEN 
   Filter Pump (Pin 13 / PWMB): [   OFF   ] 0.0V  |  Filtration Valve: CLOSED
----------------------------------------------------------------------------------------
 [HARD REAL-TIME DETERMINISM METRICS (Core 1 SCHED_FIFO)]
   Avg Latency: 12 us  |  Max WCET: 3917 us  |  Jitter: ±3916 us  |  Safety Envelope: 50,000 us
========================================================================================
 DISTURBANCE MENU: [b] Bias Probe 1 (+150ppm) | [s] Freeze Probe 2 | [k] Kill Probe 0
                  [p] Pressure Spike (>180kPa) | [m] Toggle Dirty   | [f] Trigger Flow (3s)
                  [r] Restore/Heal             | [e] Emergency Halt | [q] Quit
========================================================================================
Enter command: 
```

### CLI Command Reference

| Key | Action | Expected System Behavior |
| :---: | :--- | :--- |
| **`f`** | **Trigger Inflow (3s)** | Simulates 2.45 LPM flow. Clean pump runs for strictly 3.0s, then auto-stops into safe standby. |
| **`m`** | **Toggle Dirty Mode** | Injects 850 ppm dirty influent. Orange LED turns ON, filtration valve opens. **Pump remains OFF until `'f'` is pressed or water is poured**. Once flow starts, filter pump spins for 3.0s, then auto-reverts to Normal Clean Mode. |
| **`p`** | **Pressure Spike (>180 kPa)** | Injects 240 kPa. Directly opens the Filter/Relief Valve (Pin 13), sounds continuous 2s alarm, alerts HUD, and auto-reverts after 3.0s **regardless of flow**. |
| **`b`** | **Bias Probe 1 (+150 ppm)** | Induces electrical drift. 2oo3 engine detects deviation (>60 ppm), isolates Probe 1 to probation, and shifts to `STATE_DEGRADED_1` with zero clean water disruption. |
| **`s`** | **Freeze Probe 2** | Probes 0 & 1 continue updating live; Probe 2 stays frozen. |
| **`k`** | **Kill Probe 0** | Marks Probe 0 as DEAD (0.0 ppm, Health 0%). System isolates it instantly. |
| **`r`** | **Restore / Heal** | Clears faults. Restored probes enter 6-cycle (300 ms) probation before re-admission. |
| **`c`** | **100% CPU Stress Hog** | Launches a thread on Core 3 consuming 100% CPU load. Safety loops on Cores 0, 1, 2 maintain sub-15 µs latency with 0 deadline misses. |
| **`e`** | **Emergency Halt** | Operator manual trip. Clamps all pumps to 0.0V, opens filtration valve, sounds alarm. |
| **`q`** | **Clean Exit** | Clamps all pins to 0.0V, closes QNX channels, and terminates cleanly. |

---

## Real-Time Performance & QNX System Profiler Data

Extensive empirical profiling captured via **QNX System Profiler** (`tracelogger`) over a 35-second continuous capture on physical hardware demonstrates deterministic operation:

- **Loop Latency (Core 1):** Nominal **4 µs to 12 µs**
- **Worst-Case Execution Time (WCET):** **3,917 µs** (3.917 ms) — consumes only **7.8%** of the **50,000 µs (50 ms)** hard real-time safety envelope.
- **Deadline Misses:** **0** across all operational cycles and fault injections.
- **Trace Dropped Buffers:** **0 of 352** buffers dropped by the kernel.
- **Multi-Core Isolation Headroom:** Over **96% idle CPU headroom** maintained on Core 1 (`idle_cpu_1: 33.573 s` of 35 s) while Core 3 handled operator CLI and disturbance loads.

---

## Safety Standards & Design Principles (IEC 61508 SIL-3)

1. **Deterministic Static Allocation:** Zero dynamic heap allocation (`malloc`/`free`) during active control loops prevents memory fragmentation and unpredictable GC pauses.
2. **Defensive Fail-Safe Biasing:** Hardware outputs feature active pull-downs ensuring that power loss or hardware disconnection leaves all pumps in an unpowered (0.0V) state.
3. **Single-Fault Immunity:** The 2oo3 voting architecture guarantees that the failure, drift, or destruction of any single sensor cannot trigger an erroneous clean water contamination or false shutdown.
4. **Independent Channel Integrity:** The safety watchdog operates on an isolated core (Core 2) with direct hardware valve override authority, independent of the decision supervisor.

---

## License

This project is developed for educational, research, and functional safety embedded systems demonstrations on QNX Neutrino RTOS.
