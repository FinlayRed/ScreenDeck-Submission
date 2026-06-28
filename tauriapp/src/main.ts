import { listen } from "@tauri-apps/api/event";
import { invoke } from "@tauri-apps/api/tauri";
import "./styles.css";

const GRID_ROWS = 4;
const GRID_COLS = 8;
const GRID_SIZE = GRID_ROWS * GRID_COLS;
const ICON_SIZE = 85;
const ICON_BYTE_SIZE = ICON_SIZE * ICON_SIZE * 2;
const MAX_ACTIONS_PER_ICON = 24;
const RADIAL_DIRECTIONS = ["n", "e", "s", "w"] as const;

type Mod = "CTRL" | "SHIFT" | "ALT" | "GUI";
type RadialDirection = typeof RADIAL_DIRECTIONS[number];

type ComboAction = {
  type: "combo";
  key: string;
  mods: Mod[];
};

type DelayAction = {
  type: "delay";
  ms: number;
};

type MacroAction = ComboAction | DelayAction;

type HostCommandAction = {
  type: "command";
  label: string;
  program: string;
  args: string[];
  cwd: string | null;
  runDetached: boolean;
};

type RadialItem = {
  direction: RadialDirection;
  enabled: boolean;
  actions: MacroAction[];
  hostActions: HostCommandAction[];
  previewDataUrl: string | null;
  imageBytes: number[] | null;
  imageDirty: boolean;
};

type IconSlot = {
  index: number;
  row: number;
  col: number;
  actions: MacroAction[];
  hostActions: HostCommandAction[];
  radialEnabled: boolean;
  radialItems: RadialItem[];
  previewDataUrl: string | null;
  imageBytes: number[] | null;
  imageDirty: boolean;
  macroDirty: boolean;
};

type IconUpload = {
  index: number;
  row: number;
  col: number;
  direction: RadialDirection | null;
  bytes: number[];
};

type SendUpdatesPayload = {
  portName: string;
  baudRate: number;
  iconUploads: IconUpload[];
  macrosJson: string | null;
  sendReloadAll: boolean;
};

type SendUpdatesResult = {
  logs: string[];
};

type UploadProgressEvent = {
  phase: string;
  currentBytes: number;
  totalBytes: number;
  fileBytesSent: number;
  fileTotalBytes: number;
  filePath: string;
  fileIndex: number;
  fileCount: number;
};

type SyncFromDevicePayload = {
  portName: string;
  baudRate: number;
};

type IconDownload = {
  row: number;
  col: number;
  direction?: RadialDirection | null;
  bytes: number[];
};

type SyncFromDeviceResult = {
  logs: string[];
  macrosJson: string;
  fallbackBytes: number[] | null;
  iconDownloads: IconDownload[];
};

type ProbePortResult = {
  portName: string;
  responsive: boolean;
  detail: string;
};

type ProbePortsResult = {
  ports: ProbePortResult[];
  suggestedPort: string | null;
  logs: string[];
};

type CompanionStatus = {
  portName: string;
  baudRate: number;
  listenerEnabled: boolean;
  paused: boolean;
  connected: boolean;
  activePort: string | null;
  lastEvent: string | null;
  lastExecution: string | null;
  lastError: string | null;
  hostActionCount: number;
  autostartEnabled: boolean;
};

type DeviceButtonEvent = {
  index: number;
  row: number;
  col: number;
  direction?: RadialDirection | null;
};

type MacroJsonDoc = {
  version: number;
  grid: {
    rows: number;
    cols: number;
  };
  icons: Array<{
    index: number;
    row: number;
    col: number;
    actions: MacroAction[];
    hostActions?: HostCommandAction[];
    radial?: {
      enabled: boolean;
      items: Array<{
        direction: RadialDirection;
        enabled?: boolean;
        actions: MacroAction[];
        hostActions?: HostCommandAction[];
      }>;
    };
  }>;
};

type NewActionDraft = {
  type: "combo" | "delay";
  key: string;
  mods: Mod[];
  ms: number;
};

type AppState = {
  icons: IconSlot[];
  selectedIndex: number;
  ports: string[];
  portInfo: Record<string, ProbePortResult>;
  selectedPort: string;
  baudRate: number;
  logs: string[];
  sending: boolean;
  syncing: boolean;
  uploadProgress: UploadProgressEvent | null;
  companionStatus: CompanionStatus | null;
  macrosDirty: boolean;
  error: string | null;
  newActionDraft: NewActionDraft | null;
  editorTab: "keys" | "script" | "radial";
  radialEditorTab: "keys" | "script";
  selectedRadialDirection: RadialDirection;
};

function mustGetAppRoot(): HTMLDivElement {
  const found = document.querySelector<HTMLDivElement>("#app");
  if (!found) {
    throw new Error("#app not found");
  }
  return found;
}

const app = mustGetAppRoot();

const state: AppState = {
  icons: createDefaultIcons(),
  selectedIndex: 0,
  ports: [],
  portInfo: {},
  selectedPort: "",
  baudRate: 115200,
  logs: [],
  sending: false,
  syncing: false,
  uploadProgress: null,
  companionStatus: null,
  macrosDirty: false,
  error: null,
  newActionDraft: null,
  editorTab: "keys",
  radialEditorTab: "keys",
  selectedRadialDirection: "n",
};

function createDefaultRadialItems(): RadialItem[] {
  return RADIAL_DIRECTIONS.map((direction) => ({
    direction,
    enabled: true,
    actions: [],
    hostActions: [],
    previewDataUrl: null,
    imageBytes: null,
    imageDirty: false,
  }));
}

function createDefaultIcons(): IconSlot[] {
  const result: IconSlot[] = [];

  for (let index = 0; index < GRID_SIZE; index++) {
    result.push({
      index,
      row: Math.floor(index / GRID_COLS),
      col: index % GRID_COLS,
      actions: [],
      hostActions: [],
      radialEnabled: false,
      radialItems: createDefaultRadialItems(),
      previewDataUrl: null,
      imageBytes: null,
      imageDirty: false,
      macroDirty: false,
    });
  }

  return result;
}

function selectedIcon(): IconSlot {
  return state.icons[state.selectedIndex];
}

function selectedRadialItem(): RadialItem {
  const icon = selectedIcon();
  return icon.radialItems.find((item) => item.direction === state.selectedRadialDirection)
    ?? icon.radialItems[0];
}

function hasPendingChanges(): boolean {
  const hasImageChanges = state.icons.some((icon) =>
    icon.imageDirty || icon.radialItems.some((item) => item.imageDirty)
  );
  return hasImageChanges || state.macrosDirty;
}

function iconHasPendingChanges(icon: IconSlot): boolean {
  return icon.imageDirty || icon.macroDirty || icon.radialItems.some((item) => item.imageDirty);
}

function iconDirtyCount(): number {
  return state.icons.filter((icon) => iconHasPendingChanges(icon)).length;
}

function isPortResponsive(portName: string): boolean {
  return !!state.portInfo[portName]?.responsive;
}

function portLabel(portName: string): string {
  const info = state.portInfo[portName];
  if (!info) {
    return portName;
  }
  return info.responsive ? `${portName} (responsive)` : `${portName} (no response)`;
}

function normalizeComboKey(value: string): string {
  const trimmed = value.trim();
  if (trimmed.length === 1) {
    return trimmed.toUpperCase();
  }
  return trimmed.toUpperCase();
}

function normalizeModifier(value: string): Mod | null {
  const upper = value.trim().toUpperCase();
  if (upper === "CTRL" || upper === "CONTROL") return "CTRL";
  if (upper === "SHIFT") return "SHIFT";
  if (upper === "ALT" || upper === "OPTION") return "ALT";
  if (upper === "GUI" || upper === "WIN" || upper === "CMD" || upper === "META") {
    return "GUI";
  }
  return null;
}

function normalizeRadialDirection(value: unknown): RadialDirection | null {
  if (typeof value !== "string") {
    return null;
  }

  const normalized = value.trim().toLowerCase();
  return (RADIAL_DIRECTIONS as readonly string[]).includes(normalized)
    ? normalized as RadialDirection
    : null;
}

function radialDirectionLabel(direction: RadialDirection): string {
  return direction.toUpperCase();
}

