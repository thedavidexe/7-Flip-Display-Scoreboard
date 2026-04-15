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

    // Reconnection state
    private var reconnectionTimer: Timer?   // 60s give-up timer
    private var reconnectScanTimer: Timer?  // drives scan/wait cycle
    private var reconnectTargetID: UUID?    // which peripheral we want back

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
        // Cancel any pending connection before starting a new one
        if let existing = connectedPeripheral {
            centralManager.cancelPeripheralConnection(existing)
        }
        connectionStatus = .connecting
        connectedPeripheral = device.peripheral
        connectedPeripheral?.delegate = self
        centralManager.connect(device.peripheral, options: nil)
    }

    /// Disconnect from the current device
    func disconnect() {
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
        reconnectionTimer?.invalidate()
        reconnectionTimer = nil
        reconnectScanTimer?.invalidate()
        reconnectScanTimer = nil
        reconnectTargetID = nil
        centralManager.stopScan()
        if let p = connectedPeripheral {
            centralManager.cancelPeripheralConnection(p)
        }
        connectedPeripheral = nil
        scoreboardCharacteristic = nil
        connectedDevice = nil
        connectionStatus = .disconnected
    }

    // MARK: - Reconnection Logic

    /// Begin a reconnect attempt for the given peripheral.
    /// Uses belt-and-suspenders: passive connect() registered at all times,
    /// plus active scan cycles to find the device faster when RF is marginal.
    private func enterReconnecting(peripheral: CBPeripheral) {
        // Clear the characteristic — can't use the old one after disconnect.
        // connectedPeripheral stays set so we have the reference for connect().
        scoreboardCharacteristic = nil
        connectionStatus = .reconnecting
        reconnectTargetID = peripheral.identifier

        // Start give-up timer
        reconnectionTimer = Timer.scheduledTimer(
            withTimeInterval: Constants.reconnectionTimeout,
            repeats: false
        ) { [weak self] _ in
            self?.cleanup()  // Navigates to scan page after timeout
        }

        // Register passive connect intent immediately. CoreBluetooth will connect
        // whenever it sees the device advertising, even between our active scan windows.
        centralManager.connect(peripheral, options: nil)

        // Also start active scanning to find the device faster.
        performReconnectScan()
    }

    /// Start a 4-second active scan window. If the target is found, connect.
    /// If not, wait 1 second and try again (until the 60s timer fires).
    private func performReconnectScan() {
        guard connectionStatus == .reconnecting else { return }
        centralManager.stopScan()
        centralManager.scanForPeripherals(withServices: [Constants.serviceUUID], options: nil)

        // If target not found within the scan window, pause briefly and retry
        reconnectScanTimer = Timer.scheduledTimer(withTimeInterval: 4.0, repeats: false) { [weak self] _ in
            guard let self, self.connectionStatus == .reconnecting else { return }
            self.centralManager.stopScan()
            self.reconnectScanTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: false) { [weak self] _ in
                self?.performReconnectScan()
            }
        }
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
        // During reconnection: if this is our target, stop scanning and connect immediately
        if connectionStatus == .reconnecting, peripheral.identifier == reconnectTargetID {
            reconnectScanTimer?.invalidate()
            reconnectScanTimer = nil
            centralManager.stopScan()
            // Update to fresh peripheral reference before connecting
            connectedPeripheral = peripheral
            peripheral.delegate = self
            centralManager.connect(peripheral, options: nil)
            return
        }

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
        // Cancel all reconnect machinery
        reconnectionTimer?.invalidate()
        reconnectionTimer = nil
        reconnectScanTimer?.invalidate()
        reconnectScanTimer = nil
        reconnectTargetID = nil
        centralManager.stopScan()
        // Update peripheral reference and start service discovery
        connectedPeripheral = peripheral
        peripheral.delegate = self
        peripheral.discoverServices([Constants.serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        if connectionStatus == .reconnecting {
            // Don't give up — immediately re-register the passive connect intent and
            // restart the active scan to catch the next advertising window.
            centralManager.connect(peripheral, options: nil)
            reconnectScanTimer?.invalidate()
            performReconnectScan()
        } else {
            lastError = error?.localizedDescription ?? "Failed to connect"
            cleanup()
        }
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        if error != nil {
            // Unexpected disconnect — begin active reconnect loop
            enterReconnecting(peripheral: peripheral)
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
