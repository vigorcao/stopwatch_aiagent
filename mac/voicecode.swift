// voicecode — Mac helper for the M5StopWatch "Voice Code" app.
//
// Hold A on the watch -> the watch streams its mic over BLE (the Mac mic records
// too, as a fallback); release -> transcribe with a local whisper-server and type
// the text into the focused window (Claude Code), then press Return. Leave it
// running in a terminal tab while you vibe code.
//
// Permissions (asked on first use, granted to the terminal app running this):
//   Bluetooth, Microphone, Accessibility (to type into the focused window).
import AVFoundation
import AppKit
import ApplicationServices
import CoreBluetooth
import Foundation

let serviceUUID = CBUUID(string: "7c5a0001-5e0c-4e7a-9b1d-c1a0de00c0de")
let statusUUID = CBUUID(string: "7c5a0002-5e0c-4e7a-9b1d-c1a0de00c0de")
let talkUUID = CBUUID(string: "7c5a0003-5e0c-4e7a-9b1d-c1a0de00c0de")
let audioUUID = CBUUID(string: "7c5a0004-5e0c-4e7a-9b1d-c1a0de00c0de")

let env = ProcessInfo.processInfo.environment
let whisperModel = env["VOICECODE_MODEL"] ?? NSHomeDirectory() + "/whisper-models/ggml-large-v3-turbo.bin"
let whisperPort = env["VOICECODE_PORT"] ?? "8178"
let language = env["VOICECODE_LANG"] ?? "zh"
let autoSubmit = env["VOICECODE_SUBMIT"] != "0"
// Nudges whisper toward simplified Chinese with English code terms kept as-is.
let whisperPrompt = env["VOICECODE_PROMPT"] ?? "以下是普通话的编程指令，使用简体中文，英文术语保持原样。"
// Which mic to transcribe: "auto" (watch if it streamed, else Mac), "watch", or "mac".
let micSource = env["VOICECODE_MIC"] ?? "auto"
// Watch clips whose peak stays under this are background noise, not speech.
let minWatchPeak = Int(env["VOICECODE_MIN_PEAK"] ?? "") ?? 600

// Whisper's stock hallucinations on silence/noise (video outros, subtitle credits).
let hallucinationMarkers = ["点赞", "订阅", "转发", "打赏", "明镜", "点点栏目", "字幕由", "字幕提供",
                            "谢谢观看", "感谢观看", "Amara", "请不吝"]
func isHallucination(_ text: String) -> Bool {
    hallucinationMarkers.contains { text.contains($0) }
}

func log(_ s: String) {
    let t = DateFormatter.localizedString(from: Date(), dateStyle: .none, timeStyle: .medium)
    print("[\(t)] \(s)")
    fflush(stdout)
}

/* -------------------------------- Whisper -------------------------------- */
final class Whisper {
    private var process: Process?
    var url: URL { URL(string: "http://127.0.0.1:\(whisperPort)/inference")! }

    func start() {
        guard FileManager.default.fileExists(atPath: whisperModel) else {
            log("whisper model not found: \(whisperModel)"); exit(1)
        }
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/opt/homebrew/bin/whisper-server")
        p.arguments = ["-m", whisperModel, "--host", "127.0.0.1", "--port", whisperPort,
                       "-l", language, "--prompt", whisperPrompt, "-t", "6"]
        p.standardOutput = FileHandle.nullDevice
        p.standardError = FileHandle.nullDevice
        try! p.run()
        process = p
        log("whisper-server starting (model loads once, a few seconds)...")
        atexit { whisper.stop() }
        signal(SIGINT) { _ in whisper.stop(); exit(0) }
        signal(SIGTERM) { _ in whisper.stop(); exit(0) }
    }

    func stop() { process?.terminate() }

    func transcribe(wav: Data, completion: @escaping (String?) -> Void) {
        let boundary = "voicecode-\(UUID().uuidString)"
        var body = Data()
        func field(_ name: String, _ value: String) {
            body.append("--\(boundary)\r\nContent-Disposition: form-data; name=\"\(name)\"\r\n\r\n\(value)\r\n".data(using: .utf8)!)
        }
        field("response_format", "text")
        field("temperature", "0.0")
        body.append("--\(boundary)\r\nContent-Disposition: form-data; name=\"file\"; filename=\"a.wav\"\r\nContent-Type: audio/wav\r\n\r\n".data(using: .utf8)!)
        body.append(wav)
        body.append("\r\n--\(boundary)--\r\n".data(using: .utf8)!)

        var req = URLRequest(url: url, timeoutInterval: 60)
        req.httpMethod = "POST"
        req.setValue("multipart/form-data; boundary=\(boundary)", forHTTPHeaderField: "Content-Type")
        URLSession.shared.uploadTask(with: req, from: body) { data, _, error in
            if let error { log("whisper error: \(error.localizedDescription)") }
            let text = data.flatMap { String(data: $0, encoding: .utf8) }?
                .trimmingCharacters(in: .whitespacesAndNewlines)
            completion(text)
        }.resume()
    }
}

