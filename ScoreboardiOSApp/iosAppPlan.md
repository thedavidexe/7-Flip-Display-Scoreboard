# Scoreboard Controller App - Technical Plan

## Executive Summary

This document outlines the technical specification for a native iOS application that controls an ESP32-based electromechanical 7-segment scoreboard via Bluetooth Low Energy (BLE). The app manages score state, countdown timers, and configuration while the scoreboard handles display updates and timer countdown logic independently.

---

## 1. System Architecture

### 1.1 High-Level Overview

```
┌─────────────────┐         BLE          ┌─────────────────────┐
│    iOS App      │◄────────────────────►│   ESP32 Scoreboard  │
│    (Swift)      │     Indications      │                     │
│                 │     (with ACK)       │  ┌───┬───┬───┬───┐  │
│  - Score State  │                      │  │ 0 │ 0 │ 0 │ 0 │  │
│  - Timer Config │                      │  └───┴───┴───┴───┘  │
│  - Action Log   │                      │   4x 7-Segment      │
└─────────────────┘                      └─────────────────────┘
```

### 1.2 Responsibility Split

| Component | Responsibilities |
|-----------|-----------------|
| **Mobile App** | Score state management, user interface, BLE communication, action logging, settings storage |
| **Scoreboard** | Display rendering, timer countdown execution, BLE advertising, ID display on startup |

---

## 2. BLE Communication Protocol

### 2.1 Service & Characteristic Definition

| Attribute | Value |
|-----------|-------|
| **Service UUID** | Custom 128-bit UUID (e.g., `12345678-1234-5678-1234-56789abcdef0`) |
| **Characteristic UUID** | Custom 128-bit UUID (e.g., `12345678-1234-5678-1234-56789abcdef1`) |
| **Characteristic Properties** | Write, Indicate |
| **Security** | Bonded pairing required |

### 2.2 Data Packet Format

All communication uses a 5-byte packet:

| Byte | Field | Type | Range | Description |
|------|-------|------|-------|-------------|
| 0 | `blueScore` | uint8 | 0-99 | Blue team score (values >99 wrap: 105 → 05) |
| 1 | `redScore` | uint8 | 0-99 | Red team score (values >99 wrap) |
| 2 | `timerMinutes` | uint8 | 0-99 | Timer minutes (capped at 99) |
| 3 | `timerSeconds` | uint8 | 0-59 | Timer seconds |
| 4 | `flags` | uint8 | bitfield | Configuration flags |

#### Flags Byte Definition

| Bit | Name | Description |
|-----|------|-------------|
| 0 | `TIMER_UPDATE_SLOW` | 1 = Update display every 10s, 0 = Update every 1s |
| 1-7 | Reserved | Future use (set to 0) |

### 2.3 Scoreboard Behavior Logic

```
ON RECEIVE packet:
    IF timerMinutes > 0 OR timerSeconds > 0:
        Enter TIMER mode
        Display MM:SS format
        Begin countdown from received value
        Apply TIMER_UPDATE_SLOW flag for display refresh rate
    ELSE:
        Enter SCORE mode
        Display blueScore on left two digits
        Display redScore on right two digits
```

### 2.4 Communication Flow

1. **App writes** packet to characteristic
2. **Scoreboard sends** indication (BLE confirmation required)
3. **App receives** ACK confirming delivery
4. On ACK failure, app retries (max 3 attempts with exponential backoff)

### 2.5 Connection Lifecycle

```
STARTUP (Scoreboard):
    Clear any existing bond
    Display 4-character hardware ID on segments
    Begin BLE advertising with app-specific service UUID

PAIRING (App):
    Scan for devices advertising service UUID
    User selects scoreboard matching displayed ID
    Initiate bonded pairing
    On success: Send initial packet [00, 00, 00, 00, 0x00]

RUNTIME:
    Maintain persistent connection
    App sends full state on every change

DISCONNECT (Scoreboard):
    Continue current operation (timer keeps counting, score stays displayed)
    Resume advertising for reconnection

RECONNECT (App):
    Auto-reconnect using stored bond
    Resend current state immediately
```