function normalizeAction(raw: unknown): MacroAction | null {
  if (typeof raw !== "object" || raw === null) {
    return null;
  }

  const maybeType = (raw as { type?: unknown }).type;
  const actionType = typeof maybeType === "string" ? maybeType.trim().toLowerCase() : "";

  if (actionType === "combo") {
    const keyRaw = (raw as { key?: unknown }).key;
    if (typeof keyRaw !== "string" || keyRaw.trim() === "") {
      return null;
    }

    const modsRaw = (raw as { mods?: unknown }).mods;
    const mods: Mod[] = [];

    if (Array.isArray(modsRaw)) {
      for (const mod of modsRaw) {
        if (typeof mod !== "string") {
          continue;
        }
        const normalized = normalizeModifier(mod);
        if (normalized) {
          mods.push(normalized);
        }
      }
    } else if (typeof modsRaw === "string") {
      const pieces = modsRaw.split(/[\s,+|]+/).filter((entry) => entry.trim().length > 0);
      for (const piece of pieces) {
        const normalized = normalizeModifier(piece);
        if (normalized) {
          mods.push(normalized);
        }
      }
    }

    return {
      type: "combo",
      key: normalizeComboKey(keyRaw),
      mods,
    };
  }

  if (actionType === "delay") {
    const msRaw = (raw as { ms?: unknown }).ms;
    const msNumber = typeof msRaw === "number" ? msRaw : Number(msRaw);
    if (!Number.isFinite(msNumber)) {
      return null;
    }

    return {
      type: "delay",
      ms: clamp(Math.round(msNumber), 0, 60000),
    };
  }

  return null;
}

function normalizeHostAction(raw: unknown): HostCommandAction | null {
  if (typeof raw !== "object" || raw === null) {
    return null;
  }

  const typeRaw = (raw as { type?: unknown }).type;
  const actionType = typeof typeRaw === "string" ? typeRaw.trim().toLowerCase() : "";
  if (actionType !== "command") {
    return null;
  }

  const labelRaw = (raw as { label?: unknown }).label;
  const programRaw = (raw as { program?: unknown }).program;
  const cwdRaw = (raw as { cwd?: unknown }).cwd;
  const runDetachedRaw = (raw as { runDetached?: unknown }).runDetached;
  const argsRaw = (raw as { args?: unknown }).args;

  const args: string[] = [];
  if (Array.isArray(argsRaw)) {
    for (const arg of argsRaw) {
      if (typeof arg === "string") {
        args.push(arg);
      }
    }
  }

  return {
    type: "command",
    label: typeof labelRaw === "string" ? labelRaw : "",
    program: typeof programRaw === "string" ? programRaw : "",
    args,
    cwd: typeof cwdRaw === "string" && cwdRaw.trim() ? cwdRaw : null,
    runDetached: Boolean(runDetachedRaw),
  };
}

function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

function buildMacroDocument(): MacroJsonDoc {
  return {
    version: 2,
    grid: {
      rows: GRID_ROWS,
      cols: GRID_COLS,
    },
    icons: state.icons.map((icon) => {
      const radialItems = icon.radialItems
        .filter((item) =>
          item.enabled && (item.actions.length > 0 || item.hostActions.length > 0)
        )
        .map((item) => ({
          direction: item.direction,
          enabled: item.enabled,
          actions: item.actions,
          hostActions: item.hostActions,
        }));

      return {
        index: icon.index,
        row: icon.row,
        col: icon.col,
        actions: icon.actions,
        hostActions: icon.hostActions,
        ...(icon.radialEnabled && radialItems.length > 0
          ? {
            radial: {
              enabled: true,
              items: radialItems,
            },
          }
          : {}),
      };
    }),
  };
}

function applyMacroDocument(doc: unknown, markDirty = true): void {
  if (typeof doc !== "object" || doc === null) {
    throw new Error("JSON root must be an object");
  }

  const iconsRaw = (doc as { icons?: unknown }).icons;
  if (!Array.isArray(iconsRaw)) {
    throw new Error("JSON is missing icons[] array");
  }

  const nextActions: MacroAction[][] = Array.from({ length: GRID_SIZE }, () => []);
  const nextHostActions: HostCommandAction[][] = Array.from({ length: GRID_SIZE }, () => []);
  const nextRadialEnabled: boolean[] = Array.from({ length: GRID_SIZE }, () => false);
  const nextRadialItems: RadialItem[][] = Array.from({ length: GRID_SIZE }, () =>
    createDefaultRadialItems()
  );

  for (const entry of iconsRaw) {
    if (typeof entry !== "object" || entry === null) {
      continue;
    }

    let index: number | null = null;
    const maybeIndex = (entry as { index?: unknown }).index;
    if (typeof maybeIndex === "number" && Number.isInteger(maybeIndex)) {
      index = maybeIndex;
    } else {
      const row = (entry as { row?: unknown }).row;
      const col = (entry as { col?: unknown }).col;
      if (
        typeof row === "number" &&
        Number.isInteger(row) &&
        typeof col === "number" &&
        Number.isInteger(col)
      ) {
        index = row * GRID_COLS + col;
      }
    }

    if (index === null || index < 0 || index >= GRID_SIZE) {
      continue;
    }

    const actionsRaw = (entry as { actions?: unknown }).actions;
    if (!Array.isArray(actionsRaw)) {
      continue;
    }

    const hostActionsRaw = (entry as { hostActions?: unknown }).hostActions;
    const radialRaw = (entry as { radial?: unknown }).radial;

    const parsed: MacroAction[] = [];
    for (const actionRaw of actionsRaw) {
      const action = normalizeAction(actionRaw);
      if (action) {
        parsed.push(action);
      }
      if (parsed.length >= MAX_ACTIONS_PER_ICON) {
        break;
      }
    }

    if (Array.isArray(hostActionsRaw)) {
      const parsedHostActions: HostCommandAction[] = [];
      for (const hostActionRaw of hostActionsRaw) {
        const hostAction = normalizeHostAction(hostActionRaw);
        if (hostAction) {
          parsedHostActions.push(hostAction);
        }
      }
      nextHostActions[index] = parsedHostActions;
    }

    nextActions[index] = parsed;

    if (typeof radialRaw === "object" && radialRaw !== null) {
      const enabledRaw = (radialRaw as { enabled?: unknown }).enabled;
      nextRadialEnabled[index] = Boolean(enabledRaw);
      const itemsRaw = (radialRaw as { items?: unknown }).items;
      if (Array.isArray(itemsRaw)) {
        const radialItems = createDefaultRadialItems();
        for (const itemRaw of itemsRaw) {
          if (typeof itemRaw !== "object" || itemRaw === null) {
            continue;
          }

          const direction = normalizeRadialDirection((itemRaw as { direction?: unknown }).direction);
          if (!direction) {
            continue;
          }

          const radialItem = radialItems.find((item) => item.direction === direction);
          if (!radialItem) {
            continue;
          }

          const radialEnabledRaw = (itemRaw as { enabled?: unknown }).enabled;
          radialItem.enabled = radialEnabledRaw === undefined ? true : Boolean(radialEnabledRaw);

          const radialActionsRaw = (itemRaw as { actions?: unknown }).actions;
          if (Array.isArray(radialActionsRaw)) {
            const radialActions: MacroAction[] = [];
            for (const actionRaw of radialActionsRaw) {
              const action = normalizeAction(actionRaw);
              if (action) {
                radialActions.push(action);
              }
              if (radialActions.length >= MAX_ACTIONS_PER_ICON) {
                break;
              }
            }
            radialItem.actions = radialActions;
          }

          const radialHostActionsRaw = (itemRaw as { hostActions?: unknown }).hostActions;
          if (Array.isArray(radialHostActionsRaw)) {
            const radialHostActions: HostCommandAction[] = [];
            for (const hostActionRaw of radialHostActionsRaw) {
              const hostAction = normalizeHostAction(hostActionRaw);
              if (hostAction) {
                radialHostActions.push(hostAction);
              }
            }
            radialItem.hostActions = radialHostActions;
          }
        }
        nextRadialItems[index] = radialItems;
      }
    }
  }

  state.icons = state.icons.map((icon) => ({
    ...icon,
    actions: nextActions[icon.index],
    hostActions: nextHostActions[icon.index],
    radialEnabled: nextRadialEnabled[icon.index],
    radialItems: nextRadialItems[icon.index].map((item) => {
      const previous = icon.radialItems.find((existing) => existing.direction === item.direction);
      return {
        ...item,
        previewDataUrl: previous?.previewDataUrl ?? null,
        imageBytes: null,
        imageDirty: false,
      };
    }),
    macroDirty: markDirty,
  }));
  state.macrosDirty = markDirty;
}