func makeWav(_ pcm: [Int16], rate: UInt32 = 16000) -> Data {
    var wav = Data()
    func u32(_ v: UInt32) { withUnsafeBytes(of: v.littleEndian) { wav.append(contentsOf: $0) } }
    func u16(_ v: UInt16) { withUnsafeBytes(of: v.littleEndian) { wav.append(contentsOf: $0) } }
    let dataSize = UInt32(pcm.count * 2)
    wav.append("RIFF".data(using: .ascii)!); u32(36 + dataSize); wav.append("WAVE".data(using: .ascii)!)
    wav.append("fmt ".data(using: .ascii)!); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16)
    wav.append("data".data(using: .ascii)!); u32(dataSize)
    pcm.withUnsafeBytes { wav.append(contentsOf: $0) }
    return wav
}

/* ------------------------------- Watch mic ------------------------------- */
// Packets: [seq u16][predictor i16][step index u8][IMA-ADPCM nibbles, low first].
// Each carries its own decoder state, so a lost packet only drops ~30 ms.
final class WatchAudio {
    private static let steps: [Int] = [
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
        88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598,
        658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
        3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289,
        16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
    private static let indexAdjust = [-1, -1, -1, -1, 2, 4, 6, 8]

    private var pcm = [Int16]()
    private var nextSeq: UInt16 = 0
    private(set) var packets = 0
    private(set) var lost = 0
    private(set) var active = false

    func begin() { pcm.removeAll(keepingCapacity: true); nextSeq = 0; packets = 0; lost = 0; active = true }
    func end() { active = false }
    var seconds: Double { Double(pcm.count) / 16000 }

    func append(_ data: Data) {
        guard active, data.count > 5 else { return }
        let b = [UInt8](data)
        let seq = UInt16(b[0]) | UInt16(b[1]) << 8
        let samplesPerPacket = (b.count - 5) * 2
        // Fill gaps with silence so timing stays right (seq wraps at 65536).
        let gap = Int(seq &- nextSeq)
        if gap > 0 && gap < 200 {
            lost += gap
            pcm.append(contentsOf: repeatElement(0, count: gap * samplesPerPacket))
        }
        nextSeq = seq &+ 1
        packets += 1

        var predictor = Int(Int16(bitPattern: UInt16(b[2]) | UInt16(b[3]) << 8))
        var index = Int(b[4])
        func decode(_ code: Int) {
            let step = WatchAudio.steps[index]
            var delta = step >> 3
            if code & 4 != 0 { delta += step }
            if code & 2 != 0 { delta += step >> 1 }
            if code & 1 != 0 { delta += step >> 2 }
            predictor = max(-32768, min(32767, predictor + (code & 8 != 0 ? -delta : delta)))
            index = max(0, min(88, index + WatchAudio.indexAdjust[code & 7]))
            pcm.append(Int16(predictor))
        }
        for byte in b[5...] {
            decode(Int(byte & 0x0f))
            decode(Int(byte >> 4))
        }
    }

    /// 16 kHz mono WAV, DC removed and normalised (the watch mic is quiet at arm's length).
    func wav() -> Data? {
        guard pcm.count > 16000 / 3 else { return nil }
        // Keep the raw clip for tuning the watch mic gain.
        try? makeWav(pcm).write(to: URL(fileURLWithPath: "/tmp/voicecode-last-watch.wav"))
        // The watch's own button click on press/release saturates its mic; no speech there.
        let trimmed = pcm.count > 16000 ? Array(pcm[1600..<(pcm.count - 2400)]) : pcm
        let mean = trimmed.reduce(0) { $0 + Int($1) } / trimmed.count
        var out = trimmed.map { Int($0) - mean }
        let peak = out.reduce(1) { max($0, abs($1)) }
        // Near-silence makes whisper hallucinate ("请不吝点赞 订阅…"); don't amplify noise.
        guard peak >= minWatchPeak else {
            log(String(format: "watch mic: %.1fs, %d packets, %d lost, peak %d — too quiet, ignored",
                       seconds, packets, lost, peak))
            return nil
        }
        let gain = min(20000.0 / Double(peak), 8.0)
        if gain > 1 { out = out.map { Int(Double($0) * gain) } }
        log(String(format: "watch mic: %.1fs, %d packets, %d lost, peak %d, gain x%.1f",
                   seconds, packets, lost, peak, max(gain, 1)))
        return makeWav(out.map { Int16(max(-32768, min(32767, $0))) })
    }
}

/* -------------------------------- Recorder ------------------------------- */
final class Recorder {
    private let engine = AVAudioEngine()
    private let target = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: 16000, channels: 1, interleaved: true)!
    private var converter: AVAudioConverter?
    private var samples = [Int16]()
    private let lock = NSLock()
    private(set) var recording = false