---

## 3. Scoreboard Hardware ID System

### 3.1 Character Set for 7-Segment Display

Characters that render unambiguously on 7-segment displays:

| Type | Characters |
|------|------------|
| **Digits** | `0, 1, 2, 3, 4, 5, 6, 7, 8, 9` |
| **Letters** | `A, b, C, d, E, F, H, J, L, n, o, P, r, t, U, y` |

**Total: 26 characters**

### 3.2 ID Generation Algorithm

```python
CHARSET = "0123456789AbCdEFHJLnoPrtUy"  # 26 chars

def generate_display_id(esp32_mac: bytes) -> str:
    """Generate 4-character ID from ESP32 hardware MAC address."""
    # Use last 3 bytes of MAC for uniqueness
    hash_value = (esp32_mac[-3] << 16) | (esp32_mac[-2] << 8) | esp32_mac[-1]
    
    id_chars = []
    for _ in range(4):
        id_chars.append(CHARSET[hash_value % 26])
        hash_value //= 26
    
    return ''.join(id_chars)  # e.g., "bF3H"
```

### 3.3 ID Display Behavior

- Shown on all 4 digits at scoreboard power-on
- Remains displayed until first successful BLE connection
- After pairing, scoreboard immediately shows `00-00`

---

## 4. Mobile App Specification

### 4.1 Technology Stack

| Layer | Technology | Rationale |
|-------|------------|-----------|
| **Language** | Swift 5.9+ | Modern, safe, native iOS development |
| **UI Framework** | SwiftUI | Declarative UI, excellent state management, iOS-native |
| **BLE Library** | CoreBluetooth | Apple's native BLE framework, full bonding/indication support |
| **State Management** | `@Observable` / `@ObservableObject` | SwiftUI's built-in reactive state |
| **Local Storage** | `UserDefaults` + `@AppStorage` | Simple persistence for settings and state |
| **Min iOS Version** | iOS 16+ | Modern SwiftUI features, BLE 5.0 support |
| **Xcode Version** | 15+ | Latest Swift and SwiftUI capabilities |

### 4.2 Project Structure

```
ScoreboardController/
├── ScoreboardControllerApp.swift      # App entry point
├── ContentView.swift                   # Root navigation
│
├── Models/
│   ├── ScoreboardState.swift          # Score, timer, connection state
│   ├── ScoreboardDevice.swift         # BLE device representation
│   ├── ActionLogEntry.swift           # Log entry model
│   └── AppSettings.swift              # User settings model
│
├── Services/
│   ├── BLEManager.swift               # CoreBluetooth scanning, connection, communication
│   ├── PacketBuilder.swift            # Construct 5-byte packets
│   └── StorageService.swift           # UserDefaults persistence
│
├── ViewModels/
│   ├── ScoreboardViewModel.swift      # Central state management (@Observable)
│   ├── ConnectionViewModel.swift      # Connection state
│   └── SettingsViewModel.swift        # App settings
│
├── Views/
│   ├── ScoreboardSelectionView.swift
│   ├── ScoreControlView.swift
│   ├── TimerView.swift
│   └── SettingsView.swift
│
├── Components/
│   ├── ScoreDisplayView.swift         # Large score display component
│   ├── ScoreButtonView.swift          # Increment/decrement button
│   ├── TimerPresetButton.swift        # Quick timer selection
│   ├── DeviceListRow.swift            # Scoreboard in scan list
│   └── ActionLogList.swift            # Scrollable action history
│
├── Utilities/
│   ├── Constants.swift                # UUIDs, timeouts, etc.
│   └── ScoreUtils.swift               # Score wrapping logic
│
└── Info.plist                         # BLE usage descriptions
```

---

## 5. Screen Specifications

