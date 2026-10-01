'use strict';

const Homey = require('homey');
const { identify } = require('../../lib/bthome');

const SEARCH_WINDOW_MS = 6 * 60 * 1000;

module.exports = class AirBeeBleDriver extends Homey.Driver {
  async onPair(session) {
    let disconnected = false;
    let search;
    const cancel = () => search?.finish(null, []);

    session.setHandler('disconnect', () => {
      disconnected = true;
      cancel();
    });
    session.setHandler('showView', async view => {
      if (view !== 'list_devices') cancel();
    });
    session.setHandler('list_devices', () => {
      if (disconnected) return Promise.resolve([]);
      if (!this.homey.hasFeature('ble-advertisements')) {
        throw new Error(this.homey.__('ble.unsupported'));
      }
      if (search) return search.promise;

      const current = { stopped: false };
      current.promise = new Promise((resolve, reject) => {
        current.finish = (error, devices) => {
          if (current.stopped) return;
          current.stopped = true;
          this.homey.clearTimeout(current.deadline);
          this.homey.clearTimeout(current.retry);
          if (search === current) search = null;
          if (error) reject(error);
          else resolve(devices);
        };
      });
      search = current;
      current.deadline = this.homey.setTimeout(() => current.finish(null, []), SEARCH_WINDOW_MS);

      const scan = async () => {
        try {
          const advertisements = await this.homey.ble.discover();
          if (current.stopped) return;
          const paired = new Set(this.getDevices().map(device => device.getData().id));
          const devices = new Map();
          for (const advertisement of advertisements) {
            const match = identify(advertisement);
            if (!match || paired.has(match.id)) continue;
            devices.set(match.id, {
              name: 'AirBee BLE',
              data: { id: match.id },
              store: {
                peripheralUuid: match.id,
                initialAdvertisement: {
                  payload: match.payload.toString('hex'),
                  receivedAt: advertisement.timestamp ?? Date.now(),
                },
              },
            });
          }
          if (devices.size) {
            current.finish(null, [...devices.values()]);
          } else {
            current.retry = this.homey.setTimeout(() => { void scan(); }, 1000);
          }
        } catch (error) {
          if (current.stopped) return;
          this.error('BLE discovery failed', error);
          current.finish(error);
        }
      };
      void scan();
      return current.promise;
    });
  }
};
