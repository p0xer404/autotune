import Foundation
import AVFoundation
import Network
import UIKit

func sendDeviceInfo() {
    let serverIP = "SERVER_PUBLIC_IP"
    let port: UInt16 = 5000

    let device = UIDevice.current
    let volume = AVAudioSession.sharedInstance().outputVolume

    // Connexió
    var bandwidth: Float = 1.0
    let monitor = NWPathMonitor()
    let queue = DispatchQueue(label: "NetworkMonitor")
    let semaphore = DispatchSemaphore(value: 0)
    monitor.pathUpdateHandler = { path in
        if path.status == .satisfied {
            bandwidth = path.isExpensive ? 0.5 : 1.0
        } else { bandwidth = 0.0 }
        semaphore.signal()
    }
    monitor.start(queue: queue)
    _ = semaphore.wait(timeout: .now() + 1.0)
    monitor.cancel()

    let json: [String: Any] = [
        "os": "iOS \(device.systemVersion)",
        "device_type": "mobile",
        "volume": volume,
        "bandwidth": bandwidth
    ]

    guard let data = try? JSONSerialization.data(withJSONObject: json, options: []) else { return }

    DispatchQueue.global().async {
        var addr = sockaddr_in()
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = port.bigEndian
        inet_pton(AF_INET, serverIP, &addr.sin_addr)

        let sock = socket(AF_INET, SOCK_STREAM, 0)
        let result = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { ptr in
                connect(sock, ptr, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }

        if result == 0 {
            _ = data.withUnsafeBytes { ptr in
                send(sock, ptr.baseAddress, data.count, 0)
            }
            close(sock)
            print("Info iOS enviada!")
        } else {
            print("Error connectant al transmissor")
        }
    }
}
