// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The one window for everything a person does day to day. Plain words, one switch each, and the instrument alive in it.
import AppKit
import SwiftUI

private let warmBlack = Color(red: 0.043, green: 0.035, blue: 0.039)
private let brass = Color(red: 0.79, green: 0.63, blue: 0.36)
private let ember = Color(red: 1.0, green: 0.48, blue: 0.24)
private let paper = Color(red: 0.93, green: 0.89, blue: 0.82)

@MainActor
final class HomeModel: ObservableObject {
  @Published var status = hs_home_get_status()
  @Published var input = hs_home_get_input()
  private var timer: Timer?
  private var fast = false

  func start() {
    timer?.invalidate()
    timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 20, repeats: true) { [weak self] _ in
      MainActor.assumeIsolated {
        guard let self else { return }
        self.status = hs_home_get_status()
        self.input = hs_home_get_input()
      }
    }
  }

  func stop() {
    timer?.invalidate()
    timer = nil
  }

  var controllerLine: String {
    switch status.controller {
    case 0: return "Turn on your controller"
    case 2: return "Unplug the cable to use Bluetooth"
    case 3: return "Connected. This controller model has not been tested here, so only the basics work"
    default:
      let battery = status.charging ? "\(status.battery_percent)%, charging" : "\(status.battery_percent)%"
      return "Controller connected, \(battery)"
    }
  }

  var feelLine: String {
    if !status.feel_on { return "Your Mac plays as usual. Choose Feel it and play a song: you feel it in your hands. You can close this window." }
    if status.no_sound_seen { return "Nothing heard yet. Play something on your Mac. If this stays, Handsong needs permission below." }
    switch status.listen_state {
    case 3: return "You are feeling what plays, softly. You can close this window. Full Feel is stronger."
    case 6: return "A game has the controller. This comes back when you leave it."
    case 1: return "Waiting for something to play."
    default: return "You are feeling what plays. You can close this window."
    }
  }

  var speakerLine: String {
    if !status.speaker_on { return "" }
    if status.no_sound_seen { return "Nothing heard yet. Play something on your Mac. If this stays, Handsong needs permission below." }
    switch status.listen_state {
    case 5: return "Experimental, and rough for now. It only works in full screen: open Full Feel and it starts."
    case 6: return "A game has the controller. This comes back when you leave it."
    case 4: return "The controller is playing your Mac's sound, and the Mac is quiet."
    default: return "Waiting for something to play."
    }
  }
}

private struct InstrumentPreview: NSViewRepresentable {
  func makeNSView(context: Context) -> NSView { hs_home_preview_view() }
  func updateNSView(_ nsView: NSView, context: Context) {}
}

private struct LevelBars: View {
  let left: Float
  let right: Float
  var body: some View {
    HStack(spacing: 6) {
      ForEach(0..<2) { side in
        GeometryReader { box in
          ZStack(alignment: .leading) {
            Capsule().fill(Color.white.opacity(0.08))
            Capsule().fill(brass).frame(width: max(2, box.size.width * CGFloat(min(1, side == 0 ? left : right))))
          }
        }
        .frame(height: 5)
      }
    }
    .accessibilityHidden(true)
  }
}

// One question, one answer: where your Mac's sound goes. The link can carry the hands' stream or the speaker's, never both, so there is no combination to get wrong.
private struct SoundCard: View {
  @ObservedObject var model: HomeModel
  private var choice: Binding<Int> {
    Binding(
      get: { model.status.feel_on ? 1 : model.status.speaker_on ? 2 : 0 },
      set: { hs_home_set_feel($0 == 1); hs_home_set_speaker($0 == 2) })
  }
  var body: some View {
    VStack(alignment: .leading, spacing: 10) {
      Text("Your Mac's sound").font(.system(.title3, design: .serif)).foregroundStyle(paper)
      Picker("Your Mac's sound", selection: choice) {
        Text("Off").tag(0)
        Text("Feel it").tag(1)
        Text("Speaker").tag(2)
      }
      .pickerStyle(.segmented)
      .labelsHidden()
      .tint(brass)
      Text(model.status.speaker_on ? model.speakerLine : model.feelLine).font(.callout).foregroundStyle(paper.opacity(0.65)).fixedSize(horizontal: false, vertical: true)
    }
    .padding(14)
    .frame(maxWidth: .infinity, alignment: .leading)
    .background(RoundedRectangle(cornerRadius: 14, style: .continuous).fill(Color.white.opacity(0.045)))
  }
}

