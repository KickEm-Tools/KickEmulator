// Accessibility helper for the KT_KickEM simulator; built and run by sim_click.sh.
//
//   sim_ax <pid> list [FILTER]      print role, title, description, frame of every element
//   sim_ax <pid> click NAME         real mouse click at the centre of the first element whose
//                                   title or description is NAME (searches every app window)
//   sim_ax <pid> at X Y             real mouse click at pixel (X,Y) of the 1024x600 Kick screen,
//                                   wherever the panel window has scaled it
//
// Uses the AX C API directly (one IPC round trip per attribute), which is ~100x faster than
// walking the tree through System Events.
import AppKit
import ApplicationServices
import Foundation

struct Node { let el: AXUIElement; let role: String; let title: String; let desc: String; let frame: CGRect? }

func attr(_ el: AXUIElement, _ name: String) -> CFTypeRef? {
    var v: CFTypeRef?
    return AXUIElementCopyAttributeValue(el, name as CFString, &v) == .success ? v : nil
}

func frame(_ el: AXUIElement) -> CGRect? {
    guard let p = attr(el, kAXPositionAttribute), let s = attr(el, kAXSizeAttribute) else { return nil }
    var pt = CGPoint.zero, sz = CGSize.zero
    AXValueGetValue(p as! AXValue, .cgPoint, &pt)
    AXValueGetValue(s as! AXValue, .cgSize, &sz)
    return CGRect(origin: pt, size: sz)
}

func walk(_ el: AXUIElement, _ depth: Int, _ out: inout [Node]) {
    guard depth < 20, let kids = attr(el, kAXChildrenAttribute) as? [AXUIElement] else { return }
    for k in kids {
        let role = attr(k, kAXRoleAttribute) as? String ?? ""
        if role == kAXApplicationRole as String { continue }   // JUCE can list the app as a child
        out.append(Node(el: k,
                        role: role,
                        title: attr(k, kAXTitleAttribute) as? String ?? "",
                        desc: attr(k, kAXDescriptionAttribute) as? String ?? "",
                        frame: frame(k)))
        walk(k, depth + 1, &out)
    }
}

func click(_ p: CGPoint) {
    for t in [CGEventType.leftMouseDown, .leftMouseUp] {
        CGEvent(mouseEventSource: nil, mouseType: t, mouseCursorPosition: p, mouseButton: .left)?.post(tap: .cghidEventTap)
        usleep(60_000)
    }
}

let args = CommandLine.arguments
guard args.count >= 3, let pid = pid_t(args[1]) else {
    FileHandle.standardError.write("usage: sim_ax <pid> list|click|at ...\n".data(using: .utf8)!); exit(2)
}
NSRunningApplication(processIdentifier: pid)?.activate(options: [])
usleep(150_000)

let app = AXUIElementCreateApplication(pid)
var nodes: [Node] = []
for w in attr(app, kAXWindowsAttribute) as? [AXUIElement] ?? [] {
    nodes.append(Node(el: w, role: kAXWindowRole as String, title: attr(w, kAXTitleAttribute) as? String ?? "", desc: "", frame: frame(w)))
    walk(w, 0, &nodes)
}

switch args[2] {
case "list":
    let f = args.count > 3 ? args[3] : ""
    for n in nodes where f.isEmpty || n.title.contains(f) || n.desc.contains(f) {
        let r = n.frame.map { "\(Int($0.minX)),\(Int($0.minY)) \(Int($0.width))x\(Int($0.height))" } ?? ""
        print("\(n.role)\t\"\(n.title)\"\t\"\(n.desc)\"\t\(r)")
    }
case "click":
    let name = args[3]
    guard let n = nodes.first(where: { $0.title == name }) ?? nodes.first(where: { $0.desc == name }),
          let r = n.frame, r.width > 0, r.height > 0 else {
        FileHandle.standardError.write("sim_ax: no visible element named \"\(name)\"\n".data(using: .utf8)!); exit(1)
    }
    click(CGPoint(x: r.midX, y: r.midY))
    print("clicked \(name) at \(Int(r.midX)),\(Int(r.midY))")
case "at":
    // The Kick screen is the largest element with the screen's 1024:600 aspect ratio.
    let screens = nodes.compactMap { $0.frame }.filter { $0.height > 50 && abs($0.width / $0.height - 1024.0 / 600.0) < 0.02 }
    guard let s = screens.max(by: { $0.width < $1.width }), let x = Double(args[3]), let y = Double(args[4]) else {
        FileHandle.standardError.write("sim_ax: Kick screen not found\n".data(using: .utf8)!); exit(1)
    }
    let p = CGPoint(x: s.minX + x * s.width / 1024.0, y: s.minY + y * s.height / 600.0)
    click(p)
    print("clicked screen \(Int(x)),\(Int(y)) at \(Int(p.x)),\(Int(p.y))")
default:
    exit(2)
}