function appendLog(line: string): void {
  const timestamp = new Date().toLocaleTimeString();
  state.logs.push(`[${timestamp}] ${line}`);
  if (state.logs.length > 300) {
    state.logs = state.logs.slice(state.logs.length - 300);
  }
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) {
    return `${bytes} B`;
  }

  if (bytes < 1024 * 1024) {
    return `${(bytes / 1024).toFixed(1)} KB`;
  }

  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

function uploadProgressPercent(progress: UploadProgressEvent): number {
  if (progress.totalBytes <= 0) {
    return 0;
  }

  return clamp(Math.round((progress.currentBytes / progress.totalBytes) * 100), 0, 100);
}

function uploadProgressHeadline(progress: UploadProgressEvent): string {
  if (progress.phase === "preparing") {
    return "Preparing upload";
  }

  if (progress.phase === "finalizing") {
    return "Finalizing device reload";
  }

  if (progress.phase === "complete") {
    return "Upload complete";
  }

  if (progress.filePath) {
    return `Uploading ${progress.filePath}`;
  }

  return "Uploading changes";
}

function uploadProgressDetail(progress: UploadProgressEvent): string {
  if (progress.phase === "uploading" && progress.filePath) {
    const fileLabel = progress.fileCount > 0 ? `File ${progress.fileIndex}/${progress.fileCount}` : "Uploading";
    return `${fileLabel} - ${formatBytes(progress.fileBytesSent)} of ${formatBytes(progress.fileTotalBytes)}`;
  }

  return `${formatBytes(progress.currentBytes)} of ${formatBytes(progress.totalBytes)}`;
}

async function setupUploadProgressListener(): Promise<void> {
  await listen<UploadProgressEvent>("upload-progress", (event) => {
    state.uploadProgress = event.payload;
    if (state.sending) {
      render();
    }
  });
}

function hostArgsToMultiline(args: string[]): string {
  return args.join("\n");
}

function multilineToHostArgs(value: string): string[] {
  return value
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line.length > 0);
}

function currentActionTarget(): { actions: MacroAction[]; ownerIndex: number } {
  if (state.editorTab === "radial" && state.radialEditorTab === "keys") {
    return { actions: selectedRadialItem().actions, ownerIndex: selectedIcon().index };
  }

  return { actions: selectedIcon().actions, ownerIndex: selectedIcon().index };
}

function currentHostActionTarget(): { hostActions: HostCommandAction[]; ownerIndex: number } {
  if (state.editorTab === "radial" && state.radialEditorTab === "script") {
    return { hostActions: selectedRadialItem().hostActions, ownerIndex: selectedIcon().index };
  }

  return { hostActions: selectedIcon().hostActions, ownerIndex: selectedIcon().index };
}

function companionStatusLabel(): string {
  const status = state.companionStatus;
  if (!status) {
    return "Companion status unavailable";
  }

  if (!status.listenerEnabled) {
    return "Companion paused";
  }

  if (status.paused) {
    return "Companion paused for transfer";
  }

  if (status.connected && status.activePort) {
    return `Companion listening on ${status.activePort}`;
  }

  if (status.portName) {
    return `Companion waiting for ${status.portName}`;
  }

  return "Companion ready - select a port";
}

function applyCompanionStatus(status: CompanionStatus): void {
  state.companionStatus = status;
  if (!state.selectedPort && status.portName) {
    state.selectedPort = status.portName;
  }
  if (status.baudRate > 0) {
    state.baudRate = status.baudRate;
  }
}

async function syncCompanionSettings(): Promise<void> {
  try {
    const status = await invoke<CompanionStatus>("configure_companion", {
      portName: state.selectedPort,
      baudRate: state.baudRate,
      listenerEnabled: state.companionStatus?.listenerEnabled ?? true,
    });
    applyCompanionStatus(status);
  } catch (error) {
    appendLog(`Companion settings update failed: ${String(error)}`);
  }
}

async function setCompanionListenerEnabled(listenerEnabled: boolean): Promise<void> {
  try {
    const status = await invoke<CompanionStatus>("configure_companion", {
      portName: state.selectedPort,
      baudRate: state.baudRate,
      listenerEnabled,
    });
    applyCompanionStatus(status);
    appendLog(listenerEnabled ? "Companion listener enabled" : "Companion listener paused");
  } catch (error) {
    state.error = `Failed to update companion listener: ${String(error)}`;
    appendLog(state.error);
  }
  render();
}

async function loadCompanionStatus(): Promise<void> {
  try {
    const status = await invoke<CompanionStatus>("get_companion_status");
    applyCompanionStatus(status);
  } catch (error) {
    appendLog(`Failed to load companion status: ${String(error)}`);
  }
}

async function cacheCompanionMacros(): Promise<void> {
  try {
    const status = await invoke<CompanionStatus>("cache_companion_macros", {
      macrosJson: JSON.stringify(buildMacroDocument(), null, 2),
    });
    applyCompanionStatus(status);
  } catch (error) {
    appendLog(`Failed to cache companion macros: ${String(error)}`);
  }
}

async function setupCompanionListeners(): Promise<void> {
  await listen<CompanionStatus>("companion-status", (event) => {
    applyCompanionStatus(event.payload);
    render();
  });

  await listen<{ line: string }>("companion-log", (event) => {
    appendLog(event.payload.line);
    render();
  });

  await listen<DeviceButtonEvent>("device-button-event", (event) => {
    const direction = event.payload.direction;
    appendLog(direction
      ? `Deck radial ${event.payload.index}/${direction} released (${event.payload.row},${event.payload.col})`
      : `Deck button ${event.payload.index} pressed (${event.payload.row},${event.payload.col})`);
    render();
  });
}

