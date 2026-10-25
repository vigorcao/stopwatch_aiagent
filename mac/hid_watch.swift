// Print raw key events coming from the "M5 Voice Code" BLE keyboard.
import Foundation
import IOKit.hid

let manager = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
IOHIDManagerSetDeviceMatching(manager, [kIOHIDProductKey: "M5 Voice Code"] as CFDictionary)
IOHIDManagerRegisterInputValueCallback(manager, { _, _, _, value in
    let e = IOHIDValueGetElement(value)
    let page = IOHIDElementGetUsagePage(e), usage = IOHIDElementGetUsage(e)
    guard page == 0x07, usage != 0xFFFFFFFF, usage > 3 else { return }
    let names: [UInt32: String] = [0xE3: "LeftCmd", 0xE7: "RightCmd", 0x28: "Return"]
    print(String(format: "%.3f  %@ %@", Date().timeIntervalSince1970.truncatingRemainder(dividingBy: 1000),
                 names[usage] ?? String(format: "0x%02X", usage), IOHIDValueGetIntegerValue(value) != 0 ? "down" : "up"))
    fflush(stdout)
}, nil)
IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetMain(), CFRunLoopMode.defaultMode.rawValue)
let rc = IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone))
let devices = (IOHIDManagerCopyDevices(manager) as? Set<IOHIDDevice>)?.count ?? 0
print("open rc=\(rc) devices=\(devices)  (press/hold A on the watch)"); fflush(stdout)
DispatchQueue.main.asyncAfter(deadline: .now() + (Double(CommandLine.arguments.dropFirst().first ?? "60") ?? 60)) { exit(0) }
RunLoop.main.run()
