'use strict';

const Homey = require('homey');
const { decode, servicePayload } = require('../../lib/bthome');
const scanner = require('../../lib/ble-scanner');

const STALE_AFTER_MS = 20 * 60 * 1000;
const RECOVERY_AFTER_MS = 6 * 60 * 1000;

module.exports = class AirBeeBleDevice extends Homey.Device {
  async onInit() {
    this._stopped = false;
    this._subscribed = false;
    this._subscribing = false;
    this._recovering = false;
    this._lastSeen = 0;
    this._signature = this.getStoreValue('lastAdvertisementSignature');
    this._startedAt = Date.now();
    this._lastRecoveryAt = this._startedAt;
    this._queue = Promise.resolve();
    this._uuid = this.getStoreValue('peripheralUuid') || this.getData().id;
    await this.setUnavailable(this.homey.__('sensor.waiting'));
    if (!this.homey.hasFeature('ble-advertisements')) {
      await this.setUnavailable(this.homey.__('ble.unsupported'));
      return;
    }
    const initial = this.getStoreValue('initialAdvertisement');
    if (initial) {
      try {
        await this.unsetStoreValue('initialAdvertisement');
        if (typeof initial.payload === 'string' && /^(?:[a-f\d]{2}){1,31}$/i.test(initial.payload)
          && this._isFresh(initial.receivedAt)) {
          await this._onAdvertisement({ serviceData: [
            { uuid: 'fcd2', data: Buffer.from(initial.payload, 'hex') },
          ] }, initial.receivedAt);
        }
      } catch (error) {
        this.error('Could not restore pairing measurement', error);
      }
    }
    this._healthTimer = this.homey.setInterval(() => {
      void this._checkHealth().catch(error => this.error(error));
    }, 60000);
    await this._subscribe();
    if (this._stopped) return;
    this._stopScan = scanner.subscribe(this.homey, this._uuid, advertisement => {
      this._queue = this._queue.then(() => this._onAdvertisement(advertisement, Date.now(), true))
        .catch(error => {
          this.error('BLE scan measurement failed', error);
        });
      return this._queue;
    }, error => {
      this.error('BLE discovery failed; retrying', error);
    });
  }

  async _subscribe() {
    if (this._stopped || this._subscribed || this._subscribing) return;
    this._subscribing = true;
    try {
      await this.homey.ble.subscribeToAdvertisements(
        this._uuid,
        { rateLimitMs: 1000 },
        advertisement => {
          if (this._stopped) return;
          this._queue = this._queue.then(() => this._onAdvertisement(advertisement))
            .catch(error => this.error('BLE measurement update failed', error));
        },
      );
      this._subscribed = true;
      if (this._stopped) await this._unsubscribe();
    } catch (error) {
      this.error('BLE subscription failed; retrying in one minute', error);
    } finally {
      this._subscribing = false;
    }
  }

  _isFresh(timestamp) {
    return Number.isFinite(timestamp) && timestamp <= Date.now()
      && Date.now() - timestamp <= RECOVERY_AFTER_MS;
  }

  async _onAdvertisement(advertisement, receivedAt = Date.now(), fromScan = false) {
    if (this._stopped) return;
    if (receivedAt < this._lastSeen) return;
    const payload = servicePayload(advertisement);
    const decoded = decode(payload);
    if (!decoded || Object.keys(decoded.values).length === 0) return;
    const signature = payload.toString('hex');
    if (fromScan && signature === this._signature) return;
    if (signature === this._signature && receivedAt - this._lastSeen < 10000) return;
    for (const [capability, value] of Object.entries(decoded.values)) {
      if (this._stopped) return;
      await this.setCapabilityValue(capability, value);
    }
    if (this._stopped) return;
    await this.setStoreValue('lastAdvertisementSignature', signature);
    if (this._stopped) return;
    this._signature = signature;
    this._lastSeen = receivedAt;
    await this.setAvailable();
  }

  async _checkHealth() {
    if (this._stopped) return;
    if (Date.now() - (this._lastSeen || this._startedAt) > STALE_AFTER_MS) {
      await this.setUnavailable(this.homey.__('ble.stale'));
    }
    await this._subscribe();
    if (this._stopped || this._subscribing || this._recovering || Date.now() - Math.max(
      this._lastSeen || this._startedAt, this._lastRecoveryAt,
    ) < RECOVERY_AFTER_MS) return;
    this._recovering = true;
    this._lastRecoveryAt = Date.now();
    try {
      this.log('No BLE measurement for six minutes; renewing advertisement reception');
      await this._unsubscribe();
      await this._subscribe();
    } catch (error) {
      this.error('BLE reception recovery failed', error);
    } finally {
      this._recovering = false;
    }
  }

  async _unsubscribe() {
    if (!this._subscribed) return;
    this._subscribed = false;
    try {
      await this.homey.ble.unsubscribeFromAdvertisements(this._uuid);
    } catch (error) {
      this.error('BLE unsubscribe failed', error);
    }
  }

  async onUninit() {
    this._stopped = true;
    if (this._healthTimer) this.homey.clearInterval(this._healthTimer);
    this._stopScan?.();
    await this._unsubscribe();
  }

  async onDeleted() {
    await this.onUninit();
  }
};