function escapeHtml(value: string): string {
  return value
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/\"/g, "&quot;")
    .replace(/'/g, "&#039;");
}

function iconCardMarkup(icon: IconSlot): string {
  const isSelected = icon.index === state.selectedIndex;
  const dirty = iconHasPendingChanges(icon);
  const radialCount = icon.radialItems.filter((item) =>
    item.enabled && (item.actions.length > 0 || item.hostActions.length > 0)
  ).length;
  const classes = ["icon-card"];
  if (isSelected) classes.push("selected");
  if (dirty) classes.push("dirty");

  const body = icon.previewDataUrl
    ? `<img src="${icon.previewDataUrl}" alt="Icon ${icon.index}" />`
    : `<div class="icon-placeholder">${icon.row},${icon.col}</div>`;

  return `
    <button class="${classes.join(" ")}" data-icon-index="${icon.index}">
      ${body}
      <span class="icon-index">#${icon.index}</span>
      ${icon.radialEnabled ? `<span class="radial-badge">${radialCount}</span>` : ""}
    </button>
  `;
}

function actionMarkup(action: MacroAction, actionIndex: number): string {
  const typeSelect = `
    <select class="action-type" data-action-index="${actionIndex}">
      <option value="combo" ${action.type === "combo" ? "selected" : ""}>combo</option>
      <option value="delay" ${action.type === "delay" ? "selected" : ""}>delay</option>
    </select>
  `;

  if (action.type === "combo") {
    const hasMod = (mod: Mod) => action.mods.includes(mod);

    return `
      <div class="action-row" data-action-index="${actionIndex}">
        ${typeSelect}
        <input class="action-key" data-action-index="${actionIndex}" value="${escapeHtml(action.key)}" maxlength="16" placeholder="Key (A, ENTER, F13)" />
        <label><input type="checkbox" class="mod-toggle" data-action-index="${actionIndex}" data-mod="CTRL" ${hasMod("CTRL") ? "checked" : ""}/>Ctrl</label>
        <label><input type="checkbox" class="mod-toggle" data-action-index="${actionIndex}" data-mod="SHIFT" ${hasMod("SHIFT") ? "checked" : ""}/>Shift</label>
        <label><input type="checkbox" class="mod-toggle" data-action-index="${actionIndex}" data-mod="ALT" ${hasMod("ALT") ? "checked" : ""}/>Alt</label>
        <label><input type="checkbox" class="mod-toggle" data-action-index="${actionIndex}" data-mod="GUI" ${hasMod("GUI") ? "checked" : ""}/>Gui</label>
        <button class="danger small remove-action" data-action-index="${actionIndex}">Remove</button>
      </div>
    `;
  }

  return `
    <div class="action-row" data-action-index="${actionIndex}">
      ${typeSelect}
      <label class="delay-label">ms</label>
      <input class="action-delay" data-action-index="${actionIndex}" type="number" min="0" max="60000" value="${action.ms}" />
      <button class="danger small remove-action" data-action-index="${actionIndex}">Remove</button>
    </div>
  `;
}

function hostActionMarkup(action: HostCommandAction, actionIndex: number): string {
  return `
    <div class="host-action-card" data-host-action-index="${actionIndex}">
      <div class="host-action-head">
        <strong>Host Command ${actionIndex + 1}</strong>
        <button class="danger small remove-host-action" data-host-action-index="${actionIndex}">Remove</button>
      </div>
      <div class="host-action-grid">
        <label>
          Label
          <input class="host-action-label" data-host-action-index="${actionIndex}" type="text" value="${escapeHtml(action.label)}" placeholder="Spotify Next" />
        </label>
        <label>
          Program
          <input class="host-action-program" data-host-action-index="${actionIndex}" type="text" value="${escapeHtml(action.program)}" placeholder="C:\\Python312\\python.exe" />
        </label>
        <label>
          Working Directory
          <input class="host-action-cwd" data-host-action-index="${actionIndex}" type="text" value="${escapeHtml(action.cwd ?? "")}" placeholder="Optional working directory" />
        </label>
        <label class="host-action-checkbox">
          <input class="host-action-detached" data-host-action-index="${actionIndex}" type="checkbox" ${action.runDetached ? "checked" : ""} />
          Run detached
        </label>
      </div>
      <label>
        Arguments (one per line)
        <textarea class="host-action-args" data-host-action-index="${actionIndex}" rows="4" placeholder="C:\\scripts\\spotify_next.py">${escapeHtml(hostArgsToMultiline(action.args))}</textarea>
      </label>
    </div>
  `;
}

function newActionFormMarkup(): string {
  if (state.newActionDraft === null) {
    return "";
  }

  return `
    <div class="new-action-form">
      <div class="new-action-row">
        <select id="newActionType">
          <option value="combo" ${state.newActionDraft.type === "combo" ? "selected" : ""}>combo</option>
          <option value="delay" ${state.newActionDraft.type === "delay" ? "selected" : ""}>delay</option>
        </select>
        ${state.newActionDraft.type === "combo" ? `
          <input id="newActionKey" class="action-key" value="${escapeHtml(state.newActionDraft.key)}" maxlength="16" placeholder="Key (A, ENTER, F13)" />
        ` : `
          <label class="delay-label">ms</label>
          <input id="newActionMs" class="action-delay" type="number" min="0" max="60000" value="${state.newActionDraft.ms}" />
        `}
      </div>
      ${state.newActionDraft.type === "combo" ? `
      <div class="new-action-mods">
        <label><input type="checkbox" id="newModCTRL" ${state.newActionDraft.mods.includes("CTRL") ? "checked" : ""} />Ctrl</label>
        <label><input type="checkbox" id="newModSHIFT" ${state.newActionDraft.mods.includes("SHIFT") ? "checked" : ""} />Shift</label>
        <label><input type="checkbox" id="newModALT" ${state.newActionDraft.mods.includes("ALT") ? "checked" : ""} />Alt</label>
        <label><input type="checkbox" id="newModGUI" ${state.newActionDraft.mods.includes("GUI") ? "checked" : ""} />Gui</label>
      </div>
      ` : ""}
      <div class="button-row">
        <button id="confirmAddCommandBtn" class="primary small">Add to sequence</button>
        <button id="cancelAddCommandBtn" class="secondary small">Cancel</button>
      </div>
    </div>
  `;
}

function keyEditorMarkup(title: string, actions: MacroAction[]): string {
  return `
    <div class="section-heading">
      <h3>${escapeHtml(title)} <span class="action-count">${actions.length}/${MAX_ACTIONS_PER_ICON}</span></h3>
      <button id="addCommandBtn" class="secondary small" ${actions.length >= MAX_ACTIONS_PER_ICON || state.newActionDraft !== null ? "disabled" : ""}>+ Add command</button>
    </div>

    ${newActionFormMarkup()}

    <div class="actions-list">
      ${actions.length === 0 ? `<div class="empty-state">No actions configured.</div>` : actions.map((action, i) => actionMarkup(action, i)).join("")}
    </div>
  `;
}

function scriptEditorMarkup(title: string, hostActions: HostCommandAction[]): string {
  return `
    <div class="section-heading">
      <div>
        <h3>${escapeHtml(title)}</h3>
        <p>Run a program or script via the desktop companion.</p>
      </div>
      <button id="addHostActionBtn" class="secondary small">+ Add script</button>
    </div>

    <div class="host-actions-list">
      ${hostActions.length === 0 ? `<div class="empty-state">No scripts configured.</div>` : hostActions.map((action, i) => hostActionMarkup(action, i)).join("")}
    </div>
  `;
}

function radialDirectionGridMarkup(icon: IconSlot): string {
  const cells: Array<RadialDirection | "center" | "empty"> = ["empty", "n", "empty", "w", "center", "e", "empty", "s", "empty"];

  return `
    <div class="radial-grid">
      ${cells.map((cell) => {
        if (cell === "center") {
          return `<div class="radial-center">${icon.previewDataUrl ? `<img src="${icon.previewDataUrl}" alt="Center icon" />` : ""}</div>`;
        }

        if (cell === "empty") {
          return `<div class="radial-empty"></div>`;
        }

        const item = icon.radialItems.find((entry) => entry.direction === cell);
        const configured = !!item && (item.actions.length > 0 || item.hostActions.length > 0);
        const enabled = item?.enabled ?? true;
        const selected = cell === state.selectedRadialDirection;
        return `
          <button class="radial-slot ${selected ? "selected" : ""} ${configured ? "configured" : ""} ${enabled ? "" : "disabled"}" data-radial-direction="${cell}">
            ${item?.previewDataUrl ? `<img src="${item.previewDataUrl}" alt="${cell} radial icon" />` : `<span>${radialDirectionLabel(cell)}</span>`}
          </button>
        `;
      }).join("")}
    </div>
  `;
}

function radialEditorMarkup(icon: IconSlot): string {
  const item = selectedRadialItem();

  return `
    <div class="radial-panel">
      <label class="radial-enable">
        <input id="radialEnabledInput" type="checkbox" ${icon.radialEnabled ? "checked" : ""} />
        Enable radial menu
      </label>

      ${radialDirectionGridMarkup(icon)}

      <div class="radial-selected-head">
        <h3>${radialDirectionLabel(item.direction)} Slot</h3>
        <span class="action-count">${item.enabled ? "Enabled" : "Disabled"} - ${item.actions.length + item.hostActions.length} configured</span>
      </div>

      <label class="radial-enable">
        <input id="radialItemEnabledInput" type="checkbox" ${item.enabled ? "checked" : ""} />
        Show this slot on device
      </label>

      <div class="preview-box radial-preview">
        ${item.previewDataUrl ? `<img src="${item.previewDataUrl}" alt="Radial icon preview" />` : `<div class="preview-placeholder">${radialDirectionLabel(item.direction)}</div>`}
      </div>

      <div class="button-row">
        <button id="pickRadialImageBtn" class="secondary">Choose Radial Image</button>
        <button id="clearRadialImageBtn" class="secondary" ${item.imageBytes || item.previewDataUrl ? "" : "disabled"}>Clear Radial Image</button>
      </div>
      <input id="radialFileInput" type="file" accept="image/*" hidden />

      <div class="mode-tabs compact">
        <button id="radialTabKeysBtn" class="tab-btn ${state.radialEditorTab === "keys" ? "active" : ""}">Key Sequence</button>
        <button id="radialTabScriptBtn" class="tab-btn ${state.radialEditorTab === "script" ? "active" : ""}">Execute Script</button>
      </div>

      ${state.radialEditorTab === "keys"
        ? keyEditorMarkup("Radial Key Sequence", item.actions)
        : scriptEditorMarkup("Radial Script", item.hostActions)}
    </div>
  `;
}

function render(): void {
  const icon = selectedIcon();
  const changed = hasPendingChanges();
  const uploadProgress = state.uploadProgress;
  const companionStatus = state.companionStatus;
  const selectedPortStatus = state.selectedPort
    ? isPortResponsive(state.selectedPort)
      ? "Selected port responds to CDC"
      : "Selected port has not responded to CDC"
    : "No port selected";

  app.innerHTML = `
    <main class="shell">
      <header class="topbar">
        <div>
          <h1>Deck Macro Editor</h1>
          <p>4x8 icon + macro editor for the Waveshare deck firmware.</p>
        </div>
        <div class="status-block">
          <span class="pill ${changed ? "dirty" : "clean"}">${changed ? "Unsynced changes" : "In sync"}</span>
          <span class="pill neutral">Dirty slots: ${iconDirtyCount()}</span>
        </div>
      </header>

      <section class="transport">
        <div class="control">
          <label for="portSelect">Serial Port</label>
          <select id="portSelect">
            <option value="">Select a port</option>
            ${state.ports
              .map((port) => `<option value="${escapeHtml(port)}" ${port === state.selectedPort ? "selected" : ""}>${escapeHtml(portLabel(port))}</option>`)
              .join("")}
          </select>
        </div>
        <div class="control small">
          <label for="baudRateInput">Baud</label>
          <input id="baudRateInput" type="number" value="${state.baudRate}" min="1200" max="2000000" step="1" />
        </div>
        <button id="refreshPortsBtn" class="secondary">Refresh Ports</button>
        <button id="sendBtn" class="primary" ${state.sending || state.syncing ? "disabled" : ""}>${state.sending ? "Sending..." : "Send Changes To Device"}</button>
        <button id="syncBtn" class="secondary" ${state.sending || state.syncing ? "disabled" : ""}>${state.syncing ? "Syncing..." : "Sync From Device"}</button>
        <button id="toggleCompanionBtn" class="secondary">${companionStatus?.listenerEnabled === false ? "Resume Companion" : "Pause Companion"}</button>
        <div class="transport-note">${escapeHtml(selectedPortStatus)}</div>
        <div class="companion-panel">
          <div class="companion-header">
            <strong>Desktop Companion</strong>
            <span class="pill ${companionStatus?.connected ? "clean" : companionStatus?.listenerEnabled === false ? "neutral" : "dirty"}">${companionStatus?.connected ? "Connected" : companionStatus?.listenerEnabled === false ? "Paused" : "Waiting"}</span>
          </div>
          <div class="companion-meta">
            <span>${escapeHtml(companionStatusLabel())}</span>
            <span>${companionStatus?.autostartEnabled ? "Autostart on" : "Autostart off"}</span>
          </div>
          ${companionStatus?.lastExecution ? `<div class="companion-detail">Last host action: ${escapeHtml(companionStatus.lastExecution)}</div>` : ""}
          ${companionStatus?.lastError ? `<div class="companion-detail error">${escapeHtml(companionStatus.lastError)}</div>` : ""}
        </div>
        ${state.sending && uploadProgress
          ? `
            <div class="upload-progress-panel">
              <div class="upload-progress-header">
                <strong>${escapeHtml(uploadProgressHeadline(uploadProgress))}</strong>
                <span>${uploadProgressPercent(uploadProgress)}%</span>
              </div>
              <div class="upload-progress-track">
                <div class="upload-progress-fill" style="width: ${uploadProgressPercent(uploadProgress)}%"></div>
              </div>
              <div class="upload-progress-meta">
                <span>${escapeHtml(uploadProgressDetail(uploadProgress))}</span>
                <span>${escapeHtml(formatBytes(uploadProgress.currentBytes))} / ${escapeHtml(formatBytes(uploadProgress.totalBytes))}</span>
              </div>
            </div>
          `
          : ""}
      </section>

      ${state.error ? `<section class="error-banner">${escapeHtml(state.error)}</section>` : ""}

      <section class="workspace">
        <div class="grid-pane">
          <div class="grid">
            ${state.icons.map((slot) => iconCardMarkup(slot)).join("")}
          </div>
        </div>

        <div class="editor-pane">
          <h2>Slot #${icon.index} (${icon.row},${icon.col})</h2>
          <p class="hint">Assign icon image and configure action sequence.</p>

          <div class="preview-box">
            ${icon.previewDataUrl ? `<img src="${icon.previewDataUrl}" alt="Selected icon preview" />` : `<div class="preview-placeholder">No icon selected</div>`}
          </div>

          <div class="button-row">
            <button id="pickImageBtn" class="secondary">Choose Image</button>
            <button id="clearImageBtn" class="secondary" ${icon.imageBytes ? "" : "disabled"}>Clear Pending Image</button>
          </div>
          <input id="iconFileInput" type="file" accept="image/*" hidden />

          <hr />

          <div class="mode-tabs">
            <button id="tabKeysBtn" class="tab-btn ${state.editorTab === "keys" ? "active" : ""}">Key Sequence</button>
            <button id="tabScriptBtn" class="tab-btn ${state.editorTab === "script" ? "active" : ""}">Execute Script</button>
            <button id="tabRadialBtn" class="tab-btn ${state.editorTab === "radial" ? "active" : ""}">Radial Menu</button>
          </div>

          ${state.editorTab === "keys"
            ? keyEditorMarkup("Key Sequence", icon.actions)
            : state.editorTab === "script"
              ? scriptEditorMarkup("Execute Script", icon.hostActions)
              : radialEditorMarkup(icon)}
        </div>
      </section>

      <section class="logs">
        <div class="logs-header">
          <h3>Transfer Log</h3>
          <button id="clearLogsBtn" class="secondary small">Clear</button>
        </div>
        <pre>${state.logs.map((line) => escapeHtml(line)).join("\n")}</pre>
      </section>
    </main>
  `;

  bindEvents();
}

function markMacroDirty(index: number): void {
  state.icons[index].macroDirty = true;
  state.macrosDirty = true;
}

function bindEvents(): void {
  app.querySelectorAll<HTMLButtonElement>(".icon-card").forEach((btn) => {
    btn.addEventListener("click", () => {
      const raw = btn.dataset.iconIndex;
      if (!raw) return;
      state.selectedIndex = Number(raw);
      state.error = null;
      state.newActionDraft = null;
      state.editorTab = "keys";
      state.radialEditorTab = "keys";
      state.selectedRadialDirection = "n";
      render();
    });
  });

  const refreshPortsBtn = app.querySelector<HTMLButtonElement>("#refreshPortsBtn");
  refreshPortsBtn?.addEventListener("click", () => {
    void refreshPorts();
  });

  const portSelect = app.querySelector<HTMLSelectElement>("#portSelect");
  portSelect?.addEventListener("change", () => {
    state.selectedPort = portSelect.value;
    void syncCompanionSettings();
  });

  const baudRateInput = app.querySelector<HTMLInputElement>("#baudRateInput");
  baudRateInput?.addEventListener("change", () => {
    state.baudRate = clamp(Number(baudRateInput.value) || 115200, 1200, 2000000);
    baudRateInput.value = String(state.baudRate);
    void syncCompanionSettings();
  });

  const sendBtn = app.querySelector<HTMLButtonElement>("#sendBtn");
  sendBtn?.addEventListener("click", () => {
    void sendChanges();
  });

  const syncBtn = app.querySelector<HTMLButtonElement>("#syncBtn");
  syncBtn?.addEventListener("click", () => {
    void syncFromDevice();
  });

  const toggleCompanionBtn = app.querySelector<HTMLButtonElement>("#toggleCompanionBtn");
  toggleCompanionBtn?.addEventListener("click", () => {
    void setCompanionListenerEnabled(!(state.companionStatus?.listenerEnabled ?? true));
  });

  const pickImageBtn = app.querySelector<HTMLButtonElement>("#pickImageBtn");
  const iconFileInput = app.querySelector<HTMLInputElement>("#iconFileInput");
  pickImageBtn?.addEventListener("click", () => {
    iconFileInput?.click();
  });

  iconFileInput?.addEventListener("change", async () => {
    const file = iconFileInput.files?.[0];
    if (!file) {
      return;
    }

    try {
      const converted = await convertImageToRgb565(file);
      const icon = selectedIcon();
      icon.previewDataUrl = converted.previewDataUrl;
      icon.imageBytes = converted.bytes;
      icon.imageDirty = true;
      appendLog(`Prepared icon_${icon.row}_${icon.col}.bin (${converted.bytes.length} bytes)`);
      state.error = null;
    } catch (error) {
      state.error = `Image conversion failed: ${String(error)}`;
      appendLog(state.error);
    }

    iconFileInput.value = "";
    render();
  });

  const clearImageBtn = app.querySelector<HTMLButtonElement>("#clearImageBtn");
  clearImageBtn?.addEventListener("click", () => {
    const icon = selectedIcon();
    icon.imageBytes = null;
    icon.imageDirty = false;
    icon.previewDataUrl = null;
    render();
  });

  const tabKeysBtn = app.querySelector<HTMLButtonElement>("#tabKeysBtn");
  tabKeysBtn?.addEventListener("click", () => {
    state.editorTab = "keys";
    state.newActionDraft = null;
    render();
  });

  const tabScriptBtn = app.querySelector<HTMLButtonElement>("#tabScriptBtn");
  tabScriptBtn?.addEventListener("click", () => {
    state.editorTab = "script";
    state.newActionDraft = null;
    render();
  });

  const tabRadialBtn = app.querySelector<HTMLButtonElement>("#tabRadialBtn");
  tabRadialBtn?.addEventListener("click", () => {
    state.editorTab = "radial";
    state.newActionDraft = null;
    render();
  });

  const radialEnabledInput = app.querySelector<HTMLInputElement>("#radialEnabledInput");
  radialEnabledInput?.addEventListener("change", () => {
    const icon = selectedIcon();
    icon.radialEnabled = radialEnabledInput.checked;
    markMacroDirty(icon.index);
    render();
  });

  const radialItemEnabledInput = app.querySelector<HTMLInputElement>("#radialItemEnabledInput");
  radialItemEnabledInput?.addEventListener("change", () => {
    const icon = selectedIcon();
    const item = selectedRadialItem();
    item.enabled = radialItemEnabledInput.checked;
    markMacroDirty(icon.index);
    render();
  });

  app.querySelectorAll<HTMLButtonElement>(".radial-slot").forEach((btn) => {
    btn.addEventListener("click", () => {
      const direction = normalizeRadialDirection(btn.dataset.radialDirection);
      if (!direction) return;
      state.selectedRadialDirection = direction;
      state.newActionDraft = null;
      render();
    });
  });

  const radialTabKeysBtn = app.querySelector<HTMLButtonElement>("#radialTabKeysBtn");
  radialTabKeysBtn?.addEventListener("click", () => {
    state.radialEditorTab = "keys";
    state.newActionDraft = null;
    render();
  });

  const radialTabScriptBtn = app.querySelector<HTMLButtonElement>("#radialTabScriptBtn");
  radialTabScriptBtn?.addEventListener("click", () => {
    state.radialEditorTab = "script";
    state.newActionDraft = null;
    render();
  });

  const pickRadialImageBtn = app.querySelector<HTMLButtonElement>("#pickRadialImageBtn");
  const radialFileInput = app.querySelector<HTMLInputElement>("#radialFileInput");
  pickRadialImageBtn?.addEventListener("click", () => {
    radialFileInput?.click();
  });

  radialFileInput?.addEventListener("change", async () => {
    const file = radialFileInput.files?.[0];
    if (!file) {
      return;
    }

    try {
      const converted = await convertImageToRgb565(file);
      const icon = selectedIcon();
      const item = selectedRadialItem();
      item.previewDataUrl = converted.previewDataUrl;
      item.imageBytes = converted.bytes;
      item.imageDirty = true;
      appendLog(`Prepared radial_${icon.row}_${icon.col}_${item.direction}.bin (${converted.bytes.length} bytes)`);
      state.error = null;
    } catch (error) {
      state.error = `Radial image conversion failed: ${String(error)}`;
      appendLog(state.error);
    }

    radialFileInput.value = "";
    render();
  });

  const clearRadialImageBtn = app.querySelector<HTMLButtonElement>("#clearRadialImageBtn");
  clearRadialImageBtn?.addEventListener("click", () => {
    const item = selectedRadialItem();
    item.imageBytes = null;
    item.imageDirty = false;
    item.previewDataUrl = null;
    render();
  });

  const addCommandBtn = app.querySelector<HTMLButtonElement>("#addCommandBtn");
  addCommandBtn?.addEventListener("click", () => {
    const target = currentActionTarget();
    if (target.actions.length >= MAX_ACTIONS_PER_ICON) return;
    state.newActionDraft = { type: "combo", key: "A", mods: [], ms: 120 };
    render();
  });

  const newActionType = app.querySelector<HTMLSelectElement>("#newActionType");
  newActionType?.addEventListener("change", () => {
    if (!state.newActionDraft) return;
    state.newActionDraft = { ...state.newActionDraft, type: newActionType.value as "combo" | "delay" };
    render();
  });

  const newActionKey = app.querySelector<HTMLInputElement>("#newActionKey");
  newActionKey?.addEventListener("input", () => {
    if (!state.newActionDraft) return;
    state.newActionDraft.key = newActionKey.value;
  });

  const newActionMs = app.querySelector<HTMLInputElement>("#newActionMs");
  newActionMs?.addEventListener("input", () => {
    if (!state.newActionDraft) return;
    state.newActionDraft.ms = clamp(Number(newActionMs.value) || 0, 0, 60000);
  });

  (["CTRL", "SHIFT", "ALT", "GUI"] as Mod[]).forEach((mod) => {
    const checkbox = app.querySelector<HTMLInputElement>(`#newMod${mod}`);
    checkbox?.addEventListener("change", () => {
      if (!state.newActionDraft) return;
      const mods = new Set(state.newActionDraft.mods);
      if (checkbox.checked) {
        mods.add(mod);
      } else {
        mods.delete(mod);
      }
      state.newActionDraft.mods = Array.from(mods);
    });
  });

  const confirmAddCommandBtn = app.querySelector<HTMLButtonElement>("#confirmAddCommandBtn");
  confirmAddCommandBtn?.addEventListener("click", () => {
    const target = currentActionTarget();
    if (!state.newActionDraft || target.actions.length >= MAX_ACTIONS_PER_ICON) return;
    const draft = state.newActionDraft;
    let action: MacroAction;
    if (draft.type === "combo") {
      action = { type: "combo", key: normalizeComboKey(draft.key || "A"), mods: draft.mods };
    } else {
      action = { type: "delay", ms: draft.ms };
    }
    target.actions.push(action);
    markMacroDirty(target.ownerIndex);
    state.newActionDraft = null;
    render();
  });

  const cancelAddCommandBtn = app.querySelector<HTMLButtonElement>("#cancelAddCommandBtn");
  cancelAddCommandBtn?.addEventListener("click", () => {
    state.newActionDraft = null;
    render();
  });

  app.querySelectorAll<HTMLButtonElement>(".remove-action").forEach((btn) => {
    btn.addEventListener("click", () => {
      const target = currentActionTarget();
      const idx = Number(btn.dataset.actionIndex);
      if (!Number.isInteger(idx) || idx < 0 || idx >= target.actions.length) {
        return;
      }
      target.actions.splice(idx, 1);
      markMacroDirty(target.ownerIndex);
      render();
    });
  });

  app.querySelectorAll<HTMLSelectElement>(".action-type").forEach((select) => {
    select.addEventListener("change", () => {
      const target = currentActionTarget();
      const idx = Number(select.dataset.actionIndex);
      if (!Number.isInteger(idx) || idx < 0 || idx >= target.actions.length) {
        return;
      }

      if (select.value === "combo") {
        target.actions[idx] = { type: "combo", key: "A", mods: [] };
      } else {
        target.actions[idx] = { type: "delay", ms: 120 };
      }

      markMacroDirty(target.ownerIndex);
      render();
    });
  });

  app.querySelectorAll<HTMLInputElement>(".action-key").forEach((input) => {
    input.addEventListener("change", () => {
      const target = currentActionTarget();
      const idx = Number(input.dataset.actionIndex);
      const action = target.actions[idx];
      if (!action || action.type !== "combo") {
        return;
      }

      action.key = normalizeComboKey(input.value || "A");
      if (!action.key) {
        action.key = "A";
      }
      markMacroDirty(target.ownerIndex);
      render();
    });
  });

  app.querySelectorAll<HTMLInputElement>(".mod-toggle").forEach((checkbox) => {
    checkbox.addEventListener("change", () => {
      const target = currentActionTarget();
      const idx = Number(checkbox.dataset.actionIndex);
      const mod = checkbox.dataset.mod as Mod | undefined;
      const action = target.actions[idx];

      if (!action || action.type !== "combo" || !mod) {
        return;
      }

      const nextMods = new Set(action.mods);
      if (checkbox.checked) {
        nextMods.add(mod);
      } else {
        nextMods.delete(mod);
      }
      action.mods = Array.from(nextMods);
      markMacroDirty(target.ownerIndex);
    });
  });

  app.querySelectorAll<HTMLInputElement>(".action-delay").forEach((input) => {
    input.addEventListener("change", () => {
      const target = currentActionTarget();
      const idx = Number(input.dataset.actionIndex);
      const action = target.actions[idx];
      if (!action || action.type !== "delay") {
        return;
      }

      action.ms = clamp(Number(input.value) || 0, 0, 60000);
      markMacroDirty(target.ownerIndex);
      render();
    });
  });

  const addHostActionBtn = app.querySelector<HTMLButtonElement>("#addHostActionBtn");
  addHostActionBtn?.addEventListener("click", () => {
    const target = currentHostActionTarget();
    target.hostActions.push({
      type: "command",
      label: "",
      program: "",
      args: [],
      cwd: null,
      runDetached: false,
    });
    markMacroDirty(target.ownerIndex);
    render();
  });

  app.querySelectorAll<HTMLButtonElement>(".remove-host-action").forEach((btn) => {
    btn.addEventListener("click", () => {
      const target = currentHostActionTarget();
      const idx = Number(btn.dataset.hostActionIndex);
      if (!Number.isInteger(idx) || idx < 0 || idx >= target.hostActions.length) {
        return;
      }

      target.hostActions.splice(idx, 1);
      markMacroDirty(target.ownerIndex);
      render();
    });
  });

  app.querySelectorAll<HTMLInputElement>(".host-action-label").forEach((input) => {
    input.addEventListener("change", () => {
      const target = currentHostActionTarget();
      const idx = Number(input.dataset.hostActionIndex);
      const action = target.hostActions[idx];
      if (!action) {
        return;
      }

      action.label = input.value.trim();
      markMacroDirty(target.ownerIndex);
    });
  });

  app.querySelectorAll<HTMLInputElement>(".host-action-program").forEach((input) => {
    input.addEventListener("change", () => {
      const target = currentHostActionTarget();
      const idx = Number(input.dataset.hostActionIndex);
      const action = target.hostActions[idx];
      if (!action) {
        return;
      }

      action.program = input.value.trim();
      markMacroDirty(target.ownerIndex);
    });
  });

  app.querySelectorAll<HTMLInputElement>(".host-action-cwd").forEach((input) => {
    input.addEventListener("change", () => {
      const target = currentHostActionTarget();
      const idx = Number(input.dataset.hostActionIndex);
      const action = target.hostActions[idx];
      if (!action) {
        return;
      }

      const trimmed = input.value.trim();
      action.cwd = trimmed ? trimmed : null;
      markMacroDirty(target.ownerIndex);
    });
  });

  app.querySelectorAll<HTMLInputElement>(".host-action-detached").forEach((input) => {
    input.addEventListener("change", () => {
      const target = currentHostActionTarget();
      const idx = Number(input.dataset.hostActionIndex);
      const action = target.hostActions[idx];
      if (!action) {
        return;
      }

      action.runDetached = input.checked;
      markMacroDirty(target.ownerIndex);
    });
  });

  app.querySelectorAll<HTMLTextAreaElement>(".host-action-args").forEach((input) => {
    input.addEventListener("change", () => {
      const target = currentHostActionTarget();
      const idx = Number(input.dataset.hostActionIndex);
      const action = target.hostActions[idx];
      if (!action) {
        return;
      }

      action.args = multilineToHostArgs(input.value);
      markMacroDirty(target.ownerIndex);
    });
  });

  const clearLogsBtn = app.querySelector<HTMLButtonElement>("#clearLogsBtn");
  clearLogsBtn?.addEventListener("click", () => {
    state.logs = [];
    render();
  });
}

function rgb565BytesToPreviewDataUrl(bytes: number[]): string {
  if (bytes.length !== ICON_BYTE_SIZE) {
    throw new Error(`RGB565 payload must be ${ICON_BYTE_SIZE} bytes, got ${bytes.length}`);
  }

  const canvas = document.createElement("canvas");
  canvas.width = ICON_SIZE;
  canvas.height = ICON_SIZE;

  const ctx = canvas.getContext("2d", { willReadFrequently: true });
  if (!ctx) {
    throw new Error("2D canvas context unavailable");
  }

  const imageData = ctx.createImageData(ICON_SIZE, ICON_SIZE);
  const rgba = imageData.data;

  let src = 0;
  let dst = 0;
  while (src < bytes.length) {
    const lo = bytes[src++] ?? 0;
    const hi = bytes[src++] ?? 0;
    const rgb565 = lo | (hi << 8);

    const r5 = (rgb565 >> 11) & 0x1f;
    const g6 = (rgb565 >> 5) & 0x3f;
    const b5 = rgb565 & 0x1f;

    rgba[dst++] = Math.round((r5 * 255) / 31);
    rgba[dst++] = Math.round((g6 * 255) / 63);
    rgba[dst++] = Math.round((b5 * 255) / 31);
    rgba[dst++] = 255;
  }

  ctx.putImageData(imageData, 0, 0);
  return canvas.toDataURL("image/png");
}

async function convertImageToRgb565(file: File): Promise<{ previewDataUrl: string; bytes: number[] }> {
  const image = await loadImage(file);
  const canvas = document.createElement("canvas");
  canvas.width = ICON_SIZE;
  canvas.height = ICON_SIZE;

  const ctx = canvas.getContext("2d", { willReadFrequently: true });
  if (!ctx) {
    throw new Error("2D canvas context unavailable");
  }

  ctx.fillStyle = "#000000";
  ctx.fillRect(0, 0, ICON_SIZE, ICON_SIZE);
  ctx.drawImage(image, 0, 0, ICON_SIZE, ICON_SIZE);

  const rgba = ctx.getImageData(0, 0, ICON_SIZE, ICON_SIZE).data;
  const output = new Uint8Array(ICON_BYTE_SIZE);

  let out = 0;
  for (let i = 0; i < rgba.length; i += 4) {
    const r5 = rgba[i] >> 3;
    const g6 = rgba[i + 1] >> 2;
    const b5 = rgba[i + 2] >> 3;
    const rgb565 = (r5 << 11) | (g6 << 5) | b5;

    output[out++] = rgb565 & 0xff;
    output[out++] = (rgb565 >> 8) & 0xff;
  }

  if (output.length !== ICON_BYTE_SIZE) {
    throw new Error(`Unexpected output size ${output.length}`);
  }

  return {
    previewDataUrl: canvas.toDataURL("image/png"),
    bytes: Array.from(output),
  };
}

function loadImage(file: File): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();

    reader.onerror = () => {
      reject(new Error("Failed to read image file"));
    };

    reader.onload = () => {
      const img = new Image();
      img.onload = () => resolve(img);
      img.onerror = () => reject(new Error("Selected file is not a valid image"));
      img.src = String(reader.result ?? "");
    };

    reader.readAsDataURL(file);
  });
}