private struct SwitchCard: View {
  let title: String
  let line: String
  @Binding var isOn: Bool
  var body: some View {
    VStack(alignment: .leading, spacing: 6) {
      HStack {
        Text(title).font(.system(.title3, design: .serif)).foregroundStyle(paper)
        Spacer()
        Toggle(title, isOn: $isOn).labelsHidden().toggleStyle(.switch).tint(brass)
      }
      Text(line).font(.callout).foregroundStyle(paper.opacity(0.65)).fixedSize(horizontal: false, vertical: true)
    }
    .padding(14)
    .frame(maxWidth: .infinity, alignment: .leading)
    .background(RoundedRectangle(cornerRadius: 14, style: .continuous).fill(Color.white.opacity(0.045)))
  }
}

// Teaching pairing with one small picture: the two keys to hold, and the light that answers.
private struct PairingHint: View {
  @State private var lit = false
  var body: some View {
    VStack(alignment: .leading, spacing: 12) {
      HStack(spacing: 18) {
        ZStack {
          RoundedRectangle(cornerRadius: 22).stroke(paper.opacity(0.35), lineWidth: 1.5).frame(width: 130, height: 64)
          Capsule().fill(ember.opacity(lit ? 1 : 0.15)).frame(width: 46, height: 3).offset(y: -22)
          Circle().fill(paper).frame(width: 12, height: 12).offset(y: 18)
          Capsule().fill(paper).frame(width: 5, height: 12).offset(x: -34, y: -12)
        }
        Text("Hold the small key beside the touch surface and the home button together, until the light flashes.")
          .font(.callout).foregroundStyle(paper.opacity(0.8)).fixedSize(horizontal: false, vertical: true)
      }
      Button("Open Bluetooth Settings") { hs_home_open_bluetooth_settings() }
        .buttonStyle(.bordered)
    }
    .onAppear {
      if !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion {
        withAnimation(.easeInOut(duration: 0.45).repeatForever(autoreverses: true)) { lit = true }
      } else {
        lit = true
      }
    }
    .accessibilityElement(children: .combine)
  }
}

private struct TestBench: View {
  @ObservedObject var model: HomeModel
  @Environment(\.dismiss) private var dismiss
  private let keyNames: [(UInt32, String)] = [(1 << 0, "Left key"), (1 << 1, "Lower key"), (1 << 2, "Right key"), (1 << 3, "Upper key"), (1 << 4, "L1"), (1 << 5, "R1"), (1 << 8, "Create"), (1 << 9, "Options"), (1 << 10, "L3"), (1 << 11, "R3"), (1 << 12, "Home"), (1 << 13, "Touch click"), (1 << 14, "Mute"), (1 << 15, "Cross up"), (1 << 16, "Cross right"), (1 << 17, "Cross down"), (1 << 18, "Cross left")]