    func start() {
        lock.lock(); samples.removeAll(keepingCapacity: true); lock.unlock()
        let input = engine.inputNode
        let format = input.outputFormat(forBus: 0)
        converter = AVAudioConverter(from: format, to: target)
        input.removeTap(onBus: 0)
        input.installTap(onBus: 0, bufferSize: 2048, format: format) { [weak self] buffer, _ in
            self?.append(buffer)
        }
        do {
            try engine.start()
            recording = true
        } catch {
            log("microphone error: \(error.localizedDescription)")
        }
    }

    private func append(_ buffer: AVAudioPCMBuffer) {
        guard let converter else { return }
        let ratio = target.sampleRate / buffer.format.sampleRate
        let capacity = AVAudioFrameCount(Double(buffer.frameLength) * ratio) + 32
        guard let out = AVAudioPCMBuffer(pcmFormat: target, frameCapacity: capacity) else { return }
        var fed = false
        converter.convert(to: out, error: nil) { _, status in
            if fed { status.pointee = .noDataNow; return nil }
            fed = true
            status.pointee = .haveData
            return buffer
        }
        guard let ch = out.int16ChannelData else { return }
        lock.lock()
        samples.append(contentsOf: UnsafeBufferPointer(start: ch[0], count: Int(out.frameLength)))
        lock.unlock()
    }

    /// Stops and returns a 16 kHz mono WAV, or nil if too short to be speech.
    func stop() -> Data? {
        engine.inputNode.removeTap(onBus: 0)
        engine.stop()
        recording = false
        lock.lock(); let pcm = samples; lock.unlock()
        guard pcm.count > 16000 / 3 else { return nil }  // < ~0.3 s
        return makeWav(pcm)
    }
}

/* --------------------------------- Typing -------------------------------- */
enum Typer {
    static func type(_ text: String, submit: Bool) {
        let src = CGEventSource(stateID: .hidSystemState)
        // Keyboard events carry at most ~20 UTF-16 units each.
        let units = Array(text.utf16)
        var i = 0
        while i < units.count {
            let chunk = Array(units[i..<min(i + 16, units.count)])
            for down in [true, false] {
                let e = CGEvent(keyboardEventSource: src, virtualKey: 0, keyDown: down)!
                e.keyboardSetUnicodeString(stringLength: chunk.count, unicodeString: chunk)
                e.post(tap: .cghidEventTap)
            }
            i += 16
            usleep(8000)
        }
        guard submit else { return }
        // Separate Return so the TUI sees a keypress, not the tail of a paste.
        usleep(250_000)
        for down in [true, false] {
            CGEvent(keyboardEventSource: src, virtualKey: 36, keyDown: down)!.post(tap: .cghidEventTap)
        }
    }
}

/* -------------------------------- OpenClaw ------------------------------- */
// The 煤球 (Meiqiu) app's OpenClaw mode: the text goes to OpenClaw on the DGX
// Spark over ssh (its main session, so the web UI shows the same chat), the
// reply goes to the watch and is spoken by the Spark's local Qwen3-TTS.
// Set OPENCLAW_HOST to the ssh target of your Spark, e.g. user@spark-host.
let openclawHost = env["OPENCLAW_HOST"] ?? "user@spark-host"
let openclawDir = env["OPENCLAW_DIR"] ?? "~/dgx_spark_openclaw_demo"
let openclawSpeak = env["OPENCLAW_TTS"] != "0"
let openclawVoice = env["OPENCLAW_VOICE"] ?? "vivian"
// Open demo links from the reply (the games it builds) in the Mac's browser.
let openclawOpenLinks = env["OPENCLAW_OPEN"] != "0"
// Saying one of these starts a fresh OpenClaw session (each demo starts with /new).
let openclawResetPhrases = ["新对话", "新会话", "重新开始", "清空对话"]