async function refreshPorts(): Promise<void> {
  try {
    const result = await invoke<ProbePortsResult>("probe_serial_ports", {
      baudRate: state.baudRate,
    });

    state.ports = result.ports.map((p) => p.portName);
    state.portInfo = Object.fromEntries(result.ports.map((p) => [p.portName, p]));

    if (result.suggestedPort) {
      state.selectedPort = result.suggestedPort;
    } else if (!state.ports.includes(state.selectedPort) || !isPortResponsive(state.selectedPort)) {
      state.selectedPort = "";
    }

    await syncCompanionSettings();

    const responsiveCount = result.ports.filter((p) => p.responsive).length;
    appendLog(`Detected ${result.ports.length} serial port(s), ${responsiveCount} responsive`);
    if (state.selectedPort) {
      appendLog(`Auto-selected port: ${state.selectedPort}`);
    }

    if (responsiveCount === 0) {
      appendLog("No responsive CDC device detected. Reconnect the ESP32 and refresh ports.");
      for (const line of result.logs) {
        appendLog(line);
      }
    }

    for (const port of result.ports) {
      if (!port.responsive) {
        appendLog(`Probe ${port.portName}: ${port.detail}`);
      }
    }

    state.error = null;
  } catch (error) {
    state.error = `Failed to probe serial ports: ${String(error)}`;
    appendLog(state.error);
  }

  render();
}

