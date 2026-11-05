export type SignalState = 'ready' | 'connecting' | 'live' | 'offline';
export type PlayerId = 'preview' | 'output';

export interface InputStatus {
  id: string;
  name: string;
  state: SignalState;
  fpsIn: number;
  dropped: number;
  queued: number;
  coverage: Array<[number, number]>;
}

export interface DiskStatus {
  freeBytes: number;
  secondsLeft: number | null;
  writeMBps: number;
}

export interface Alert {
  id: string;
  text: string;
  since: number;
}

export interface ZoomRect {
  cx: number;
  cy: number;
  scale: number;
}

export interface PlayerStatus {
  posUs: number;
  tc: string;
  eventId: number | null;
  angle: string;
  playing: boolean;
  speed: number;
  locked: boolean;
  zoom: ZoomRect;
}

export interface OutputPlayerStatus extends PlayerStatus {
  remainingS: number | null;
}

export type ExportCodec = 'h264' | 'hevc';
export type ExportQuality = 'high' | 'medium' | 'small' | 'custom';

export interface ExportState {
  id: number;
  name: string;
  state: 'queued' | 'running' | 'done' | 'failed' | 'cancelled';
  progress: number;
  error: string;
  path: string;
}

export interface Status {
  recording: boolean;
  rate?: { num: number; den: number };
  inputs: InputStatus[];
  disk: DiskStatus;
  alerts: Alert[];
  hotkeys?: Record<string, string>;
  players: { preview: PlayerStatus; output: OutputPlayerStatus };
  events: { version: number };
  edit?: { canUndo: boolean; canRedo: boolean };
  nativeVideo?: true | string;
  exports?: ExportState[];
  exportCodecs?: ExportCodec[];
  exportCodec?: ExportCodec;
  exportQuality?: ExportQuality;
  exportMbps?: number;
}

export interface AngleCut {
  tUs: number;
  inputId: string;
}

export interface SpeedPoint {
  tUs: number;
  speed: number;
}

export interface ZoomPoint {
  tUs: number;
  rect: ZoomRect;
}

export interface Event {
  id: number;
  listId: string;
  name: string;
  tags: string[];
  angleNotes: Record<string, string>;
  inUs: number;
  outUs: number;
  angles: AngleCut[];
  speeds: SpeedPoint[];
  zooms: ZoomPoint[];
}

export interface EventList {
  id: string;
  name: string;
  order: number[];
}

export interface EventStore {
  version: number;
  nextId: number;
  nextListId: number;
  currentList: string;
  preRollUs: number;
  postRollUs: number;
  defaultSpeed: number;
  rootOrder: number[];
  lists: EventList[];
  events: Event[];
}

export interface OutputConfig {
  id: string;
  kind: string;
  keyed: boolean;
  enabled: boolean;
  params: Record<string, unknown>;
}

export interface ShowSettings {
  rate: string;
  outW: number;
  outH: number;
  quality: 'LQ' | 'SQ' | 'HQ';
  lastNSeconds: number;
  preRollS: number;
  postRollS: number;
  defaultSpeed: number;
  transition: 'fade' | 'cut';
  fadeS: number;
  lowSpaceMinutes: number;
  limit?: { hours?: number; bytes?: number };
  endAction: 'hold' | 'slate';
  slatePath: string;
  hotkeys: Record<string, string>;
  recordingFolder: string;
  exportFolder: string;
  exportCodec: ExportCodec;
  exportQuality: ExportQuality;
  exportMbps: number;
}