final class OpenClaw {
    private(set) var busy = false
    private var player: Process?
    private var sshHost: String { openclawHost.components(separatedBy: "@").last ?? openclawHost }

    private func ssh(_ command: String, input: Data, timeout: TimeInterval) -> (Data, Int32) {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/ssh")
        p.arguments = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=5", openclawHost, command]
        let stdin = Pipe(), stdout = Pipe()
        p.standardInput = stdin
        p.standardOutput = stdout
        p.standardError = FileHandle.nullDevice
        do { try p.run() } catch { return (Data(), -1) }
        stdin.fileHandleForWriting.write(input)
        try? stdin.fileHandleForWriting.close()
        let killer = DispatchWorkItem { if p.isRunning { p.terminate() } }
        DispatchQueue.global().asyncAfter(deadline: .now() + timeout, execute: killer)
        let out = stdout.fileHandleForReading.readDataToEndOfFile()
        p.waitUntilExit()
        killer.cancel()
        return (out, p.terminationStatus)
    }

    /// Sends one message; completion gets the reply text (nil on failure), on the main queue.
    func ask(_ text: String, completion: @escaping (String?) -> Void) {
        busy = true
        let reset = openclawResetPhrases.contains { text.contains($0) } && text.count <= 8
        let message = reset ? "/new" : text
        // The message goes through stdin into a temp file: no shell quoting of what was said.
        let command = "cd \(openclawDir) && source env.sh >/dev/null && f=$(mktemp) && cat > \"$f\" && " +
            "openclaw/bin/openclaw agent --agent main --message-file \"$f\" --json 2>/dev/null; rm -f \"$f\""
        DispatchQueue.global().async {
            let (out, status) = self.ssh(command, input: message.data(using: .utf8)!, timeout: 660)
            let reply = OpenClaw.replyText(out)
            if reply == nil { log("⚠️  openclaw failed (ssh exit \(status), \(out.count) bytes)") }
            DispatchQueue.main.async {
                self.busy = false
                completion(reply)
            }
        }
    }

    static func replyText(_ json: Data) -> String? {
        guard let obj = try? JSONSerialization.jsonObject(with: json) as? [String: Any],
              let result = obj["result"] as? [String: Any] else { return nil }
        if let meta = result["meta"] as? [String: Any],
           let text = meta["finalAssistantVisibleText"] as? String, !text.isEmpty { return text }
        let texts = (result["payloads"] as? [[String: Any]] ?? []).compactMap { $0["text"] as? String }
        return texts.isEmpty ? nil : texts.joined(separator: "\n")
    }

    /// Demo links in the reply, with loopback hosts pointed at the Spark.
    func links(in text: String) -> [URL] {
        guard let detector = try? NSDataDetector(types: NSTextCheckingResult.CheckingType.link.rawValue) else { return [] }
        let range = NSRange(text.startIndex..., in: text)
        return detector.matches(in: text, range: range).compactMap { m -> URL? in
            guard var c = m.url.flatMap({ URLComponents(url: $0, resolvingAgainstBaseURL: false) }),
                  c.scheme?.hasPrefix("http") == true else { return nil }
            if ["localhost", "127.0.0.1", "0.0.0.0"].contains(c.host ?? "") { c.host = sshHost }
            return c.host == sshHost ? c.url : nil
        }
    }

    /// Text to read aloud: no links, markdown or emoji, and not too long.
    static func speakable(_ text: String) -> String {
        var s = StatusHub.plain(text)
        if let detector = try? NSDataDetector(types: NSTextCheckingResult.CheckingType.link.rawValue) {
            for m in detector.matches(in: s, range: NSRange(s.startIndex..., in: s)).reversed() {
                if let r = Range(m.range, in: s) { s.replaceSubrange(r, with: "") }
            }
        }
        s = String(s.unicodeScalars.filter { !$0.properties.isEmojiPresentation && $0 != "•" }.map(Character.init))
        // One line per clause; lines that end without punctuation get a comma.
        let lines = s.components(separatedBy: "\n")
            .map { $0.trimmingCharacters(in: .whitespaces) }
            .filter { !$0.isEmpty && !$0.allSatisfy { "-—*_=：:".contains($0) } }
        s = lines.map { "。！？!?，,：:".contains($0.last!) ? $0 : $0 + "，" }.joined()
        // Read the opening only (the watch shows the rest): cut after the last sentence end within ~90 chars.
        let head = String(s.prefix(90))
        // Also drop a dangling "访问链接：" whose link was removed above.
        if s.count > 90 || "：:，".contains(head.last ?? "。"),
           let end = head.lastIndex(where: { "。！？!?".contains($0) }) {
            return String(head[...end])
        }
        return head
    }

