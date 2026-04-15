import Foundation
import CoreBluetooth

/// Manages BLE communication with the scoreboard
@Observable
class BLEManager: NSObject {
    // MARK: - Published State

    /// Discovered scoreboard devices
    var discoveredDevices: [ScoreboardDevice] = []

    /// Current connection status
    var connectionStatus: ConnectionStatus = .disconnected

    /// Currently connected device
    var connectedDevice: ScoreboardDevice?

    /// Last error message
    var lastError: String?

    // MARK: - Private Properties

    private var centralManager: CBCentralManager!
    private var connectedPeripheral: CBPeripheral?
    private var scoreboardCharacteristic: CBCharacteristic?
    private var scanTimer: Timer?

    // MARK: - Initialization

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }

    // MARK: - Public API

    /// Start scanning for scoreboard devices
    func startScanning() {
        guard centralManager.state == .poweredOn else {
            lastError = "Bluetooth is not available"
            return
        }

        discoveredDevices.removeAll()
        connectionStatus = .scanning
        lastError = nil

        centralManager.scanForPeripherals(
            withServices: [Constants.serviceUUID],
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )

        // Stop scan after timeout
        scanTimer = Timer.scheduledTimer(withTimeInterval: Constants.scanTimeout, repeats: false) { [weak self] _ in
            self?.stopScanning()
        }
    }

    /// Stop scanning for devices
    func stopScanning() {
        centralManager.stopScan()
        scanTimer?.invalidate()
        scanTimer = nil
        if connectionStatus == .scanning {
            connectionStatus = .disconnected
        }
    }

    /// Connect to a discovered device
    /// - Parameter device: The device to connect to
    func connect(to device: ScoreboardDevice) {
        stopScanning()
        connectionStatus = .connecting
        connectedPeripheral = device.peripheral
        connectedPeripheral?.delegate = self
        centralManager.connect(device.peripheral, options: nil)
    }

    /// Disconnect from the current device
    func disconnect() {
        if let peripheral = connectedPeripheral {
            centralManager.cancelPeripheralConnection(peripheral)
        }
        cleanup()
    }

    /// Send a packet to the scoreboard
    /// - Parameter packet: 5-byte data packet
    /// - Returns: Whether the write succeeded
    func sendPacket(_ packet: Data) async -> Bool {
        guard let peripheral = connectedPeripheral,
              let characteristic = scoreboardCharacteristic else {
            return false
        }

        for attempt in 0..<Constants.writeRetryCount {
            // Write with response
            peripheral.writeValue(packet, for: characteristic, type: .withResponse)

            // Wait for confirmation
            let delay = Constants.writeRetryDelayBase * Double(attempt + 1)
            try? await Task.sleep(nanoseconds: UInt64(delay * 1_000_000_000))

            // If still connected, assume success
            if connectionStatus == .connected {
                return true
            }
        }

        return false
    }

    // MARK: - Private Helpers

    private func cleanup() {
        connectedPeripheral = nil
        scoreboardCharacteristic = nil
        connectedDevice = nil
        connectionStatus = .disconnected
    }

    private func extractHardwareId(from name: String?) -> String {
        // Device name format: "Scoreboard XXXX"
        guard let name = name, name.count >= 4 else {
            return "????"
        }
        return String(name.suffix(4))
    }
}

// MARK: - CBCentralManagerDelegate

extension BLEManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            lastError = nil
        case .poweredOff:
            lastError = "Bluetooth is turned off"
            cleanup()
        case .unauthorized:
            lastError = "Bluetooth access not authorized"
        case .unsupported:
            lastError = "Bluetooth LE not supported"
        case .resetting:
            lastError = "Bluetooth is resetting"
        case .unknown:
            lastError = nil
        @unknown default:
            lastError = nil
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any],
                        rssi RSSI: NSNumber) {
        let hardwareId = extractHardwareId(from: peripheral.name)

        let device = ScoreboardDevice(
            id: peripheral.identifier,
            peripheral: peripheral,
            hardwareId: hardwareId,
            rssi: RSSI.intValue
        )

        // Add or update device in list
        if let index = discoveredDevices.firstIndex(where: { $0.id == device.id }) {
            discoveredDevices[index] = device
        } else {
            discoveredDevices.append(device)
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.discoverServices([Constants.serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        lastError = error?.localizedDescription ?? "Failed to connect"
        cleanup()
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        if error != nil {
            // Unexpected disconnect - try to reconnect
            connectionStatus = .reconnecting
            centralManager.connect(peripheral, options: nil)
        } else {
            cleanup()
        }
    }
}

// MARK: - CBPeripheralDelegate

extension BLEManager: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error = error {
            lastError = "Service discovery failed: \(error.localizedDescription)"
            return
        }

        guard let service = peripheral.services?.first(where: { $0.uuid == Constants.serviceUUID }) else {
            lastError = "Scoreboard service not found"
            return
        }

        peripheral.discoverCharacteristics([Constants.characteristicUUID], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error = error {
            lastError = "Characteristic discovery failed: \(error.localizedDescription)"
            return
        }

        guard let characteristic = service.characteristics?.first(where: { $0.uuid == Constants.characteristicUUID }) else {
            lastError = "Scoreboard characteristic not found"
            return
        }

        scoreboardCharacteristic = characteristic

        // Enable indications for ACK
        if characteristic.properties.contains(.indicate) {
            peripheral.setNotifyValue(true, for: characteristic)
        }

        // Find the device in discovered list
        if let device = discoveredDevices.first(where: { $0.peripheral.identifier == peripheral.identifier }) {
            connectedDevice = device
        } else {
            connectedDevice = ScoreboardDevice(
                id: peripheral.identifier,
                peripheral: peripheral,
                hardwareId: extractHardwareId(from: peripheral.name),
                rssi: -50
            )
        }

        connectionStatus = .connected
        lastError = nil
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error = error {
            lastError = "Write failed: \(error.localizedDescription)"
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        if let error = error {
            lastError = "Failed to enable notifications: \(error.localizedDescription)"
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error = error {
            lastError = "Value update error: \(error.localizedDescription)"
        }
    }
}