  var body: some View {
    VStack(alignment: .leading, spacing: 16) {
      Text("Check your controller").font(.system(.title2, design: .serif)).foregroundStyle(paper)
      Text("Everything here shows the controller as it is right now. Sticks that are not touched should sit in the middle.").font(.callout).foregroundStyle(paper.opacity(0.7))
      HStack(alignment: .top, spacing: 24) {
        stick(model.input.left_x, model.input.left_y, "Left stick")
        stick(model.input.right_x, model.input.right_y, "Right stick")
        VStack(alignment: .leading, spacing: 10) {
          travel("L2", model.input.left_trigger)
          travel("R2", model.input.right_trigger)
          touch
        }
      }
      LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 6), count: 5), spacing: 6) {
        ForEach(keyNames, id: \.0) { key, name in
          Text(name).font(.caption).padding(.vertical, 4).frame(maxWidth: .infinity)
            .background(RoundedRectangle(cornerRadius: 6).fill(model.input.keys & key != 0 ? brass : Color.white.opacity(0.07)))
            .foregroundStyle(model.input.keys & key != 0 ? .black : paper.opacity(0.8))
            .accessibilityLabel("\(name), \(model.input.keys & key != 0 ? "pressed" : "not pressed")")
        }
      }
      HStack {
        Button("Pulse the left hand") { hs_home_pulse_left() }
        Button("Pulse the right hand") { hs_home_pulse_right() }
        Spacer()
        Button("Done") { dismiss() }.keyboardShortcut(.defaultAction).buttonStyle(.borderedProminent).tint(brass)
      }
    }
    .padding(24)
    .frame(width: 620)
    .background(warmBlack)
  }

  private func stick(_ x: Float, _ y: Float, _ name: String) -> some View {
    VStack(spacing: 6) {
      ZStack {
        Circle().stroke(paper.opacity(0.3), lineWidth: 1)
        Circle().fill(brass).frame(width: 12, height: 12).offset(x: CGFloat(x) * 46, y: CGFloat(y) * 46)
      }
      .frame(width: 104, height: 104)
      Text(name).font(.caption).foregroundStyle(paper.opacity(0.7))
    }
    .accessibilityElement(children: .ignore)
    .accessibilityLabel("\(name), \(Int(x * 100)) percent across, \(Int(y * 100)) percent down")
  }

  private func travel(_ name: String, _ value: Float) -> some View {
    HStack {
      Text(name).font(.caption).frame(width: 22).foregroundStyle(paper.opacity(0.7))
      GeometryReader { box in
        ZStack(alignment: .leading) {
          Capsule().fill(Color.white.opacity(0.08))
          Capsule().fill(brass).frame(width: max(2, box.size.width * CGFloat(value)))
        }
      }
      .frame(width: 180, height: 8)
    }
    .accessibilityElement(children: .ignore)
    .accessibilityLabel("\(name), \(Int(value * 100)) percent")
  }

  private var touch: some View {
    ZStack(alignment: .topLeading) {
      RoundedRectangle(cornerRadius: 8).stroke(paper.opacity(0.3), lineWidth: 1).frame(width: 206, height: 60)
      if model.input.touch_down.0 { Circle().fill(Color(red: 0.22, green: 0.82, blue: 0.76)).frame(width: 10, height: 10).offset(x: CGFloat(model.input.touch_x.0) * 196, y: CGFloat(model.input.touch_y.0) * 50) }
      if model.input.touch_down.1 { Circle().fill(Color(red: 0.22, green: 0.82, blue: 0.76)).frame(width: 10, height: 10).offset(x: CGFloat(model.input.touch_x.1) * 196, y: CGFloat(model.input.touch_y.1) * 50) }
    }
    .accessibilityLabel("Touch surface")
  }
}

private struct CareSheet: View {
  @ObservedObject var model: HomeModel
  @Environment(\.dismiss) private var dismiss
  @State private var confirmRemove = false

  var body: some View {
    VStack(alignment: .leading, spacing: 16) {
      Text("Settings").font(.system(.title2, design: .serif)).foregroundStyle(paper)
      Toggle("Turn off the controller's small lights while Handsong runs", isOn: Binding(get: { model.status.calm_lights }, set: { hs_home_set_calm_lights($0) }))
        .toggleStyle(.switch).tint(brass)
      Toggle("Light follows the music in Full Feel", isOn: Binding(get: { hs_home_light_follows_music() }, set: { hs_home_set_light_follows_music($0) }))
        .toggleStyle(.switch).tint(brass)
      Toggle("Open at login, in the menu bar", isOn: Binding(get: { model.status.login_item }, set: { hs_home_set_login_item($0) }))
        .toggleStyle(.switch).tint(brass)
        .disabled(!model.status.in_applications)
      if !model.status.in_applications {
        Text("Move Handsong to your Applications folder first.").font(.caption).foregroundStyle(paper.opacity(0.6))
      }
      Divider()
      HStack {
        Text("Remove Handsong and everything it changed.").foregroundStyle(paper.opacity(0.8))
        Spacer()
        Button("Remove…", role: .destructive) { confirmRemove = true }
      }
      HStack {
        Spacer()
        Button("Done") { dismiss() }.keyboardShortcut(.defaultAction).buttonStyle(.borderedProminent).tint(brass)
      }
    }
    .padding(24)
    .frame(width: 520)
    .background(warmBlack)
    .alert("Remove Handsong?", isPresented: $confirmRemove) {
      Button("Remove", role: .destructive) { hs_home_remove_everything() }
      Button("Keep it", role: .cancel) {}
    } message: {
      Text("The sound output and the login item go, and the app moves to the Trash.")
    }
  }
}