    func speak(_ text: String) {
        let line = OpenClaw.speakable(text)
        guard openclawSpeak, !line.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return }
        let body = try! JSONSerialization.data(withJSONObject: ["input": line, "voice": openclawVoice, "response_format": "wav"])
        let command = "curl -s --max-time 60 -H 'Content-Type: application/json' --data-binary @- " +
            "http://127.0.0.1:18002/v1/audio/speech"
        DispatchQueue.global().async {
            let (wav, _) = self.ssh(command, input: body, timeout: 70)
            DispatchQueue.main.async {
                let file = "/tmp/openclaw-reply.wav"
                let isWav = wav.count > 44 && wav.prefix(4) == Data("RIFF".utf8)
                if isWav { try? wav.write(to: URL(fileURLWithPath: file)) }
                // No Spark TTS: fall back to the Mac's own voice.
                self.play(isWav ? ["/usr/bin/afplay", file] : ["/usr/bin/say", "-v", "Tingting", line])
            }
        }
    }

    private func play(_ argv: [String]) {
        stopSpeaking()
        let p = Process()
        p.executableURL = URL(fileURLWithPath: argv[0])
        p.arguments = Array(argv.dropFirst())
        try? p.run()
        player = p
    }

    /// Talking over the reply cuts it off.
    func stopSpeaking() {
        if let p = player, p.isRunning { p.terminate() }
        player = nil
    }
}

/* ------------------------------- Status hub ------------------------------ */
// Every Claude Code / Codex session's hooks report here (voicecode-hook.sh ->
// /tmp/voicecode.sock). The watch gets one merged status per agent, re-sent every
// 10 s with how long the agent has been busy, so the watch face shows a live
// timer and can tell when the Mac has gone quiet.
let hubSocketPath = "/tmp/voicecode.sock"
let stallAfter: TimeInterval = 15 * 60  // busy with no hook at all: interrupted (Esc fires no hook)
let doneFor: TimeInterval = 30 * 60     // "Done" settles back to idle after this
let errorFor: TimeInterval = 10

final class StatusHub {
    struct Session {
        var state: String
        var detail: String
        var busySince: Date
        var updated: Date
    }
    private var sessions: [String: Session] = [:]  // "<agent>\t<session id>"
    private var lastSent: [String: String] = [:]
    private var replies: [String: String] = [:]  // agent -> latest reply, for the watch's details page
    private var source: DispatchSourceRead?
    private var heartbeat: Timer?
    /// state, detail, agent, seconds busy
    var send: ((String, String, String, Int) -> Void)?
    /// agent, reply text
    var sendReply: ((String, String) -> Void)?

    static let priority = ["permission": 5, "working": 4, "thinking": 3, "error": 2, "done": 1, "idle": 0]
    static func isBusy(_ state: String) -> Bool { ["thinking", "working", "permission"].contains(state) }