async function sendChanges(): Promise<void> {
  if (state.sending || state.syncing) {
    return;
  }

  if (!state.selectedPort) {
    await refreshPorts();
    if (!state.selectedPort) {
      state.error = "Select a serial port before sending changes.";
      render();
      return;
    }
  }

  if (!isPortResponsive(state.selectedPort)) {
    appendLog(`Selected port ${state.selectedPort} is not confirmed responsive, probing again...`);
    await refreshPorts();
    if (!state.selectedPort) {
      state.error = "No serial port found. Reconnect device and refresh ports.";
      render();
      return;
    }

    if (!isPortResponsive(state.selectedPort)) {
      state.error = `Selected port ${state.selectedPort} did not respond to the CDC protocol.`;
      appendLog(state.error);
      render();
      return;
    }
  }

  if (!hasPendingChanges()) {
    state.error = "There are no pending changes to send.";
    render();
    return;
  }

  const iconUploads: IconUpload[] = state.icons
    .filter((icon) => icon.imageDirty && icon.imageBytes !== null)
    .map((icon) => ({
      index: icon.index,
      row: icon.row,
      col: icon.col,
      direction: null,
      bytes: icon.imageBytes ?? [],
    }));

  for (const icon of state.icons) {
    for (const item of icon.radialItems) {
      if (item.imageDirty && item.imageBytes !== null) {
        iconUploads.push({
          index: icon.index,
          row: icon.row,
          col: icon.col,
          direction: item.direction,
          bytes: item.imageBytes,
        });
      }
    }
  }

  for (const icon of iconUploads) {
    if (icon.bytes.length !== ICON_BYTE_SIZE) {
      const label = icon.direction ? `Radial ${icon.index}/${icon.direction}` : `Icon ${icon.index}`;
      state.error = `${label} has invalid byte size (${icon.bytes.length}).`;
      render();
      return;
    }
  }

  const macrosJson = state.macrosDirty ? JSON.stringify(buildMacroDocument(), null, 2) : null;

  const payload: SendUpdatesPayload = {
    portName: state.selectedPort,
    baudRate: state.baudRate,
    iconUploads,
    macrosJson,
    sendReloadAll: true,
  };

  const totalUploadBytes = iconUploads.reduce((total, icon) => total + icon.bytes.length, 0)
    + (macrosJson ? new TextEncoder().encode(macrosJson).length : 0);
  const fileCount = iconUploads.length + (macrosJson ? 1 : 0);

  state.sending = true;
  state.error = null;
  state.uploadProgress = {
    phase: "preparing",
    currentBytes: 0,
    totalBytes: totalUploadBytes,
    fileBytesSent: 0,
    fileTotalBytes: 0,
    filePath: "",
    fileIndex: 0,
    fileCount,
  };
  appendLog(`Starting upload to ${state.selectedPort} (${iconUploads.length} icon file(s), macros ${macrosJson ? "yes" : "no"})`);
  render();

  try {
    const result = await invoke<SendUpdatesResult>("send_updates", { request: payload });

    for (const line of result.logs) {
      appendLog(line);
    }

    for (const icon of state.icons) {
      if (icon.imageDirty) {
        icon.imageDirty = false;
      }
      for (const item of icon.radialItems) {
        if (item.imageDirty) {
          item.imageDirty = false;
        }
      }
    }

    if (state.macrosDirty) {
      state.macrosDirty = false;
      for (const icon of state.icons) {
        icon.macroDirty = false;
      }
    }

    appendLog("Upload completed successfully");
  } catch (error) {
    const rawError = String(error);
    const splitToken = "---LOGS---\n";
    const splitIndex = rawError.indexOf(splitToken);

    if (splitIndex >= 0) {
      const summary = rawError.slice(0, splitIndex).trim();
      const logsPart = rawError.slice(splitIndex + splitToken.length);
      state.error = `Send failed: ${summary}`;
      for (const line of logsPart.split(/\r?\n/)) {
        if (line.trim()) {
          appendLog(line.trim());
        }
      }
    } else {
      state.error = `Send failed: ${rawError}`;
    }
    appendLog(state.error);
  } finally {
    state.sending = false;
    state.uploadProgress = null;
    render();
  }
}

