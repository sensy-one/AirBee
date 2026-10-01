'use strict';

const scanners = new WeakMap();

function subscribe(homey, uuid, onAdvertisement, onError) {
  let scanner = scanners.get(homey);
  if (!scanner) {
    scanner = { listeners: new Set(), stopped: false };
    scanners.set(homey, scanner);
  }
  const listener = { uuid, onAdvertisement, onError };
  scanner.listeners.add(listener);

  if (!scanner.started) {
    scanner.started = true;
    const scan = async () => {
      try {
        const advertisements = await homey.ble.discover();
        if (scanner.stopped) return;
        for (const advertisement of advertisements) {
          for (const current of scanner.listeners) {
            if (advertisement.uuid !== current.uuid) continue;
            Promise.resolve().then(() => {
              if (!scanner.stopped && scanner.listeners.has(current)) {
                return current.onAdvertisement(advertisement);
              }
            }).catch(current.onError);
          }
        }
      } catch (error) {
        if (!scanner.stopped) {
          for (const current of scanner.listeners) current.onError(error);
        }
      } finally {
        if (!scanner.stopped) scanner.timer = homey.setTimeout(() => { void scan(); }, 1000);
      }
    };
    void scan();
  }

  return () => {
    scanner.listeners.delete(listener);
    if (scanner.listeners.size) return;
    scanner.stopped = true;
    if (scanner.timer) homey.clearTimeout(scanner.timer);
    if (scanners.get(homey) === scanner) scanners.delete(homey);
  };
}

module.exports = { subscribe };