    func listen() {
        unlink(hubSocketPath)
        let fd = socket(AF_UNIX, SOCK_DGRAM, 0)
        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        let path = hubSocketPath.utf8CString
        withUnsafeMutableBytes(of: &addr.sun_path) { dst in
            path.withUnsafeBytes { dst.copyBytes(from: $0.prefix(dst.count - 1)) }
        }
        let bound = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { bind(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size)) }
        }
        guard fd >= 0, bound == 0 else { log("status hub: can't bind \(hubSocketPath)"); return }
        _ = fcntl(fd, F_SETFL, O_NONBLOCK)
        let src = DispatchSource.makeReadSource(fileDescriptor: fd, queue: .main)
        src.setEventHandler { [weak self] in
            var buf = [UInt8](repeating: 0, count: 4096)
            while true {
                let n = recv(fd, &buf, buf.count, 0)
                guard n > 0 else { break }
                let f = String(decoding: buf[0..<n], as: UTF8.self).components(separatedBy: "\t")
                guard f.count >= 4 else { continue }
                self?.report(f[0], f[1], agent: f[2], session: f[3], reply: f.count > 4 ? f[4] : "")
            }
        }
        src.resume()
        source = src
        heartbeat = Timer.scheduledTimer(withTimeInterval: 10, repeats: true) { [weak self] _ in self?.pushAll() }
    }

    func report(_ state: String, _ detail: String = "", agent: String, session: String, reply: String = "") {
        let key = agent + "\t" + session
        let text = StatusHub.plain(reply)
        if !text.isEmpty {
            replies[agent] = text
            log("⌚ \(agent) reply → watch (\(text.utf8.count) bytes): \(text.prefix(30).replacingOccurrences(of: "\n", with: " "))…")
            sendReply?(agent, text)
        } else if state == "done" {
            log("⌚ \(agent) done, but the hook carried no reply text")
        }
        let now = Date()
        if state == "end" {
            sessions[key] = nil
        } else {
            var s = sessions[key] ?? Session(state: "idle", detail: "", busySince: now, updated: now)
            if StatusHub.isBusy(state) && !StatusHub.isBusy(s.state) { s.busySince = now }
            s.state = state
            s.detail = detail
            s.updated = now
            sessions[key] = s
        }
        push(agent, force: false)
    }

    private func effective(_ s: Session, _ now: Date) -> String {
        let age = now.timeIntervalSince(s.updated)
        if StatusHub.isBusy(s.state) && age > stallAfter { return "idle" }
        if s.state == "done" && age > doneFor { return "idle" }
        if s.state == "error" && age > errorFor { return "idle" }
        return s.state
    }

    /// Busiest session wins; the timer runs from the earliest still-busy session.
    func merged(_ agent: String) -> (state: String, detail: String, seconds: Int) {
        let now = Date()
        sessions = sessions.filter { now.timeIntervalSince($0.value.updated) < 12 * 3600 }
        let mine = sessions.filter { $0.key.hasPrefix(agent + "\t") }.map(\.value)
        let busy = mine.filter { StatusHub.isBusy(effective($0, now)) }
        let rank = { (s: Session) in (StatusHub.priority[self.effective(s, now)]!, s.updated) }
        if let top = busy.max(by: { rank($0) < rank($1) }) {
            let since = busy.map(\.busySince).min()!
            return (effective(top, now), top.detail, Int(now.timeIntervalSince(since)))
        }
        if let last = mine.max(by: { $0.updated < $1.updated }) { return (effective(last, now), last.detail, 0) }
        return ("idle", "", 0)
    }

    func push(_ agent: String, force: Bool) {
        let m = merged(agent)
        let line = m.state + "\t" + m.detail
        guard force || lastSent[agent] != line else { return }
        if lastSent[agent] != line { log("⌚ \(agent) → \(m.state)\(m.detail.isEmpty ? "" : " " + m.detail)") }
        lastSent[agent] = line
        send?(m.state, m.detail, agent, m.seconds)
    }

    func pushAll() {
        // Older watch firmware files unknown agents under Claude: only push OpenClaw once it's in use.
        let openclawUsed = sessions.keys.contains { $0.hasPrefix("openclaw\t") }
        for agent in ["claude", "gpt"] + (openclawUsed ? ["openclaw"] : []) { push(agent, force: true) }
    }

    /// After a (re)connect the watch has lost the replies too.
    func pushReplies() {
        for (agent, text) in replies { sendReply?(agent, text) }
    }

    /// Markdown -> plain text that fits one BLE write (≤ 500 bytes, cut on a character).
    static func plain(_ markdown: String) -> String {
        var lines = [String]()
        for raw in markdown.components(separatedBy: "\n") {
            var line = raw.trimmingCharacters(in: .whitespaces)
            if line.hasPrefix("```") || line.allSatisfy({ "|-: ".contains($0) }) && line.contains("-") { continue }
            while line.hasPrefix("#") || line.hasPrefix(">") { line = String(line.dropFirst()).trimmingCharacters(in: .whitespaces) }
            if line.hasPrefix("- ") || line.hasPrefix("* ") { line = "• " + line.dropFirst(2) }
            for token in ["**", "__", "`"] { line = line.replacingOccurrences(of: token, with: "") }
            line = line.replacingOccurrences(of: "\t", with: " ")
            if line.isEmpty && (lines.last?.isEmpty ?? true) { continue }
            lines.append(line)
        }
        var out = lines.joined(separator: "\n").trimmingCharacters(in: .whitespacesAndNewlines)
        while out.utf8.count > 500 { out.removeLast() }
        return out
    }
}