async function syncFromDevice(): Promise<void> {
  if (state.sending || state.syncing) {
    return;
  }

  if (!state.selectedPort) {
    await refreshPorts();
    if (!state.selectedPort) {
      state.error = "Select a serial port before syncing from device.";
      render();
      return;
    }
  }

  if (!isPortResponsive(state.selectedPort)) {
    appendLog(`Selected port ${state.selectedPort} is not confirmed responsive, probing again...`);
    await refreshPorts();
    if (!state.selectedPort) {
      state.error = "No serial port found. Reconnect device and refresh ports.";
      render();
      return;
    }

    if (!isPortResponsive(state.selectedPort)) {
      state.error = `Selected port ${state.selectedPort} did not respond to the CDC protocol.`;
      appendLog(state.error);
      render();
      return;
    }
  }

  if (hasPendingChanges()) {
    const confirmed = window.confirm(
      "Sync from device will overwrite unsent local changes in the editor. Continue?",
    );
    if (!confirmed) {
      appendLog("Sync cancelled: kept local unsent changes.");
      return;
    }
  }

  const payload: SyncFromDevicePayload = {
    portName: state.selectedPort,
    baudRate: state.baudRate,
  };

  state.syncing = true;
  state.error = null;
  appendLog(`Starting sync from ${state.selectedPort}`);
  render();

  try {
    const result = await invoke<SyncFromDeviceResult>("sync_from_device", { request: payload });

    for (const line of result.logs) {
      appendLog(line);
    }

    const macroDoc = JSON.parse(result.macrosJson);
    applyMacroDocument(macroDoc, false);

    const iconMap = new Map<number, number[]>();
    const radialIconMap = new Map<string, number[]>();
    for (const icon of result.iconDownloads) {
      const index = icon.row * GRID_COLS + icon.col;
      if (index >= 0 && index < GRID_SIZE && icon.bytes.length === ICON_BYTE_SIZE) {
        if (icon.direction) {
          radialIconMap.set(`${index}:${icon.direction}`, icon.bytes);
        } else {
          iconMap.set(index, icon.bytes);
        }
      }
    }

    let fallbackPreview: string | null = null;
    if (result.fallbackBytes && result.fallbackBytes.length === ICON_BYTE_SIZE) {
      fallbackPreview = rgb565BytesToPreviewDataUrl(result.fallbackBytes);
    }

    state.icons = state.icons.map((icon) => {
      const bytes = iconMap.get(icon.index) ?? null;
      const previewDataUrl = bytes ? rgb565BytesToPreviewDataUrl(bytes) : fallbackPreview;

      return {
        ...icon,
        previewDataUrl,
        imageBytes: null,
        imageDirty: false,
        radialItems: icon.radialItems.map((item) => {
          const radialBytes = radialIconMap.get(`${icon.index}:${item.direction}`) ?? null;
          return {
            ...item,
            previewDataUrl: radialBytes
              ? rgb565BytesToPreviewDataUrl(radialBytes)
              : item.actions.length > 0 || item.hostActions.length > 0
                ? fallbackPreview
                : null,
            imageBytes: null,
            imageDirty: false,
          };
        }),
      };
    });

    state.macrosDirty = false;
    appendLog(`Sync completed (${result.iconDownloads.length} icon file(s) fetched)`);
  } catch (error) {
    const rawError = String(error);
    const splitToken = "---LOGS---\n";
    const splitIndex = rawError.indexOf(splitToken);

    if (splitIndex >= 0) {
      const summary = rawError.slice(0, splitIndex).trim();
      const logsPart = rawError.slice(splitIndex + splitToken.length);
      state.error = `Sync failed: ${summary}`;
      for (const line of logsPart.split(/\r?\n/)) {
        if (line.trim()) {
          appendLog(line.trim());
        }
      }
    } else {
      state.error = `Sync failed: ${rawError}`;
    }
    appendLog(state.error);
  } finally {
    state.syncing = false;
    render();
  }
}

async function initializeApp(): Promise<void> {
  render();
  await setupUploadProgressListener();
  await setupCompanionListeners();
  await loadCompanionStatus();
  render();
  await refreshPorts();
}

void initializeApp();