### 5.1 Scoreboard Selection Screen

**Purpose**: Scan for and connect to a scoreboard.

#### UI Layout

```
┌─────────────────────────────────┐
│  ◄ Back          Select Board   │
├─────────────────────────────────┤
│                                 │
│  Scanning for scoreboards...    │
│  ◉ ◉ ◉ (animated)              │
│                                 │
├─────────────────────────────────┤
│  ┌─────────────────────────────┐│
│  │ 📶 Scoreboard bF3H          ││
│  │    Signal: Strong           ││
│  └─────────────────────────────┘│
│  ┌─────────────────────────────┐│
│  │ 📶 Scoreboard 4nJP          ││
│  │    Signal: Weak             ││
│  └─────────────────────────────┘│
│                                 │
│         [Refresh Scan]          │
│                                 │
└─────────────────────────────────┘
```

#### Behavior

1. On screen load, begin BLE scan filtering by service UUID
2. Display discovered devices with their 4-character ID and signal strength
3. User taps device to initiate bonded pairing
4. Show pairing progress indicator
5. On success: send `[00, 00, 00, 00, 0x00]`, navigate to Score Control Screen
6. On failure: show error, allow retry

#### State Transitions

```
IDLE → SCANNING → DEVICE_FOUND → PAIRING → CONNECTED
                       ↓
                  PAIRING_FAILED → IDLE
```

---

### 5.2 Score Control Screen (Main Screen)

**Purpose**: Display and modify team scores.

#### UI Layout

```
┌─────────────────────────────────┐
│  ☰ Menu                    ⚙️   │
├─────────────────────────────────┤
│      Connected: bF3H  🟢        │
├────────────────┬────────────────┤
│                │                │
│     BLUE       │      RED       │
│                │                │
│   ┌───────┐    │    ┌───────┐   │
│   │       │    │    │       │   │
│   │  12   │    │    │  09   │   │
│   │       │    │    │       │   │
│   └───────┘    │    └───────┘   │
│   (tappable)   │   (tappable)   │
│                │                │
│   ┌───────┐    │    ┌───────┐   │
│   │  +3   │    │    │  +3   │   │
│   │ LARGE │    │    │ LARGE │   │
│   └───────┘    │    └───────┘   │
│                │                │
│   ┌───────┐    │    ┌───────┐   │
│   │  -1   │    │    │  -1   │   │
│   │ small │    │    │ small │   │
│   └───────┘    │    └───────┘   │
│                │                │
├────────────────┴────────────────┤
│         [🔄 Reset Scores]       │
│         [⏱️ Set Timer]          │
└─────────────────────────────────┘
```

#### Components

| Element | Behavior |
|---------|----------|
| **Score Display (tappable)** | Tap opens numeric keyboard overlay for direct input (0-99) |
| **Large + Button** | Increment by configured amount (default +1, configurable in settings) |
| **Small - Button** | Decrement by 1 (minimum 0) |
| **Reset Scores** | Set both scores to 00, send packet, log action |
| **Set Timer** | Navigate to Timer Screen |
| **Connection Indicator** | Green = connected, Yellow = reconnecting, Red = disconnected |

#### Score Wrapping Logic

```swift
func wrapScore(_ value: Int) -> UInt8 {
    if value < 0 { return 0 }
    return UInt8(value % 100)  // 105 → 05, 200 → 00
}
```

---

### 5.3 Timer Screen

**Purpose**: Configure and start countdown timer.

#### UI Layout

```
┌─────────────────────────────────┐
│  ◄ Back              Timer      │
├─────────────────────────────────┤
│                                 │
│        ┌─────────────┐          │
│        │             │          │
│        │    05:00    │          │
│        │  (tappable) │          │
│        └─────────────┘          │
│                                 │
├─────────────────────────────────┤
│  ┌─────┐  ┌─────┐  ┌─────┐     │
│  │ 10  │  │  5  │  │  1  │     │
│  │ min │  │ min │  │ min │     │
│  └─────┘  └─────┘  └─────┘     │
│                                 │
├─────────────────────────────────┤
│                                 │
│        [▶ Start Timer]          │
│                                 │
│        [↩ Back to Score]        │
│                                 │
└─────────────────────────────────┘
```