/* ---------------------------------- Link --------------------------------- */
final class Link: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var central: CBCentralManager!
    var peripheral: CBPeripheral?
    var statusChar: CBCharacteristic?
    var onTalk: ((String) -> Void)?
    var onAudio: ((Data) -> Void)?
    var onReady: (() -> Void)?
    private var retryTimer: Timer?

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        if c.state == .poweredOn { attach() } else { log("bluetooth state \(c.state.rawValue)") }
    }

    // The watch is bonded as a keyboard, so macOS keeps it connected; we just
    // attach to that connection. Poll until it shows up.
    func attach() {
        retryTimer?.invalidate()
        if let p = central.retrieveConnectedPeripherals(withServices: [serviceUUID]).first {
            peripheral = p
            p.delegate = self
            central.connect(p)
        } else {
            log("waiting for the watch (open Voice Code on it)...")
            retryTimer = Timer.scheduledTimer(withTimeInterval: 3, repeats: false) { [weak self] _ in self?.attach() }
        }
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        p.discoverServices([serviceUUID])
    }

    func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        log("watch disconnected")
        statusChar = nil
        attach()
    }

    func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        attach()
    }

    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard let s = p.services?.first(where: { $0.uuid == serviceUUID }) else { return }
        p.discoverCharacteristics([statusUUID, talkUUID, audioUUID], for: s)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        for ch in s.characteristics ?? [] {
            if ch.uuid == talkUUID || ch.uuid == audioUUID { p.setNotifyValue(true, for: ch) }
            if ch.uuid == statusUUID { statusChar = ch }
        }
        if statusChar != nil { onReady?() }  // (re)connected: bring the watch up to date
        if s.characteristics?.contains(where: { $0.uuid == talkUUID }) != true {
            log("watch firmware has no talk characteristic — forget & re-pair \"M5 Voice Code\" in Bluetooth settings")
        } else if s.characteristics?.contains(where: { $0.uuid == audioUUID }) != true {
            log("no watch-mic channel (old firmware or cached pairing) — using the Mac mic; " +
                "after flashing new firmware, forget & re-pair \"M5 Voice Code\" in Bluetooth settings")
        }
    }

    func peripheral(_ p: CBPeripheral, didWriteValueFor ch: CBCharacteristic, error: Error?) {
        if let error { log("⚠️  write to watch failed: \(error.localizedDescription)") }
    }

    func peripheral(_ p: CBPeripheral, didUpdateNotificationStateFor ch: CBCharacteristic, error: Error?) {
        if ch.uuid == talkUUID && ch.isNotifying { log("ready — hold A on the watch to talk") }
    }

    func peripheral(_ p: CBPeripheral, didUpdateValueFor ch: CBCharacteristic, error: Error?) {
        guard let v = ch.value else { return }
        if ch.uuid == audioUUID { onAudio?(v); return }
        guard ch.uuid == talkUUID, let event = String(data: v, encoding: .utf8) else { return }
        onTalk?(event)
    }

    func sendStatus(_ state: String, _ detail: String, agent: String, seconds: Int) {
        write([state, String(detail.prefix(40)), agent, String(seconds)].joined(separator: "\t"))
    }

    /// The Mac's clock and time zone, so the watch shows the right time.
    func sendTime() {
        let offset = TimeZone.current.secondsFromGMT()
        let h = abs(offset) / 3600, m = abs(offset) % 3600 / 60
        let name = String(format: "%@%02d%@", offset >= 0 ? "+" : "-", h, m > 0 ? String(format: "%02d", m) : "")
        // POSIX TZ counts hours *west* of UTC, so the sign flips: UTC+8 -> "<+08>-8".
        let posix = "<\(name)>\(offset >= 0 ? "-" : "")\(h)" + (m > 0 ? String(format: ":%02d", m) : "")
        write(["time", String(Int(Date().timeIntervalSince1970)), posix].joined(separator: "\t"))
        log("⌚ time synced to watch (\(posix))")
    }

    func sendReply(_ text: String, agent: String) {
        write(["reply", text, agent].joined(separator: "\t"))  // ≤ 512 bytes: one long write
    }

    private func write(_ payload: String) {
        guard let p = peripheral, let ch = statusChar else { return }
        p.writeValue(payload.data(using: .utf8)!, for: ch, type: .withResponse)
    }
}

