// voicecode-status <state> [detail]
// Writes "<state>\t<detail>\t<agent>" to the M5 Voice Code status characteristic.
// Agent comes from $VOICECODE_AGENT: "claude" (default) or "gpt".
// state: idle | thinking | working | permission | done | error
// The watch is already connected to the Mac as a BLE keyboard, so the
// peripheral is fetched with retrieveConnectedPeripherals (no scanning).
import CoreBluetooth
import Foundation

let serviceUUID = CBUUID(string: "7c5a0001-5e0c-4e7a-9b1d-c1a0de00c0de")
let statusUUID = CBUUID(string: "7c5a0002-5e0c-4e7a-9b1d-c1a0de00c0de")

let args = CommandLine.arguments
guard args.count >= 2 else {
    FileHandle.standardError.write("usage: voicecode-status <state> [detail]\n".data(using: .utf8)!)
    exit(64)
}
let agent = ProcessInfo.processInfo.environment["VOICECODE_AGENT"] ?? "claude"
let detail = args.count > 2 ? args[2...].joined(separator: " ") : ""
let payload = [args[1], detail, agent].joined(separator: "\t").data(using: .utf8)!

final class Writer: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var central: CBCentralManager!
    var peripheral: CBPeripheral?

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        guard c.state == .poweredOn else {
            if c.state != .unknown && c.state != .resetting { fail("bluetooth unavailable (\(c.state.rawValue))") }
            return
        }
        guard let p = c.retrieveConnectedPeripherals(withServices: [serviceUUID]).first else {
            fail("M5 Voice Code not connected")
            return
        }
        peripheral = p
        p.delegate = self
        c.connect(p)
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        p.discoverServices([serviceUUID])
    }

    func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        fail("connect failed: \(error?.localizedDescription ?? "?")")
    }

    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard let s = p.services?.first(where: { $0.uuid == serviceUUID }) else { return fail("status service missing") }
        p.discoverCharacteristics([statusUUID], for: s)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        guard let ch = s.characteristics?.first(where: { $0.uuid == statusUUID }) else { return fail("status characteristic missing") }
        p.writeValue(payload, for: ch, type: .withResponse)
    }

    func peripheral(_ p: CBPeripheral, didWriteValueFor ch: CBCharacteristic, error: Error?) {
        if let error { return fail("write failed: \(error.localizedDescription)") }
        exit(0)
    }

    func fail(_ msg: String) {
        FileHandle.standardError.write("voicecode-status: \(msg)\n".data(using: .utf8)!)
        exit(1)
    }
}

let writer = Writer()
DispatchQueue.main.asyncAfter(deadline: .now() + 5) { writer.fail("timeout") }
RunLoop.main.run()