#### Behavior

| Action | Result |
|--------|--------|
| **Preset buttons** | Set timer display to 10:00, 05:00, or 01:00 |
| **Tap timer display** | Open MM:SS input (two fields, numeric keyboard) |
| **Start Timer** | Send packet with timer values, scoreboard enters timer mode and counts down |
| **Back to Score** | Send packet with timer = 00:00 (returns scoreboard to score mode) |

#### Input Validation

```swift
func validateTimer(minutes: Int, seconds: Int) -> (minutes: UInt8, seconds: UInt8) {
    // Cap at 99:59
    let validMinutes = UInt8(min(minutes, 99))
    let validSeconds = UInt8(min(seconds, 59))
    return (validMinutes, validSeconds)
}
```

---

### 5.4 Settings Screen

**Purpose**: Configure app behavior and view action history.

#### UI Layout

```
┌─────────────────────────────────┐
│  ◄ Back            Settings     │
├─────────────────────────────────┤
│                                 │
│  SCORE INCREMENT                │
│  ┌─────────────────────────────┐│
│  │ +1  +2  +3  +6  +7         ││
│  │  ○   ○   ●   ○   ○         ││
│  └─────────────────────────────┘│
│                                 │
│  Sport presets:                 │
│  [Basketball +2] [Football +6]  │
│  [Hockey +1] [Rugby +5/7]       │
│                                 │
├─────────────────────────────────┤
│                                 │
│  POWER SAVING                   │
│  ┌─────────────────────────────┐│
│  │ Slow timer updates (10s)    ││
│  │                      [OFF]  ││
│  └─────────────────────────────┘│
│                                 │
├─────────────────────────────────┤
│                                 │
│  CONNECTION                     │
│  ┌─────────────────────────────┐│
│  │ Connected to: bF3H          ││
│  │        [Disconnect]         ││
│  └─────────────────────────────┘│
│                                 │
├─────────────────────────────────┤
│                                 │
│  ACTION LOG                     │
│  ┌─────────────────────────────┐│
│  │ 14:32:05 Blue +3 (now 12)   ││
│  │ 14:31:47 Red +3 (now 09)    ││
│  │ 14:30:22 Timer 05:00        ││
│  │ 14:28:15 Connected bF3H     ││
│  │ 14:28:10 Scores reset       ││
│  │ ...                         ││
│  └─────────────────────────────┘│
│        [Clear Log]              │
│                                 │
└─────────────────────────────────┘
```

#### Score Increment Options

| Sport | Increment | Notes |
|-------|-----------|-------|
| Generic | +1 | Default |
| Basketball | +2 | Common basket |
| Basketball (3pt) | +3 | Three-pointer |
| Football (US) | +6 | Touchdown |
| Football (US) | +7 | TD + extra point |
| Rugby | +5 | Try |
| Rugby | +7 | Converted try |
| Soccer/Hockey | +1 | Goal |

#### Settings Persistence

All settings stored locally via `UserDefaults` with `@AppStorage`:

```swift
class AppSettings: ObservableObject {
    @AppStorage("scoreIncrement") var scoreIncrement: Int = 1
    @AppStorage("slowTimerUpdates") var slowTimerUpdates: Bool = false
    @AppStorage("lastConnectedId") var lastConnectedId: String?
}
```

---

## 6. State Management

### 6.1 Core State Model

