export const manifest: {
  id: string;
  name: string;
  version: string;
  apiVersion: string;
  platforms: ['darwin'];
  architectures: ['arm64'];
  transports: ['ble'];
  capabilities: { ptt: true; streamOnly: true };
  permissions: ['bluetooth', 'native-helper'];
  icon: string;
  ui: { entrypoint: string };
};
export function resolveAiPassportHelperPath(options?: {
  packageRoot?: string;
  overridePath?: string;
  resourcesPath?: string;
  workingDirectory?: string;
  existsSync?: (candidate: string) => boolean;
}): string;
export function createPluginRuntime(options?: {
  socket?: {
    readyState?: number;
    bufferedAmount?: number;
    send(value: string | Uint8Array): void;
    close?(): void;
  };
  spawnHelper?: (...args: any[]) => any;
}): {
  handleHostMessage(data: string): void;
  handleDeviceEvent(event: unknown): void;
  stopHelper(): void;
};
export function startWorker(): Promise<void>;
export function resolveWebSocketImplementation(): Promise<new (...args: any[]) => any>;
