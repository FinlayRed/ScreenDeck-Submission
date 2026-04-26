#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::{Deserialize, Serialize};
use serialport::{ClearBuffer, SerialPort, SerialPortInfo, SerialPortType};
use std::collections::HashMap;
use std::fs;
use std::io;
use std::path::PathBuf;
use std::process::Command;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};
use tauri::{
    AppHandle, CustomMenuItem, Manager, State, SystemTray, SystemTrayEvent, SystemTrayMenu,
    WindowEvent,
};
use tauri_plugin_autostart::{MacosLauncher, ManagerExt};

const GRID_ROWS: u8 = 4;
const GRID_COLS: u8 = 8;
const ICON_BYTE_SIZE: usize = 85 * 85 * 2;
const MAX_UPLOAD_BYTES: usize = 1024 * 1024;
const HANDSHAKE_TOTAL_TIMEOUT_MS: u64 = 20000;
const PROBE_HANDSHAKE_TIMEOUT_MS: u64 = 8000;
const PORT_SETTLE_MS: u64 = 1000;
const UPLOAD_CHUNK_SIZE: usize = 32;
const UPLOAD_INTER_CHUNK_DELAY_MS: u64 = 4;
const UPLOAD_IO_TIMEOUT_MS: u64 = 1200;
const UPLOAD_WRITE_RETRY_COUNT: usize = 6;
const UPLOAD_RETRY_SLEEP_MS: u64 = 8;
const UPLOAD_ATTEMPT_RETRY_COUNT: usize = 2;
const UPLOAD_OK_WAIT_TIMEOUT_SECS: u64 = 35;
const DRAIN_WINDOW_MS: u64 = 350;
const DOWNLOAD_READY_WAIT_TIMEOUT_SECS: u64 = 10;
const DOWNLOAD_READ_TIMEOUT_SECS: u64 = 45;
const DOWNLOAD_OK_WAIT_TIMEOUT_SECS: u64 = 12;
const UPLOAD_PROGRESS_EVENT: &str = "upload-progress";
const COMPANION_STATUS_EVENT: &str = "companion-status";
const COMPANION_LOG_EVENT: &str = "companion-log";
const DEVICE_BUTTON_EVENT: &str = "device-button-event";
const LISTENER_RECONNECT_DELAY_MS: u64 = 1200;
const LISTENER_IDLE_SLEEP_MS: u64 = 300;
const LISTENER_PAUSE_WAIT_MS: u64 = 8000;
const LISTENER_HANDSHAKE_TIMEOUT_MS: u64 = 500;
const COMPANION_SETTINGS_FILE: &str = "companion-settings.json";
const COMPANION_MACROS_FILE: &str = "companion-macros.json";

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct ProbePortResult {
    port_name: String,
    responsive: bool,
    detail: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct ProbePortsResult {
    ports: Vec<ProbePortResult>,
    suggested_port: Option<String>,
    logs: Vec<String>,
}

#[derive(Debug, Deserialize, Clone)]
#[serde(rename_all = "camelCase")]
struct IconUpload {
    index: u8,
    row: u8,
    col: u8,
    bytes: Vec<u8>,
}

#[derive(Debug, Deserialize, Clone)]
#[serde(rename_all = "camelCase")]
struct SendUpdatesRequest {
    port_name: String,
    baud_rate: u32,
    icon_uploads: Vec<IconUpload>,
    macros_json: Option<String>,
    send_reload_all: bool,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct SendUpdatesResult {
    logs: Vec<String>,
}

#[derive(Debug, Serialize, Clone)]
#[serde(rename_all = "camelCase")]
struct UploadProgressEvent {
    phase: String,
    current_bytes: usize,
    total_bytes: usize,
    file_bytes_sent: usize,
    file_total_bytes: usize,
    file_path: String,
    file_index: usize,
    file_count: usize,
}

struct UploadProgressTracker<'a> {
    app_handle: &'a AppHandle,
    total_bytes: usize,
    completed_bytes: usize,
    file_total_bytes: usize,
    file_index: usize,
    file_count: usize,
    file_path: &'a str,
}

impl UploadProgressTracker<'_> {
    fn emit(&self, phase: &str, file_bytes_sent: usize) {
        let file_bytes_sent = file_bytes_sent.min(self.file_total_bytes);
        let current_bytes = (self.completed_bytes + file_bytes_sent).min(self.total_bytes);
        let event = UploadProgressEvent {
            phase: phase.to_string(),
            current_bytes,
            total_bytes: self.total_bytes,
            file_bytes_sent,
            file_total_bytes: self.file_total_bytes,
            file_path: self.file_path.to_string(),
            file_index: self.file_index,
            file_count: self.file_count,
        };

        let _ = self.app_handle.emit_all(UPLOAD_PROGRESS_EVENT, event);
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
struct HostCommandAction {
    #[serde(default)]
    label: String,
    #[serde(default)]
    program: String,
    #[serde(default)]
    args: Vec<String>,
    #[serde(default)]
    cwd: Option<String>,
    #[serde(default)]
    run_detached: bool,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
struct HostMacroIconEntry {
    index: Option<u8>,
    row: Option<u8>,
    col: Option<u8>,
    #[serde(default)]
    host_actions: Vec<HostCommandAction>,
}

#[derive(Debug, Deserialize, Default)]
struct HostMacroDocument {
    #[serde(default)]
    icons: Vec<HostMacroIconEntry>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct CompanionSettings {
    port_name: String,
    baud_rate: u32,
    listener_enabled: bool,
}

impl Default for CompanionSettings {
    fn default() -> Self {
        Self {
            port_name: String::new(),
            baud_rate: 115200,
            listener_enabled: true,
        }
    }
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct CompanionStatus {
    port_name: String,
    baud_rate: u32,
    listener_enabled: bool,
    paused: bool,
    connected: bool,
    active_port: Option<String>,
    last_event: Option<String>,
    last_execution: Option<String>,
    last_error: Option<String>,
    host_action_count: usize,
    autostart_enabled: bool,
}

impl CompanionStatus {
    fn from_settings(
        settings: &CompanionSettings,
        host_action_count: usize,
        autostart_enabled: bool,
    ) -> Self {
        Self {
            port_name: settings.port_name.clone(),
            baud_rate: settings.baud_rate,
            listener_enabled: settings.listener_enabled,
            paused: false,
            connected: false,
            active_port: None,
            last_event: None,
            last_execution: None,
            last_error: None,
            host_action_count,
            autostart_enabled,
        }
    }
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct CompanionLogEvent {
    line: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct DeviceButtonEvent {
    index: u8,
    row: u8,
    col: u8,
}

struct CompanionShared {
    settings: CompanionSettings,
    status: CompanionStatus,
    host_actions: HashMap<u8, Vec<HostCommandAction>>,
    paused: bool,
    port_open: bool,
    shutdown: bool,
    settings_path: PathBuf,
    macros_path: PathBuf,
}

#[derive(Clone)]
struct CompanionState {
    shared: Arc<Mutex<CompanionShared>>,
}

impl CompanionState {
    fn new(settings_path: PathBuf, macros_path: PathBuf, autostart_enabled: bool) -> Self {
        let settings = load_companion_settings(&settings_path).unwrap_or_default();
        let host_actions = load_host_actions_cache(&macros_path).unwrap_or_default();
        let host_action_count = host_actions.values().map(Vec::len).sum();
        let status =
            CompanionStatus::from_settings(&settings, host_action_count, autostart_enabled);

        Self {
            shared: Arc::new(Mutex::new(CompanionShared {
                settings,
                status,
                host_actions,
                paused: false,
                port_open: false,
                shutdown: false,
                settings_path,
                macros_path,
            })),
        }
    }

    fn snapshot_status(&self) -> CompanionStatus {
        self.shared.lock().unwrap().status.clone()
    }
}

struct CompanionPauseGuard {
    app_handle: AppHandle,
    companion: CompanionState,
}

impl Drop for CompanionPauseGuard {
    fn drop(&mut self) {
        set_companion_paused(&self.companion, false);
        emit_companion_status(&self.app_handle, &self.companion);
    }
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
struct SyncFromDeviceRequest {
    port_name: String,
    baud_rate: u32,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct IconDownload {
    row: u8,
    col: u8,
    bytes: Vec<u8>,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct SyncFromDeviceResult {
    logs: Vec<String>,
    macros_json: String,
    fallback_bytes: Option<Vec<u8>>,
    icon_downloads: Vec<IconDownload>,
}

fn load_companion_settings(path: &PathBuf) -> Option<CompanionSettings> {
    let text = fs::read_to_string(path).ok()?;
    serde_json::from_str(&text).ok()
}

fn save_companion_settings(companion: &CompanionState) -> Result<(), String> {
    let (path, settings) = {
        let shared = companion.shared.lock().unwrap();
        (shared.settings_path.clone(), shared.settings.clone())
    };

    let text = serde_json::to_string_pretty(&settings)
        .map_err(|e| format!("Failed to serialize companion settings: {e}"))?;
    fs::write(&path, text).map_err(|e| format!("Failed to write companion settings: {e}"))
}

fn parse_host_actions_document(text: &str) -> Result<HashMap<u8, Vec<HostCommandAction>>, String> {
    let document: HostMacroDocument =
        serde_json::from_str(text).map_err(|e| format!("Failed to parse macros.json: {e}"))?;
    let mut actions_by_icon: HashMap<u8, Vec<HostCommandAction>> = HashMap::new();

    for icon in document.icons {
        let index = icon.index.or_else(|| match (icon.row, icon.col) {
            (Some(row), Some(col)) if row < GRID_ROWS && col < GRID_COLS => {
                Some(row * GRID_COLS + col)
            }
            _ => None,
        });

        let Some(index) = index else {
            continue;
        };

        let filtered: Vec<HostCommandAction> = icon
            .host_actions
            .into_iter()
            .filter(|action| !action.program.trim().is_empty())
            .collect();

        if !filtered.is_empty() {
            actions_by_icon.insert(index, filtered);
        }
    }

    Ok(actions_by_icon)
}

fn load_host_actions_cache(path: &PathBuf) -> Option<HashMap<u8, Vec<HostCommandAction>>> {
    let text = fs::read_to_string(path).ok()?;
    parse_host_actions_document(&text).ok()
}

fn update_host_actions_cache(
    companion: &CompanionState,
    macros_json: &str,
) -> Result<usize, String> {
    let actions = parse_host_actions_document(macros_json)?;
    let action_count = actions.values().map(Vec::len).sum();

    let macros_path = {
        let mut shared = companion.shared.lock().unwrap();
        shared.host_actions = actions;
        shared.status.host_action_count = action_count;
        shared.macros_path.clone()
    };

    fs::write(&macros_path, macros_json)
        .map_err(|e| format!("Failed to write companion macro cache: {e}"))?;
    Ok(action_count)
}

fn emit_companion_status(app_handle: &AppHandle, companion: &CompanionState) {
    let status = companion.snapshot_status();
    let _ = app_handle.emit_all(COMPANION_STATUS_EVENT, status);
}

fn emit_companion_log(app_handle: &AppHandle, line: impl Into<String>) {
    let _ = app_handle.emit_all(COMPANION_LOG_EVENT, CompanionLogEvent { line: line.into() });
}

fn update_companion_status<F>(app_handle: &AppHandle, companion: &CompanionState, update: F)
where
    F: FnOnce(&mut CompanionStatus),
{
    {
        let mut shared = companion.shared.lock().unwrap();
        update(&mut shared.status);
    }
    emit_companion_status(app_handle, companion);
}

fn set_companion_paused(companion: &CompanionState, paused: bool) {
    let mut shared = companion.shared.lock().unwrap();
    shared.paused = paused;
    shared.status.paused = paused;
}

fn pause_companion_listener(
    app_handle: &AppHandle,
    companion: &CompanionState,
) -> Result<CompanionPauseGuard, String> {
    set_companion_paused(companion, true);
    emit_companion_status(app_handle, companion);

    let deadline = Instant::now() + Duration::from_millis(LISTENER_PAUSE_WAIT_MS);
    loop {
        let port_open = { companion.shared.lock().unwrap().port_open };
        if !port_open {
            return Ok(CompanionPauseGuard {
                app_handle: app_handle.clone(),
                companion: companion.clone(),
            });
        }

        if Instant::now() >= deadline {
            emit_companion_status(app_handle, companion);
            return Err("Timed out waiting for companion listener to pause".to_string());
        }

        thread::sleep(Duration::from_millis(50));
    }
}

fn companion_settings_snapshot(companion: &CompanionState) -> (CompanionSettings, bool, bool) {
    let shared = companion.shared.lock().unwrap();
    (shared.settings.clone(), shared.paused, shared.shutdown)
}

fn set_companion_connected(
    app_handle: &AppHandle,
    companion: &CompanionState,
    connected: bool,
    active_port: Option<String>,
) {
    update_companion_status(app_handle, companion, |status| {
        status.connected = connected;
        status.active_port = active_port;
        if connected {
            status.last_error = None;
        }
    });
}

fn set_companion_last_error(app_handle: &AppHandle, companion: &CompanionState, message: String) {
    let should_emit = {
        let mut shared = companion.shared.lock().unwrap();
        let status = &mut shared.status;
        let should_emit = status.connected
            || status.active_port.is_some()
            || status.last_error.as_deref() != Some(message.as_str());

        status.connected = false;
        status.active_port = None;
        status.last_error = Some(message.clone());
        should_emit
    };

    if should_emit {
        emit_companion_status(app_handle, companion);
        emit_companion_log(app_handle, message);
    }
}

fn parse_button_event(line: &str) -> Option<DeviceButtonEvent> {
    let mut parts = line.split_whitespace();
    if parts.next()? != "CDC:EVENT" || parts.next()? != "BUTTON" {
        return None;
    }

    let index = parts.next()?.parse::<u8>().ok()?;
    let row = parts.next()?.parse::<u8>().ok()?;
    let col = parts.next()?.parse::<u8>().ok()?;

    Some(DeviceButtonEvent { index, row, col })
}

fn read_listener_line(
    port: &mut dyn SerialPort,
    buffer: &mut Vec<u8>,
) -> Result<Option<String>, String> {
    let mut byte = [0_u8; 1];

    match port.read(&mut byte) {
        Ok(1) => {
            let b = byte[0];
            if b == b'\r' {
                return Ok(None);
            }

            if b == b'\n' {
                if buffer.is_empty() {
                    return Ok(None);
                }

                let line = String::from_utf8_lossy(buffer).trim().to_string();
                buffer.clear();
                if line.is_empty() {
                    return Ok(None);
                }

                return Ok(Some(line));
            }

            buffer.push(b);
            if buffer.len() > 2048 {
                buffer.clear();
                return Err("Received an oversized serial listener line".to_string());
            }
            Ok(None)
        }
        Ok(_) => Ok(None),
        Err(ref e) if e.kind() == io::ErrorKind::TimedOut => Ok(None),
        Err(e) => Err(format!("Serial listener read failed: {e}")),
    }
}

fn execute_host_actions(
    app_handle: AppHandle,
    companion: CompanionState,
    button: DeviceButtonEvent,
    actions: Vec<HostCommandAction>,
) {
    let button_label = format!("button {} ({},{})", button.index, button.row, button.col);
    if actions.is_empty() {
        update_companion_status(&app_handle, &companion, |status| {
            status.last_execution = Some(format!("No host actions configured for {button_label}"));
        });
        return;
    }

    for action in actions {
        let label = if action.label.trim().is_empty() {
            action.program.clone()
        } else {
            action.label.clone()
        };

        if action.program.trim().is_empty() {
            let message = format!("Skipped empty host action for {button_label}");
            update_companion_status(&app_handle, &companion, |status| {
                status.last_execution = Some(message.clone());
                status.last_error = Some(message.clone());
            });
            emit_companion_log(&app_handle, message);
            continue;
        }

        emit_companion_log(
            &app_handle,
            format!("Running host action '{label}' for {button_label}"),
        );

        let mut command = Command::new(&action.program);
        command.args(&action.args);
        if let Some(cwd) = action.cwd.as_ref().filter(|cwd| !cwd.trim().is_empty()) {
            command.current_dir(cwd);
        }

        let result = if action.run_detached {
            command
                .spawn()
                .map(|_| "Started detached process".to_string())
        } else {
            command.output().map(|output| {
                if output.status.success() {
                    let stdout = String::from_utf8_lossy(&output.stdout).trim().to_string();
                    if stdout.is_empty() {
                        "Completed successfully".to_string()
                    } else {
                        format!("Completed successfully: {stdout}")
                    }
                } else {
                    let stderr = String::from_utf8_lossy(&output.stderr).trim().to_string();
                    if stderr.is_empty() {
                        format!("Exited with status {}", output.status)
                    } else {
                        format!("Exited with status {}: {stderr}", output.status)
                    }
                }
            })
        };

        match result {
            Ok(summary) => {
                let message = format!("{label}: {summary}");
                update_companion_status(&app_handle, &companion, |status| {
                    status.last_execution = Some(message.clone());
                    status.last_error = None;
                });
                emit_companion_log(&app_handle, message);
            }
            Err(error) => {
                let message = format!("{label}: {error}");
                update_companion_status(&app_handle, &companion, |status| {
                    status.last_execution = Some(message.clone());
                    status.last_error = Some(message.clone());
                });
                emit_companion_log(&app_handle, message);
            }
        }
    }
}

fn handle_listener_line(app_handle: &AppHandle, companion: &CompanionState, line: &str) {
    if let Some(event) = parse_button_event(line) {
        let actions = {
            let shared = companion.shared.lock().unwrap();
            shared
                .host_actions
                .get(&event.index)
                .cloned()
                .unwrap_or_default()
        };

        update_companion_status(app_handle, companion, |status| {
            status.last_event = Some(format!("Button {} pressed", event.index));
        });
        let _ = app_handle.emit_all(DEVICE_BUTTON_EVENT, event.clone());

        let app_handle_clone = app_handle.clone();
        let companion_clone = companion.clone();
        thread::spawn(move || {
            execute_host_actions(app_handle_clone, companion_clone, event, actions)
        });
        return;
    }

    if line.starts_with("CDC:") {
        emit_companion_log(app_handle, format!("Device: {line}"));
    }
}

fn companion_listener_loop(app_handle: AppHandle, companion: CompanionState) {
    let mut line_buffer: Vec<u8> = Vec::with_capacity(256);

    loop {
        let (settings, paused, shutdown) = companion_settings_snapshot(&companion);
        if shutdown {
            break;
        }

        if paused || !settings.listener_enabled || settings.port_name.trim().is_empty() {
            let should_emit = {
                let mut shared = companion.shared.lock().unwrap();
                shared.port_open = false;

                let status_was_connected = shared.status.connected;
                let status_had_active_port = shared.status.active_port.is_some();

                shared.status.connected = false;
                shared.status.active_port = None;

                status_was_connected || status_had_active_port
            };

            if should_emit {
                emit_companion_status(&app_handle, &companion);
            }
            thread::sleep(Duration::from_millis(LISTENER_IDLE_SLEEP_MS));
            continue;
        }

        let mut port = match open_serial_port(&settings.port_name, settings.baud_rate) {
            Ok(port) => port,
            Err(err) => {
                set_companion_last_error(&app_handle, &companion, err);
                thread::sleep(Duration::from_millis(LISTENER_RECONNECT_DELAY_MS));
                continue;
            }
        };

        let (latest_settings, paused, shutdown) = companion_settings_snapshot(&companion);
        if shutdown
            || paused
            || latest_settings.port_name != settings.port_name
            || latest_settings.baud_rate != settings.baud_rate
            || latest_settings.listener_enabled != settings.listener_enabled
        {
            continue;
        }

        {
            let mut shared = companion.shared.lock().unwrap();
            shared.port_open = true;
        }

        let mut handshake_logs = Vec::new();
        if let Err(err) = drain_pending_lines(port.as_mut(), &mut handshake_logs)
            .and_then(|_| listener_handshake(port.as_mut(), &mut handshake_logs))
        {
            {
                let mut shared = companion.shared.lock().unwrap();
                shared.port_open = false;
            }
            set_companion_last_error(&app_handle, &companion, err);
            thread::sleep(Duration::from_millis(LISTENER_RECONNECT_DELAY_MS));
            continue;
        }

        set_companion_connected(
            &app_handle,
            &companion,
            true,
            Some(settings.port_name.clone()),
        );
        line_buffer.clear();

        loop {
            let (latest_settings, paused, shutdown) = companion_settings_snapshot(&companion);
            if shutdown || paused {
                break;
            }

            if latest_settings.port_name != settings.port_name
                || latest_settings.baud_rate != settings.baud_rate
                || latest_settings.listener_enabled != settings.listener_enabled
            {
                break;
            }

            match read_listener_line(port.as_mut(), &mut line_buffer) {
                Ok(Some(line)) => handle_listener_line(&app_handle, &companion, &line),
                Ok(None) => {}
                Err(err) => {
                    set_companion_last_error(&app_handle, &companion, err);
                    break;
                }
            }
        }

        {
            let mut shared = companion.shared.lock().unwrap();
            shared.port_open = false;
            shared.status.connected = false;
            shared.status.active_port = None;
        }
        emit_companion_status(&app_handle, &companion);
    }
}

#[tauri::command]
fn get_companion_status(state: State<'_, CompanionState>) -> CompanionStatus {
    state.inner().clone().snapshot_status()
}

#[tauri::command]
fn configure_companion(
    app_handle: AppHandle,
    state: State<'_, CompanionState>,
    port_name: String,
    baud_rate: u32,
    listener_enabled: bool,
) -> Result<CompanionStatus, String> {
    let companion = state.inner().clone();
    {
        let mut shared = companion.shared.lock().unwrap();
        shared.settings.port_name = port_name.clone();
        shared.settings.baud_rate = baud_rate;
        shared.settings.listener_enabled = listener_enabled;
        shared.status.port_name = port_name;
        shared.status.baud_rate = baud_rate;
        shared.status.listener_enabled = listener_enabled;
    }

    save_companion_settings(&companion)?;
    emit_companion_status(&app_handle, &companion);
    update_tray_menu(&app_handle, listener_enabled);
    Ok(companion.snapshot_status())
}

#[tauri::command]
fn cache_companion_macros(
    app_handle: AppHandle,
    state: State<'_, CompanionState>,
    macros_json: String,
) -> Result<CompanionStatus, String> {
    let companion = state.inner().clone();
    update_host_actions_cache(&companion, &macros_json)?;
    emit_companion_status(&app_handle, &companion);
    Ok(companion.snapshot_status())
}

#[tauri::command]
fn list_serial_ports() -> Result<Vec<String>, String> {
    let ports =
        serialport::available_ports().map_err(|e| format!("Failed to list serial ports: {e}"))?;
    Ok(ports.into_iter().map(|p| p.port_name).collect())
}

fn send_line(port: &mut dyn SerialPort, line: &str, logs: &mut Vec<String>) -> Result<(), String> {
    logs.push(format!("> {line}"));
    port.write_all(line.as_bytes())
        .map_err(|e| format!("Serial write failed: {e}"))?;
    port.write_all(b"\n")
        .map_err(|e| format!("Serial newline write failed: {e}"))?;
    port.flush()
        .map_err(|e| format!("Serial flush failed: {e}"))?;
    Ok(())
}

fn open_serial_port(port_name: &str, baud_rate: u32) -> Result<Box<dyn SerialPort>, String> {
    let mut port = serialport::new(port_name, baud_rate)
        .timeout(Duration::from_millis(250))
        .open()
        .map_err(|e| format!("Failed to open {}: {e}", port_name))?;

    let _ = port.write_data_terminal_ready(true);
    let _ = port.write_request_to_send(false);
    thread::sleep(Duration::from_millis(PORT_SETTLE_MS));
    let _ = port.clear(ClearBuffer::Input);
    Ok(port)
}

fn contains_case_insensitive(value: &str, needle: &str) -> bool {
    value
        .to_ascii_lowercase()
        .contains(&needle.to_ascii_lowercase())
}

fn serial_port_preference_score(info: &SerialPortInfo) -> i32 {
    match &info.port_type {
        SerialPortType::UsbPort(usb) => {
            let mut score = 10;

            if usb.vid == 0x303a {
                score += 30;
            }

            if usb.product.as_deref().map_or(false, |product| {
                contains_case_insensitive(product, "Finlay Deck")
            }) {
                score += 100;
            }

            if usb.manufacturer.as_deref().map_or(false, |manufacturer| {
                contains_case_insensitive(manufacturer, "Finlay")
                    || contains_case_insensitive(manufacturer, "Espressif")
            }) {
                score += 20;
            }

            if usb
                .product
                .as_deref()
                .map_or(false, |product| contains_case_insensitive(product, "ESP32"))
            {
                score += 15;
            }

            score
        }
        _ => 0,
    }
}

fn read_line_until(port: &mut dyn SerialPort, deadline: Instant) -> Result<String, String> {
    let mut buffer: Vec<u8> = Vec::with_capacity(128);

    loop {
        if Instant::now() >= deadline {
            return Err("Timeout waiting for device response".to_string());
        }

        let mut byte = [0_u8; 1];
        match port.read(&mut byte) {
            Ok(1) => {
                let b = byte[0];
                if b == b'\r' {
                    continue;
                }

                if b == b'\n' {
                    if buffer.is_empty() {
                        continue;
                    }

                    let line = String::from_utf8_lossy(&buffer).trim().to_string();
                    if line.is_empty() {
                        buffer.clear();
                        continue;
                    }
                    return Ok(line);
                }

                buffer.push(b);
                if buffer.len() > 2048 {
                    buffer.clear();
                    return Err("Received an oversized serial line".to_string());
                }
            }
            Ok(_) => {}
            Err(ref e) if e.kind() == io::ErrorKind::TimedOut => {}
            Err(e) => {
                return Err(format!("Serial read failed: {e}"));
            }
        }
    }
}

fn read_line_until_optional(
    port: &mut dyn SerialPort,
    deadline: Instant,
) -> Result<Option<String>, String> {
    match read_line_until(port, deadline) {
        Ok(line) => Ok(Some(line)),
        Err(err) if err.contains("Timeout waiting for device response") => Ok(None),
        Err(err) => Err(err),
    }
}

fn drain_pending_lines(port: &mut dyn SerialPort, logs: &mut Vec<String>) -> Result<(), String> {
    let deadline = Instant::now() + Duration::from_millis(DRAIN_WINDOW_MS);

    while Instant::now() < deadline {
        let Some(line) = read_line_until_optional(port, deadline)? else {
            break;
        };
        logs.push(format!("<(drain) {line}"));
    }

    Ok(())
}

fn handshake_with_fallback(
    port: &mut dyn SerialPort,
    logs: &mut Vec<String>,
    total_timeout_ms: u64,
) -> Result<(), String> {
    let attempts: [(&str, u64); 3] = [("PING", total_timeout_ms), ("STATUS", 2000), ("PING", 2000)];
    handshake_with_attempts(port, logs, &attempts)
}

fn listener_handshake(port: &mut dyn SerialPort, logs: &mut Vec<String>) -> Result<(), String> {
    let attempts: [(&str, u64); 3] = [
        ("PING", LISTENER_HANDSHAKE_TIMEOUT_MS),
        ("STATUS", LISTENER_HANDSHAKE_TIMEOUT_MS),
        ("PING", LISTENER_HANDSHAKE_TIMEOUT_MS),
    ];
    handshake_with_attempts(port, logs, &attempts)
}

fn handshake_with_attempts(
    port: &mut dyn SerialPort,
    logs: &mut Vec<String>,
    attempts: &[(&str, u64)],
) -> Result<(), String> {
    for &(command, timeout_ms) in attempts {
        send_line(port, command, logs)?;

        let deadline = Instant::now() + Duration::from_millis(timeout_ms);
        while Instant::now() < deadline {
            let Some(line) = read_line_until_optional(port, deadline)? else {
                break;
            };

            logs.push(format!("< {line}"));

            if line.starts_with("CDC:PONG") || line.starts_with("CDC:STATUS") {
                return Ok(());
            }
        }
    }

    Err("Timeout waiting for device response".to_string())
}

fn wait_for<F>(
    port: &mut dyn SerialPort,
    timeout: Duration,
    logs: &mut Vec<String>,
    predicate: F,
) -> Result<String, String>
where
    F: Fn(&str) -> bool,
{
    let deadline = Instant::now() + timeout;

    loop {
        let line = read_line_until(port, deadline)?;
        logs.push(format!("< {line}"));

        if line.starts_with("CDC:ERR") {
            return Err(format!("Device reported error: {line}"));
        }

        if predicate(&line) {
            return Ok(line);
        }
    }
}

fn probe_serial_ports_impl(baud_rate: u32) -> Result<ProbePortsResult, String> {
    let port_infos =
        serialport::available_ports().map_err(|e| format!("Failed to list serial ports: {e}"))?;

    let mut logs: Vec<String> = Vec::new();
    let mut ports: Vec<ProbePortResult> = Vec::new();

    let mut scored_ports: Vec<(i32, SerialPortInfo)> = port_infos
        .into_iter()
        .map(|info| (serial_port_preference_score(&info), info))
        .collect();
    scored_ports.sort_by(|(left_score, left_info), (right_score, right_info)| {
        right_score
            .cmp(left_score)
            .then_with(|| left_info.port_name.cmp(&right_info.port_name))
    });

    let mut best_responsive: Option<(i32, String)> = None;

    for (score, info) in scored_ports {
        let name = info.port_name;
        logs.push(format!("Probing {name}"));

        let mut probe_logs: Vec<String> = vec![format!("Opening {name} at {baud_rate} baud")];
        match open_serial_port(&name, baud_rate) {
            Ok(mut port) => {
                if let Err(err) = drain_pending_lines(port.as_mut(), &mut probe_logs) {
                    probe_logs.push(format!("Drain warning: {err}"));
                }

                match handshake_with_fallback(
                    port.as_mut(),
                    &mut probe_logs,
                    PROBE_HANDSHAKE_TIMEOUT_MS,
                ) {
                    Ok(()) => {
                        if match best_responsive.as_ref() {
                            Some((best_score, _)) => score > *best_score,
                            None => true,
                        } {
                            best_responsive = Some((score, name.clone()));
                        }

                        ports.push(ProbePortResult {
                            port_name: name,
                            responsive: true,
                            detail: "CDC protocol responsive".to_string(),
                        });
                    }
                    Err(err) => {
                        ports.push(ProbePortResult {
                            port_name: name,
                            responsive: false,
                            detail: err,
                        });
                    }
                }
            }
            Err(err) => {
                ports.push(ProbePortResult {
                    port_name: name,
                    responsive: false,
                    detail: err,
                });
            }
        }

        logs.extend(probe_logs);
    }

    let suggested_port = best_responsive.map(|(_, port_name)| port_name);

    Ok(ProbePortsResult {
        ports,
        suggested_port,
        logs,
    })
}

#[tauri::command]
async fn probe_serial_ports(
    app_handle: AppHandle,
    state: State<'_, CompanionState>,
    baud_rate: u32,
) -> Result<ProbePortsResult, String> {
    let companion = state.inner().clone();
    tauri::async_runtime::spawn_blocking(move || {
        let _pause = pause_companion_listener(&app_handle, &companion)?;
        probe_serial_ports_impl(baud_rate)
    })
    .await
    .map_err(|e| format!("Background task failed: {e}"))?
}

fn upload_file(
    port: &mut dyn SerialPort,
    remote_path: &str,
    data: &[u8],
    logs: &mut Vec<String>,
    progress: Option<&UploadProgressTracker<'_>>,
) -> Result<(), String> {
    if data.is_empty() {
        return Err(format!("Refusing zero-byte upload for {remote_path}"));
    }

    if data.len() > MAX_UPLOAD_BYTES {
        return Err(format!(
            "Refusing upload over protocol limit ({}) for {remote_path}",
            MAX_UPLOAD_BYTES
        ));
    }

    for attempt in 0..=UPLOAD_ATTEMPT_RETRY_COUNT {
        if let Some(progress) = progress {
            progress.emit("uploading", 0);
        }

        let put_cmd = format!("PUT {remote_path} {}", data.len());
        send_line(port, &put_cmd, logs)?;

        let ready_prefix = format!("CDC:READY PUT {remote_path}");
        if let Err(err) = wait_for(port, Duration::from_secs(8), logs, |line| {
            line.starts_with(&ready_prefix)
        }) {
            if attempt < UPLOAD_ATTEMPT_RETRY_COUNT {
                logs.push(format!(
                    "Retrying upload (attempt {}/{}) after READY wait failure: {}",
                    attempt + 1,
                    UPLOAD_ATTEMPT_RETRY_COUNT,
                    err
                ));
                let _ = drain_pending_lines(port, logs);
                continue;
            }
            return Err(err);
        }

        let previous_timeout = port.timeout();
        let _ = port.set_timeout(Duration::from_millis(UPLOAD_IO_TIMEOUT_MS));

        let stream_result = stream_payload_chunked(port, data, logs, progress)
            .map_err(|e| format!("Failed to stream payload to {remote_path}: {e}"));

        let _ = port.set_timeout(previous_timeout);
        if let Err(err) = stream_result {
            if attempt < UPLOAD_ATTEMPT_RETRY_COUNT {
                logs.push(format!(
                    "Retrying upload (attempt {}/{}) after stream failure: {}",
                    attempt + 1,
                    UPLOAD_ATTEMPT_RETRY_COUNT,
                    err
                ));
                let _ = drain_pending_lines(port, logs);
                continue;
            }
            return Err(err);
        }

        let ok_prefix = format!("CDC:OK PUT {remote_path}");
        match wait_for(
            port,
            Duration::from_secs(UPLOAD_OK_WAIT_TIMEOUT_SECS),
            logs,
            |line| line.starts_with(&ok_prefix),
        ) {
            Ok(_) => {
                if let Some(progress) = progress {
                    progress.emit("uploading", data.len());
                }
                return Ok(());
            }
            Err(err) => {
                if attempt < UPLOAD_ATTEMPT_RETRY_COUNT {
                    logs.push(format!(
                        "Retrying upload (attempt {}/{}) after OK wait failure: {}",
                        attempt + 1,
                        UPLOAD_ATTEMPT_RETRY_COUNT,
                        err
                    ));
                    let _ = drain_pending_lines(port, logs);
                    continue;
                }
                return Err(err);
            }
        }
    }

    Err(format!("Upload retries exhausted for {remote_path}"))
}

fn is_transient_write_error(error: &io::Error) -> bool {
    error.kind() == io::ErrorKind::TimedOut || error.raw_os_error() == Some(121)
}

fn stream_payload_chunked(
    port: &mut dyn SerialPort,
    data: &[u8],
    logs: &mut Vec<String>,
    progress: Option<&UploadProgressTracker<'_>>,
) -> Result<(), String> {
    let total = data.len();
    let mut sent = 0_usize;
    let mut next_progress_mark = 4096_usize;

    while sent < total {
        let chunk_end = (sent + UPLOAD_CHUNK_SIZE).min(total);
        let mut chunk_offset = sent;

        while chunk_offset < chunk_end {
            let slice = &data[chunk_offset..chunk_end];
            let mut success = false;

            for attempt in 0..=UPLOAD_WRITE_RETRY_COUNT {
                match port.write(slice) {
                    Ok(0) => {
                        if attempt >= UPLOAD_WRITE_RETRY_COUNT {
                            return Err("write returned 0 bytes repeatedly".to_string());
                        }
                        thread::sleep(Duration::from_millis(UPLOAD_RETRY_SLEEP_MS));
                    }
                    Ok(bytes_written) => {
                        chunk_offset += bytes_written;
                        success = true;
                        break;
                    }
                    Err(error) => {
                        if is_transient_write_error(&error) && attempt < UPLOAD_WRITE_RETRY_COUNT {
                            thread::sleep(Duration::from_millis(UPLOAD_RETRY_SLEEP_MS));
                            continue;
                        }

                        return Err(error.to_string());
                    }
                }
            }

            if !success {
                return Err("write failed after retries".to_string());
            }
        }

        sent = chunk_end;
        flush_serial_port(port)?;
        if UPLOAD_INTER_CHUNK_DELAY_MS > 0 && sent < total {
            thread::sleep(Duration::from_millis(UPLOAD_INTER_CHUNK_DELAY_MS));
        }

        if sent >= next_progress_mark || sent == total {
            logs.push(format!("payload progress: {sent}/{total}"));
            if let Some(progress) = progress {
                progress.emit("uploading", sent);
            }
            next_progress_mark += 4096;
        }
    }

    flush_serial_port(port)
}

fn flush_serial_port(port: &mut dyn SerialPort) -> Result<(), String> {
    for attempt in 0..=UPLOAD_WRITE_RETRY_COUNT {
        match port.flush() {
            Ok(()) => return Ok(()),
            Err(error) => {
                if is_transient_write_error(&error) && attempt < UPLOAD_WRITE_RETRY_COUNT {
                    thread::sleep(Duration::from_millis(UPLOAD_RETRY_SLEEP_MS));
                    continue;
                }

                return Err(format!("flush failed: {error}"));
            }
        }
    }

    Err("flush failed after retries".to_string())
}

fn parse_ready_get_line(line: &str, remote_path: &str) -> Result<usize, String> {
    let parts: Vec<&str> = line.split_whitespace().collect();
    if parts.len() != 4 || parts[0] != "CDC:READY" || parts[1] != "GET" {
        return Err(format!("Unexpected GET READY line: {line}"));
    }

    if parts[2] != remote_path {
        return Err(format!(
            "GET READY path mismatch (expected {}, got {})",
            remote_path, parts[2]
        ));
    }

    let size = parts[3]
        .parse::<usize>()
        .map_err(|_| format!("Invalid GET size in line: {line}"))?;
    if size > MAX_UPLOAD_BYTES {
        return Err(format!(
            "Refusing GET over protocol limit ({}) for {}",
            MAX_UPLOAD_BYTES, remote_path
        ));
    }

    Ok(size)
}

fn wait_for_get_ready_or_not_found(
    port: &mut dyn SerialPort,
    remote_path: &str,
    timeout: Duration,
    logs: &mut Vec<String>,
) -> Result<Option<usize>, String> {
    let deadline = Instant::now() + timeout;

    loop {
        let line = read_line_until(port, deadline)?;
        logs.push(format!("< {line}"));

        if line.starts_with("CDC:READY GET") {
            let size = parse_ready_get_line(&line, remote_path)?;
            return Ok(Some(size));
        }

        if line.starts_with("CDC:ERR GET NOT_FOUND") {
            return Ok(None);
        }

        if line.starts_with("CDC:ERR") {
            return Err(format!("Device reported error: {line}"));
        }
    }
}

fn read_exact_payload(
    port: &mut dyn SerialPort,
    expected_size: usize,
    logs: &mut Vec<String>,
) -> Result<Vec<u8>, String> {
    let mut payload = vec![0_u8; expected_size];
    let mut received = 0_usize;
    let mut next_progress_mark = 4096_usize;
    let deadline = Instant::now() + Duration::from_secs(DOWNLOAD_READ_TIMEOUT_SECS);

    while received < expected_size {
        if Instant::now() >= deadline {
            return Err(format!(
                "Timed out receiving binary payload ({}/{})",
                received, expected_size
            ));
        }

        match port.read(&mut payload[received..]) {
            Ok(0) => {}
            Ok(bytes_read) => {
                received += bytes_read;
                if received >= next_progress_mark || received == expected_size {
                    logs.push(format!(
                        "download payload progress: {received}/{expected_size}"
                    ));
                    next_progress_mark += 4096;
                }
            }
            Err(ref e) if e.kind() == io::ErrorKind::TimedOut => {}
            Err(e) => return Err(format!("Serial read failed while downloading payload: {e}")),
        }
    }

    Ok(payload)
}

fn download_file(
    port: &mut dyn SerialPort,
    remote_path: &str,
    logs: &mut Vec<String>,
) -> Result<Option<Vec<u8>>, String> {
    let get_cmd = format!("GET {remote_path}");
    send_line(port, &get_cmd, logs)?;

    let Some(size) = wait_for_get_ready_or_not_found(
        port,
        remote_path,
        Duration::from_secs(DOWNLOAD_READY_WAIT_TIMEOUT_SECS),
        logs,
    )?
    else {
        return Ok(None);
    };

    let previous_timeout = port.timeout();
    let _ = port.set_timeout(Duration::from_millis(UPLOAD_IO_TIMEOUT_MS));
    let payload = read_exact_payload(port, size, logs);
    let _ = port.set_timeout(previous_timeout);
    let payload = payload?;

    let ok_prefix = format!("CDC:OK GET {remote_path}");
    wait_for(
        port,
        Duration::from_secs(DOWNLOAD_OK_WAIT_TIMEOUT_SECS),
        logs,
        |line| line.starts_with(&ok_prefix),
    )?;

    Ok(Some(payload))
}

fn send_updates_impl(
    app_handle: AppHandle,
    request: SendUpdatesRequest,
) -> Result<SendUpdatesResult, String> {
    if request.port_name.trim().is_empty() {
        return Err("A serial port must be selected".to_string());
    }

    let mut logs = vec![format!(
        "Opening {} at {} baud",
        request.port_name, request.baud_rate
    )];

    let mut port = match open_serial_port(&request.port_name, request.baud_rate) {
        Ok(port) => port,
        Err(err) => {
            logs.push(format!("ERROR: {err}"));
            return Err(format!("{err}\n---LOGS---\n{}", logs.join("\n")));
        }
    };

    if let Err(err) = drain_pending_lines(port.as_mut(), &mut logs) {
        logs.push(format!("Drain warning: {err}"));
    }

    if let Err(err) = handshake_with_fallback(port.as_mut(), &mut logs, HANDSHAKE_TOTAL_TIMEOUT_MS)
    {
        logs.push(format!("ERROR: {err}"));
        return Err(format!("{err}\n---LOGS---\n{}", logs.join("\n")));
    }

    let total_bytes = request
        .icon_uploads
        .iter()
        .map(|icon| icon.bytes.len())
        .sum::<usize>()
        + request
            .macros_json
            .as_ref()
            .map(|macros_json| macros_json.as_bytes().len())
            .unwrap_or(0);
    let file_count = request.icon_uploads.len() + usize::from(request.macros_json.is_some());
    let mut completed_bytes = 0_usize;

    if total_bytes > 0 {
        let _ = app_handle.emit_all(
            UPLOAD_PROGRESS_EVENT,
            UploadProgressEvent {
                phase: "preparing".to_string(),
                current_bytes: 0,
                total_bytes,
                file_bytes_sent: 0,
                file_total_bytes: 0,
                file_path: String::new(),
                file_index: 0,
                file_count,
            },
        );
    }

    for (file_offset, icon) in request.icon_uploads.iter().enumerate() {
        if icon.row >= GRID_ROWS || icon.col >= GRID_COLS {
            return Err(format!(
                "Icon {} has invalid row/col ({}, {})",
                icon.index, icon.row, icon.col
            ));
        }

        if icon.bytes.len() != ICON_BYTE_SIZE {
            return Err(format!(
                "Icon {} size mismatch (got {}, expected {})",
                icon.index,
                icon.bytes.len(),
                ICON_BYTE_SIZE
            ));
        }

        let remote = format!("/icon_{}_{}.bin", icon.row, icon.col);
        logs.push(format!("Uploading icon {} -> {}", icon.index, remote));
        let progress = UploadProgressTracker {
            app_handle: &app_handle,
            total_bytes,
            completed_bytes,
            file_total_bytes: icon.bytes.len(),
            file_index: file_offset + 1,
            file_count,
            file_path: &remote,
        };
        if let Err(err) = upload_file(
            port.as_mut(),
            &remote,
            &icon.bytes,
            &mut logs,
            Some(&progress),
        ) {
            logs.push(format!("ERROR: {err}"));
            return Err(format!("{err}\n---LOGS---\n{}", logs.join("\n")));
        }
        completed_bytes += icon.bytes.len();
    }

    if let Some(macros_json) = request.macros_json.as_ref() {
        let bytes = macros_json.as_bytes();
        logs.push(format!("Uploading macros.json ({} bytes)", bytes.len()));
        let progress = UploadProgressTracker {
            app_handle: &app_handle,
            total_bytes,
            completed_bytes,
            file_total_bytes: bytes.len(),
            file_index: request.icon_uploads.len() + 1,
            file_count,
            file_path: "/macros.json",
        };
        if let Err(err) = upload_file(
            port.as_mut(),
            "/macros.json",
            bytes,
            &mut logs,
            Some(&progress),
        ) {
            logs.push(format!("ERROR: {err}"));
            return Err(format!("{err}\n---LOGS---\n{}", logs.join("\n")));
        }
    }

    if total_bytes > 0 {
        let _ = app_handle.emit_all(
            UPLOAD_PROGRESS_EVENT,
            UploadProgressEvent {
                phase: "finalizing".to_string(),
                current_bytes: total_bytes,
                total_bytes,
                file_bytes_sent: 0,
                file_total_bytes: 0,
                file_path: String::new(),
                file_index: file_count,
                file_count,
            },
        );
    }

    if request.send_reload_all {
        if let Err(err) = send_line(port.as_mut(), "RELOAD ALL", &mut logs) {
            logs.push(format!("ERROR: {err}"));
            return Err(format!("{err}\n---LOGS---\n{}", logs.join("\n")));
        }
        if let Err(err) = wait_for(port.as_mut(), Duration::from_secs(6), &mut logs, |line| {
            line.starts_with("CDC:OK RELOAD ALL")
        }) {
            logs.push(format!("ERROR: {err}"));
            return Err(format!("{err}\n---LOGS---\n{}", logs.join("\n")));
        }
    }

    logs.push("Done".to_string());

    if total_bytes > 0 {
        let _ = app_handle.emit_all(
            UPLOAD_PROGRESS_EVENT,
            UploadProgressEvent {
                phase: "complete".to_string(),
                current_bytes: total_bytes,
                total_bytes,
                file_bytes_sent: 0,
                file_total_bytes: 0,
                file_path: String::new(),
                file_index: file_count,
                file_count,
            },
        );
    }

    Ok(SendUpdatesResult { logs })
}

#[tauri::command]
async fn send_updates(
    app_handle: AppHandle,
    state: State<'_, CompanionState>,
    request: SendUpdatesRequest,
) -> Result<SendUpdatesResult, String> {
    let companion = state.inner().clone();
    let macros_for_cache = request.macros_json.clone();
    let app_handle_for_task = app_handle.clone();
    let result = tauri::async_runtime::spawn_blocking(move || {
        let _pause = pause_companion_listener(&app_handle_for_task, &companion)?;
        send_updates_impl(app_handle_for_task, request)
    })
    .await
    .map_err(|e| format!("Background task failed: {e}"))??;

    if let Some(macros_json) = macros_for_cache.as_ref() {
        let companion = state.inner().clone();
        update_host_actions_cache(&companion, macros_json)?;
        emit_companion_status(&app_handle, &companion);
    }

    Ok(result)
}

fn sync_from_device_impl(request: SyncFromDeviceRequest) -> Result<SyncFromDeviceResult, String> {
    if request.port_name.trim().is_empty() {
        return Err("A serial port must be selected".to_string());
    }

    let mut logs = vec![format!(
        "Opening {} at {} baud",
        request.port_name, request.baud_rate
    )];

    let mut port = match open_serial_port(&request.port_name, request.baud_rate) {
        Ok(port) => port,
        Err(err) => {
            logs.push(format!("ERROR: {err}"));
            return Err(format!("{err}\n---LOGS---\n{}", logs.join("\n")));
        }
    };

    if let Err(err) = drain_pending_lines(port.as_mut(), &mut logs) {
        logs.push(format!("Drain warning: {err}"));
    }

    if let Err(err) = handshake_with_fallback(port.as_mut(), &mut logs, HANDSHAKE_TOTAL_TIMEOUT_MS)
    {
        logs.push(format!("ERROR: {err}"));
        return Err(format!("{err}\n---LOGS---\n{}", logs.join("\n")));
    }

    logs.push("Downloading /macros.json".to_string());
    let macros_bytes = match download_file(port.as_mut(), "/macros.json", &mut logs) {
        Ok(Some(bytes)) => bytes,
        Ok(None) => {
            let err = "Device is missing /macros.json".to_string();
            logs.push(format!("ERROR: {err}"));
            return Err(format!("{err}\n---LOGS---\n{}", logs.join("\n")));
        }
        Err(err) => {
            logs.push(format!("ERROR: {err}"));
            return Err(format!("{err}\n---LOGS---\n{}", logs.join("\n")));
        }
    };

    let macros_json = match String::from_utf8(macros_bytes) {
        Ok(text) => text,
        Err(e) => {
            let err = format!("macros.json is not valid UTF-8: {e}");
            logs.push(format!("ERROR: {err}"));
            return Err(format!("{err}\n---LOGS---\n{}", logs.join("\n")));
        }
    };

    logs.push("Downloading /fallback.bin (optional)".to_string());
    let mut fallback_bytes = match download_file(port.as_mut(), "/fallback.bin", &mut logs) {
        Ok(bytes) => bytes,
        Err(err) => {
            logs.push(format!("ERROR: {err}"));
            return Err(format!("{err}\n---LOGS---\n{}", logs.join("\n")));
        }
    };

    if let Some(bytes) = fallback_bytes.as_ref() {
        if bytes.len() != ICON_BYTE_SIZE {
            logs.push(format!(
                "Ignoring fallback.bin due to size mismatch (got {}, expected {})",
                bytes.len(),
                ICON_BYTE_SIZE
            ));
            fallback_bytes = None;
        }
    }

    let mut icon_downloads: Vec<IconDownload> = Vec::new();
    for row in 0..GRID_ROWS {
        for col in 0..GRID_COLS {
            let remote = format!("/icon_{}_{}.bin", row, col);
            logs.push(format!("Downloading {} (optional)", remote));

            match download_file(port.as_mut(), &remote, &mut logs) {
                Ok(Some(bytes)) => {
                    if bytes.len() != ICON_BYTE_SIZE {
                        logs.push(format!(
                            "Ignoring {} due to size mismatch (got {}, expected {})",
                            remote,
                            bytes.len(),
                            ICON_BYTE_SIZE
                        ));
                        continue;
                    }

                    icon_downloads.push(IconDownload { row, col, bytes });
                }
                Ok(None) => {}
                Err(err) => {
                    logs.push(format!("ERROR: {err}"));
                    return Err(format!("{err}\n---LOGS---\n{}", logs.join("\n")));
                }
            }
        }
    }

    logs.push("Done".to_string());
    Ok(SyncFromDeviceResult {
        logs,
        macros_json,
        fallback_bytes,
        icon_downloads,
    })
}

#[tauri::command]
async fn sync_from_device(
    app_handle: AppHandle,
    state: State<'_, CompanionState>,
    request: SyncFromDeviceRequest,
) -> Result<SyncFromDeviceResult, String> {
    let companion = state.inner().clone();
    let app_handle_for_task = app_handle.clone();
    let result = tauri::async_runtime::spawn_blocking(move || {
        let _pause = pause_companion_listener(&app_handle_for_task, &companion)?;
        sync_from_device_impl(request)
    })
    .await
    .map_err(|e| format!("Background task failed: {e}"))??;

    let companion = state.inner().clone();
    update_host_actions_cache(&companion, &result.macros_json)?;
    emit_companion_status(&app_handle, &companion);
    Ok(result)
}

fn companion_storage_dir(app_handle: &AppHandle) -> Result<PathBuf, String> {
    let path = app_handle
        .path_resolver()
        .app_data_dir()
        .or_else(|| app_handle.path_resolver().app_config_dir())
        .or_else(|| std::env::current_dir().ok())
        .ok_or_else(|| "Failed to resolve companion data directory".to_string())?;

    fs::create_dir_all(&path)
        .map_err(|e| format!("Failed to create companion data directory: {e}"))?;
    Ok(path)
}

fn build_system_tray() -> SystemTray {
    let show = CustomMenuItem::new("show".to_string(), "Show Deck Macro Editor");
    let toggle_listener = CustomMenuItem::new("toggle-listener".to_string(), "Pause Listener");
    let quit = CustomMenuItem::new("quit".to_string(), "Quit");
    let menu = SystemTrayMenu::new()
        .add_item(show)
        .add_item(toggle_listener)
        .add_item(quit);

    SystemTray::new().with_menu(menu)
}

fn update_tray_menu(app_handle: &AppHandle, listener_enabled: bool) {
    let title = if listener_enabled {
        "Pause Listener"
    } else {
        "Resume Listener"
    };
    let _ = app_handle
        .tray_handle()
        .get_item("toggle-listener")
        .set_title(title);
}

fn show_main_window(app_handle: &AppHandle) {
    if let Some(window) = app_handle.get_window("main") {
        let _ = window.show();
        let _ = window.set_focus();
    }
}

fn set_companion_shutdown(companion: &CompanionState) {
    let mut shared = companion.shared.lock().unwrap();
    shared.shutdown = true;
}

fn setup_companion(app_handle: &AppHandle) -> Result<CompanionState, String> {
    #[cfg(not(debug_assertions))]
    {
        let _ = app_handle.autolaunch().enable();
    }

    let autostart_enabled = app_handle.autolaunch().is_enabled().unwrap_or(false);
    let storage_dir = companion_storage_dir(app_handle)?;
    let companion = CompanionState::new(
        storage_dir.join(COMPANION_SETTINGS_FILE),
        storage_dir.join(COMPANION_MACROS_FILE),
        autostart_enabled,
    );

    let worker_handle = app_handle.clone();
    let worker_state = companion.clone();
    thread::spawn(move || companion_listener_loop(worker_handle, worker_state));

    emit_companion_status(app_handle, &companion);
    update_tray_menu(app_handle, companion.snapshot_status().listener_enabled);
    Ok(companion)
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_autostart::init(
            MacosLauncher::LaunchAgent,
            None,
        ))
        .system_tray(build_system_tray())
        .setup(|app| {
            let companion = setup_companion(&app.handle())?;
            app.manage(companion);
            Ok(())
        })
        .on_window_event(|event| {
            if let WindowEvent::CloseRequested { api, .. } = event.event() {
                event.window().hide().ok();
                api.prevent_close();
            }
        })
        .on_system_tray_event(|app, event| match event {
            SystemTrayEvent::LeftClick { .. } => show_main_window(app),
            SystemTrayEvent::MenuItemClick { id, .. } => match id.as_str() {
                "show" => show_main_window(app),
                "toggle-listener" => {
                    let status = app.state::<CompanionState>().snapshot_status();
                    let next_enabled = !status.listener_enabled;
                    let _ = configure_companion(
                        app.clone(),
                        app.state::<CompanionState>(),
                        status.port_name,
                        status.baud_rate,
                        next_enabled,
                    );
                    update_tray_menu(app, next_enabled);
                }
                "quit" => {
                    let companion = app.state::<CompanionState>();
                    set_companion_shutdown(&companion);
                    std::process::exit(0);
                }
                _ => {}
            },
            _ => {}
        })
        .invoke_handler(tauri::generate_handler![
            list_serial_ports,
            get_companion_status,
            configure_companion,
            cache_companion_macros,
            probe_serial_ports,
            send_updates,
            sync_from_device
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