```swift
@Observable
class ScoreboardState {
    var blueScore: UInt8 = 0           // 0-99
    var redScore: UInt8 = 0            // 0-99
    var timerMinutes: UInt8 = 0        // 0-99 (0 when in score mode)
    var timerSeconds: UInt8 = 0        // 0-59
    var status: ConnectionStatus = .disconnected
    var connectedDeviceId: String?
    var actionLog: [ActionLogEntry] = []
    
    func toPacket(settings: AppSettings) -> Data {
        var packet = Data(count: 5)
        packet[0] = blueScore % 100
        packet[1] = redScore % 100
        packet[2] = timerMinutes
        packet[3] = timerSeconds
        packet[4] = settings.slowTimerUpdates ? 0x01 : 0x00
        return packet
    }
}

enum ConnectionStatus {
    case disconnected
    case scanning
    case connecting
    case connected
    case reconnecting
}
```

### 6.2 Action Log Entry

```swift
struct ActionLogEntry: Identifiable {
    let id = UUID()
    let timestamp: Date
    let deviceId: String
    let type: ActionType
    let description: String
}

enum ActionType {
    case connected
    case disconnected
    case blueScoreChange
    case redScoreChange
    case timerStarted
    case scoresReset
    case settingsChanged
}
```

---

## 7. BLE Service Implementation

### 7.1 BLE Manager Setup

```swift
import CoreBluetooth

class BLEManager: NSObject, ObservableObject {
    static let serviceUUID = CBUUID(string: "12345678-1234-5678-1234-56789abcdef0")
    static let characteristicUUID = CBUUID(string: "12345678-1234-5678-1234-56789abcdef1")
    
    private var centralManager: CBCentralManager!
    private var connectedPeripheral: CBPeripheral?
    private var scoreboardCharacteristic: CBCharacteristic?
    
    @Published var discoveredDevices: [ScoreboardDevice] = []
    @Published var connectionStatus: ConnectionStatus = .disconnected
    
    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }
}
```

### 7.2 Scanning

```swift
extension BLEManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn {
            // Ready to scan
        }
    }
    
    func startScanning() {
        discoveredDevices.removeAll()
        centralManager.scanForPeripherals(
            withServices: [Self.serviceUUID],
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )
        
        // Stop scan after 10 seconds
        DispatchQueue.main.asyncAfter(deadline: .now() + 10) {
            self.centralManager.stopScan()
        }
    }
    
    func centralManager(_ central: CBCentralManager, 
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any],
                        rssi RSSI: NSNumber) {
        let device = ScoreboardDevice(peripheral: peripheral, rssi: RSSI.intValue)
        if !discoveredDevices.contains(where: { $0.id == device.id }) {
            discoveredDevices.append(device)
        }
    }
}
```

### 7.3 Connection & Bonding

```swift
func connect(to device: ScoreboardDevice) {
    connectionStatus = .connecting
    connectedPeripheral = device.peripheral
    connectedPeripheral?.delegate = self
    
    // Connect and request bonding
    centralManager.connect(device.peripheral, options: nil)
}

func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
    // Discover services to find our characteristic
    peripheral.discoverServices([Self.serviceUUID])
}

extension BLEManager: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let service = peripheral.services?.first(where: { $0.uuid == Self.serviceUUID }) else { return }
        peripheral.discoverCharacteristics([Self.characteristicUUID], for: service)
    }
    
    func peripheral(_ peripheral: CBPeripheral, 
                    didDiscoverCharacteristicsFor service: CBService, 
                    error: Error?) {
        guard let characteristic = service.characteristics?.first(where: { $0.uuid == Self.characteristicUUID }) else { return }
        
        scoreboardCharacteristic = characteristic
        
        // Enable indications for ACK
        peripheral.setNotifyValue(true, for: characteristic)
        
        connectionStatus = .connected
    }
}
```

### 7.4 Sending Data with ACK

