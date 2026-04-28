// Catan BLE service / characteristic UUIDs (must match firmware/hub/src/config.h).
export const CATAN_SERVICE_UUID  = 'CA7A0001-CA7A-4C4E-8000-00805F9B34FB';
export const GAME_STATE_UUID     = 'CA7A0002-CA7A-4C4E-8000-00805F9B34FB';
export const COMMAND_UUID        = 'CA7A0003-CA7A-4C4E-8000-00805F9B34FB';
export const IDENTITY_UUID       = 'CA7A0004-CA7A-4C4E-8000-00805F9B34FB';
export const SLOT_UUID           = 'CA7A0005-CA7A-4C4E-8000-00805F9B34FB';

/** Single advertised name from the ESP32-C6 hub. */
export const CATAN_DEVICE_NAME   = 'Catan-Board';

/** AsyncStorage key for the persistent per-device client_id. */
export const CLIENT_ID_STORAGE_KEY = 'catan.clientId';

export const SCAN_TIMEOUT_MS = 12000;