private struct HomeView: View {
  @StateObject var model = HomeModel()
  @State private var showBench = false
  @State private var showCare = false
  @State private var showFlashNote = false
  @State private var freePlayNext = false

  var body: some View {
    HStack(spacing: 0) {
      ZStack(alignment: .bottomLeading) {
        InstrumentPreview().accessibilityHidden(true)
        VStack(alignment: .leading, spacing: 10) {
          Text("Handsong").font(.system(size: 30, weight: .regular, design: .serif)).foregroundStyle(paper)
          Text(model.controllerLine).font(.callout).foregroundStyle(paper.opacity(0.75))
          if model.status.controller == 0 { PairingHint() }
        }
        .padding(22)
      }
      .frame(minWidth: 520, maxWidth: .infinity, maxHeight: .infinity)
      .background(Color.black)

      VStack(alignment: .leading, spacing: 12) {
        SoundCard(model: model)
        if model.status.feel_on { LevelBars(left: model.status.left, right: model.status.right).padding(.horizontal, 14) }
        Button { hs_home_open_listening_room() } label: {
          Text("Open Full Feel").frame(maxWidth: .infinity)
        }
        .controlSize(.large)
        Text("Full screen: the strongest feel.").font(.caption).foregroundStyle(paper.opacity(0.6)).padding(.horizontal, 14)
        if model.status.no_sound_seen {
          Button("Allow Handsong to hear your Mac") {
            NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_AudioCapture")!)
          }
          .buttonStyle(.link)
          .padding(.horizontal, 14)
        }
        Spacer()
        Button {
          freePlayNext = false
          if hs_home_flash_warning_needed() { showFlashNote = true } else { hs_home_open_experience() }
        } label: {
          Text("Start the Tour").font(.system(.title3, design: .serif)).frame(maxWidth: .infinity)
        }
        .controlSize(.extraLarge)
        .buttonStyle(.borderedProminent)
        .tint(brass)
        .disabled(model.status.experience_open)
        HStack {
          Button("Free Play") {
            freePlayNext = true
            if hs_home_flash_warning_needed() { showFlashNote = true } else { hs_home_open_free_play() }
          }.buttonStyle(.link).disabled(model.status.experience_open)
          Spacer()
          Button("Check your controller") { showBench = true }.buttonStyle(.link)
          Spacer()
          Button("Settings") { showCare = true }.buttonStyle(.link)
        }
        .font(.callout)
        .foregroundStyle(paper.opacity(0.7))
      }
      .padding(18)
      .frame(width: 320)
      .background(warmBlack)
    }
    .frame(minWidth: 860, minHeight: 540)
    .preferredColorScheme(.dark)
    .onAppear { model.start() }
    .onDisappear { model.stop() }
    .sheet(isPresented: $showBench) { TestBench(model: model) }
    .sheet(isPresented: $showCare) { CareSheet(model: model) }
    .alert("Moving light ahead", isPresented: $showFlashNote) {
      Button("Continue") { hs_home_flash_warning_seen(); if freePlayNext { hs_home_open_free_play() } else { hs_home_open_experience() } }
      Button("Not now", role: .cancel) {}
    } message: {
      Text("The instrument has bright moments and quick movement. Press Options to pause at any time.")
    }
  }
}

@MainActor private var homeWindow: NSWindow?

@_cdecl("hs_home_show")
public func hsHomeShow() {
  MainActor.assumeIsolated {
    if let window = homeWindow {
      _ = NSApp.setActivationPolicy(.regular)
      window.makeKeyAndOrderFront(nil)
      NSApp.activate(ignoringOtherApps: true)
      return
    }
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 960, height: 600), styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView], backing: .buffered, defer: false)
    window.titlebarAppearsTransparent = true
    window.titleVisibility = .hidden
    window.title = "Handsong"
    window.isReleasedWhenClosed = false
    window.backgroundColor = .black
    window.contentView = NSHostingView(rootView: HomeView())
    window.center()
    homeWindow = window
    _ = NSApp.setActivationPolicy(.regular)
    window.makeKeyAndOrderFront(nil)
    NSApp.activate(ignoringOtherApps: true)
    NotificationCenter.default.addObserver(forName: NSWindow.willCloseNotification, object: window, queue: .main) { _ in
      MainActor.assumeIsolated {
        if !hs_home_get_status().experience_open { _ = NSApp.setActivationPolicy(.accessory) }
      }
    }
  }
}