```swift
func sendPacket(_ packet: Data) async -> Bool {
    guard let peripheral = connectedPeripheral,
          let characteristic = scoreboardCharacteristic else {
        return false
    }
    
    let maxRetries = 3
    
    for attempt in 0..<maxRetries {
        do {
            // Write with response (triggers indication)
            peripheral.writeValue(packet, for: characteristic, type: .withResponse)
            
            // Wait for write confirmation
            try await Task.sleep(nanoseconds: 100_000_000) // 100ms
            return true
        } catch {
            let delay = UInt64(100_000_000 * (attempt + 1)) // Exponential backoff
            try? await Task.sleep(nanoseconds: delay)
        }
    }
    return false
}
```

### 7.5 Auto-Reconnection

```swift
func centralManager(_ central: CBCentralManager, 
                    didDisconnectPeripheral peripheral: CBPeripheral, 
                    error: Error?) {
    connectionStatus = .reconnecting
    startReconnection(to: peripheral)
}

private func startReconnection(to peripheral: CBPeripheral) {
    Task {
        while connectionStatus == .reconnecting {
            centralManager.connect(peripheral, options: nil)
            try? await Task.sleep(nanoseconds: 2_000_000_000) // 2 seconds
            
            if connectionStatus == .connected {
                // Resend current state
                if let packet = currentState?.toPacket(settings: appSettings) {
                    await sendPacket(packet)
                }
                break
            }
        }
    }
}
```

### 7.6 Info.plist Requirements

```xml
<key>NSBluetoothAlwaysUsageDescription</key>
<string>This app uses Bluetooth to connect to and control your scoreboard.</string>
<key>NSBluetoothPeripheralUsageDescription</key>
<string>This app uses Bluetooth to connect to and control your scoreboard.</string>
<key>UIBackgroundModes</key>
<array>
    <string>bluetooth-central</string>
</array>
```

---

## 8. ESP32 Firmware Requirements

### 8.1 BLE Configuration

```c
// Scoreboard must implement:
// - Custom service with service UUID
// - Single characteristic with write + indicate properties
// - Bonding support with bond clearing on startup

void ble_init() {
    // Clear existing bonds on boot
    esp_ble_bond_dev_num = 0;
    
    // Configure as peripheral with custom service
    // Enable bonding
    // Set device name to "Scoreboard XXXX" where XXXX is hardware ID
}
```

### 8.2 Packet Handler Pseudocode

```c
void on_packet_received(uint8_t* data, size_t len) {
    if (len != 5) return;
    
    uint8_t blue_score = data[0];
    uint8_t red_score = data[1];
    uint8_t timer_min = data[2];
    uint8_t timer_sec = data[3];
    uint8_t flags = data[4];
    
    bool slow_update = flags & 0x01;
    
    if (timer_min > 0 || timer_sec > 0) {
        enter_timer_mode(timer_min, timer_sec, slow_update);
    } else {
        enter_score_mode(blue_score, red_score);
    }
    
    // Send indication ACK (handled by BLE stack)
}
```

### 8.3 Timer Countdown Logic

```c
void timer_task(void* params) {
    while (in_timer_mode) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        
        if (timer_seconds > 0) {
            timer_seconds--;
        } else if (timer_minutes > 0) {
            timer_minutes--;
            timer_seconds = 59;
        } else {
            // Timer expired - show 00:00 then return to score mode
            display_timer(0, 0);
            enter_score_mode(last_blue_score, last_red_score);
            return;
        }
        
        // Update display based on slow_update flag
        if (!slow_update || (timer_seconds % 10 == 0)) {
            display_timer(timer_minutes, timer_seconds);
        }
    }
}
```

---

## 9. Error Handling

### 9.1 Connection Errors

| Scenario | App Behavior |
|----------|--------------|
| Scan finds no devices | Show "No scoreboards found" with retry button |
| Pairing rejected | Show error, return to scan screen |
| Connection lost | Show yellow indicator, auto-reconnect in background |
| Write fails | Retry 3x with backoff, then show error toast |

### 9.2 Data Validation

| Input | Validation |
|-------|------------|
| Score direct input | Clamp to 0-99, wrap on overflow |
| Timer input | Cap minutes at 99, seconds at 59 |
| Decrement below 0 | Clamp to 0 |