/* ---------------------------------- Main --------------------------------- */
let whisper = Whisper()
let recorder = Recorder()
let watchAudio = WatchAudio()
let link = Link()
let hub = StatusHub()
let openclaw = OpenClaw()
link.onAudio = { watchAudio.append($0) }
hub.send = { link.sendStatus($0, $1, agent: $2, seconds: $3) }
hub.sendReply = { link.sendReply($1, agent: $0) }
link.onReady = { link.sendTime(); hub.pushAll(); hub.pushReplies() }
hub.listen()
// The helper's own steps (transcribing, didn't catch that) show as a "voice" session.
func voiceStatus(_ state: String, _ detail: String = "", agent: String) {
    hub.report(state, detail, agent: agent, session: "voice")
}

if !AXIsProcessTrustedWithOptions([kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary) {
    log("⚠️  Accessibility permission needed to type: System Settings > Privacy & Security > Accessibility > enable your terminal app")
}
AVCaptureDevice.requestAccess(for: .audio) { ok in if !ok { log("⚠️  Microphone permission denied") } }
whisper.start()

// Talk events are "<down|up|cancel>\t<agent>[\twatch]"; agent is "claude"
// (Voice Code), "gpt" (the 黑豆 GPT session) or "openclaw" (煤球's OpenClaw
// mode); "watch" means the watch mic is streaming. Claude / GPT text goes to the
// focused window; OpenClaw text goes to the Spark.
link.onTalk = { payload in
    let parts = payload.split(separator: "\t").map(String.init)
    let event = parts.first ?? ""
    let agent = parts.count > 1 ? parts[1] : "claude"
    let watchMic = parts.count > 2 && parts[2] == "watch" && micSource != "mac"
    switch event {
    case "down":
        openclaw.stopSpeaking()
        log("🎙  listening... (\(agent), \(watchMic ? "watch mic" : "Mac mic"))")
        if watchMic { watchAudio.begin() }
        // The Mac mic still records in auto mode, in case the watch stream fails.
        if !watchMic || micSource == "auto" { recorder.start() }
    case "cancel":
        watchAudio.end()
        if recorder.recording { _ = recorder.stop() }
        log("cancelled")
    case "up":
        let streamed = watchAudio.active
        watchAudio.end()
        let macWav = recorder.recording ? recorder.stop() : nil
        let watchWav = streamed ? watchAudio.wav() : nil
        if streamed && watchWav == nil && macWav != nil { log("watch mic unusable — using the Mac mic") }
        guard let wav = watchWav ?? macWav else {
            log("too short, ignored")
            voiceStatus("end", agent: agent)
            return
        }
        voiceStatus("working", "Transcribing", agent: agent)
        let started = Date()
        whisper.transcribe(wav: wav) { text in
            DispatchQueue.main.async {
                if let text, isHallucination(text) { log("🚫 dropped whisper hallucination: \(text)") }
                guard let text, !text.isEmpty, !text.hasPrefix("{"), !isHallucination(text) else {
                    log("no speech recognised")
                    voiceStatus("error", "Didn't catch that", agent: agent)
                    return
                }
                log(String(format: "📝 %@  (%.1fs, %@)", text, Date().timeIntervalSince(started), agent))
                if agent == "openclaw" {
                    askOpenClaw(text)
                    return
                }
                Typer.type(text, submit: autoSubmit)
                // Claude hooks take over from here; plain ChatGPT chats have no
                // hooks, so celebrate the send (Codex hooks update it after).
                voiceStatus(agent == "gpt" ? "done" : "end", agent == "gpt" ? "Sent" : "", agent: agent)
            }
        }
    case "boot":
        // Sent once per watch boot: why it last restarted, and its uptime.
        log("⌚ watch booted — last reset: \(agent)\(parts.count > 2 ? ", up " + parts[2] : "")")
    default:
        break
    }
}

func askOpenClaw(_ text: String) {
    guard !openclaw.busy else {
        log("🦞 openclaw still busy, dropped: \(text)")
        voiceStatus("error", "Still working", agent: "openclaw")
        return
    }
    voiceStatus("thinking", "OpenClaw", agent: "openclaw")
    let started = Date()
    openclaw.ask(text) { reply in
        guard let reply else {
            voiceStatus("error", "Spark unreachable", agent: "openclaw")
            return
        }
        log(String(format: "🦞 openclaw (%.0fs): %@", Date().timeIntervalSince(started),
                   String(reply.prefix(80)).replacingOccurrences(of: "\n", with: " ")))
        hub.report("done", "", agent: "openclaw", session: "voice", reply: reply)
        if openclawOpenLinks, let url = openclaw.links(in: reply).last {
            log("🌐 opening \(url.absoluteString)")
            NSWorkspace.shared.open(url)
        }
        openclaw.speak(reply)
    }
}

RunLoop.main.run()
