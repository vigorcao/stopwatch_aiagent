// Probe the watch's GATT table: lists services/characteristics of "M5 Voice Code".
import CoreBluetooth
import Foundation

final class Probe: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var central: CBCentralManager!
    var target: CBPeripheral?
    var pending = 0

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        print("bluetooth state:", c.state.rawValue)
        guard c.state == .poweredOn else { return }
        let known = c.retrieveConnectedPeripherals(withServices: [CBUUID(string: "1812"), CBUUID(string: "7c5a0001-5e0c-4e7a-9b1d-c1a0de00c0de"), CBUUID(string: "180A")])
        print("already-connected matches:", known.map { $0.name ?? "?" })
        if let p = known.first(where: { $0.name?.contains("Voice") ?? false }) ?? known.first {
            use(p)
        } else {
            print("scanning...")
            c.scanForPeripherals(withServices: nil)
        }
    }

    func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral, advertisementData: [String: Any], rssi: NSNumber) {
        let name = p.name ?? (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? ""
        if name.contains("Voice Code") { c.stopScan(); print("found by scan:", name); use(p) }
    }

    func use(_ p: CBPeripheral) {
        target = p
        p.delegate = self
        central.connect(p)
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        print("connected:", p.name ?? "?")
        p.discoverServices(nil)
    }

    func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        print("connect failed:", error ?? "?"); exit(1)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        if let error { print("service error:", error) }
        let services = p.services ?? []
        print("services:", services.map { $0.uuid.uuidString })
        pending = services.count
        services.forEach { p.discoverCharacteristics(nil, for: $0) }
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        for ch in s.characteristics ?? [] {
            print("  \(s.uuid.uuidString) / \(ch.uuid.uuidString) props=\(ch.properties.rawValue)")
        }
        pending -= 1
        if pending == 0 { exit(0) }
    }
}

let probe = Probe()
DispatchQueue.main.asyncAfter(deadline: .now() + 15) { print("timeout"); exit(2) }
RunLoop.main.run()