---

## 10. Testing Plan

### 10.1 Unit Tests (XCTest)

- Score wrapping logic (boundary cases: 99, 100, 199, negative)
- Timer validation (99:59 cap, seconds overflow)
- Packet construction (byte order, flag encoding)
- State transitions

### 10.2 Integration Tests

- BLE scan filtering (mock CBCentralManager)
- Bonding flow
- Packet send with ACK verification
- Reconnection after disconnect

### 10.3 UI Tests (XCUITest)

- Full pairing flow with real scoreboard
- Score update round-trip
- Timer start and countdown verification
- Disconnection/reconnection during timer
- Settings persistence across app restart

### 10.4 TestFlight Beta Testing

- Distribute to beta testers for real-world BLE testing
- Test across iPhone models (iPhone 12+)
- Verify background reconnection behavior

---

## 11. Future Considerations

These features are explicitly out of scope but documented for potential future versions:

- **Android version**: Port to Android using Kotlin/Jetpack Compose
- **Multi-scoreboard control**: Managing multiple scoreboards from one device
- **Team name customization**: Custom names instead of Blue/Red
- **Sound/haptic feedback**: On score changes or timer expiry
- **Game templates**: Pre-configured settings for specific sports
- **Score history/statistics**: Track game history over time
- **iPad support**: Landscape layout optimized for larger screens
- **Widget support**: iOS home screen or Lock Screen widgets
- **Apple Watch companion**: Quick score adjustments from wrist

---

## 12. Development Milestones

| Phase | Tasks | Duration |
|-------|-------|----------|
| **1. Foundation** | Xcode project setup, SwiftUI structure, UserDefaults storage | 3-4 days |
| **2. BLE Core** | CoreBluetooth manager, scanning, connection, bonding, packet sending | 1-2 weeks |
| **3. Score Screen** | Main UI, increment/decrement, direct input, state management | 1 week |
| **4. Timer Screen** | Timer UI, presets, custom input | 3-4 days |
| **5. Settings** | Increment config, slow timer, action log | 3-4 days |
| **6. Polish** | Error handling, reconnection, edge cases, animations | 1 week |
| **7. Testing** | XCTest unit tests, XCUITest, TestFlight beta | 1 week |

**Estimated Total: 5-7 weeks**

---

## Appendix A: BLE UUIDs

```
Service UUID:        12345678-1234-5678-1234-56789abcdef0
Characteristic UUID: 12345678-1234-5678-1234-56789abcdef1
```

*Note: Replace with properly generated UUIDs before production.*

## Appendix B: 7-Segment Character Reference

```
 _      _  _       _   _  _   _   _
| |  |  _| _| |_| |_  |_   | |_| |_|
|_|  | |_  _|   |  _| |_|  | |_|   |
 0   1  2  3  4   5   6  7  8   9

 _       _   _   _       _       _
|_| |_  |   |_| |_  |_  | | |_| |_|
| | |_| |_  |_| |_  | | |_| | |   |
 A   b   C   d   E   F   H   J   L

     _       _       _
|_| | | |_| |   |_  | |
 _| |_| |   |    _| |_|
 n   o   P   r   t   U   y
```

## Appendix C: Sport Increment Presets

| Sport | Primary | Secondary | Notes |
|-------|---------|-----------|-------|
| Basketball | +2 | +3, +1 | 2pt, 3pt, free throw |
| American Football | +6 | +7, +3, +2 | TD, TD+XP, FG, Safety |
| Rugby Union | +5 | +7, +3 | Try, Conv. Try, Penalty |
| Rugby League | +4 | +6, +2, +1 | Try, Conv. Try, Penalty, Drop |
| Soccer | +1 | - | Goal |
| Hockey | +1 | - | Goal |
| Tennis | +1 | - | Game (manual set tracking) |
| Volleyball | +1 | - | Point |
| Table Tennis | +1 | - | Point |