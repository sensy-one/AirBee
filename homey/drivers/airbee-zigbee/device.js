'use strict';

const { ZigBeeDevice } = require('homey-zigbeedriver');
const { CLUSTER } = require('zigbee-clusters');
const CarbonDioxideCluster = require('../../lib/CarbonDioxideCluster');
const { zigbeeParsers } = require('../../lib/measurements');

module.exports = class AirBeeZigbeeDevice extends ZigBeeDevice {
  async onInit() {
    this._receivedMeasurement = false;
    await this.setUnavailable(this.homey.__('sensor.waiting'));
    await super.onInit();
  }

  async setAvailable() {
    if (this._receivedMeasurement) return super.setAvailable();
  }

  async setCapabilityValue(capability, value) {
    await super.setCapabilityValue(capability, value);
    if (Object.hasOwn(zigbeeParsers, capability) && Number.isFinite(value)) {
      this._receivedMeasurement = true;
      await this.setAvailable();
    }
  }

  async onNodeInit() {
    this._receivedMeasurement = false;
    await this.setUnavailable(this.homey.__('sensor.waiting'));
    const mappings = [
      ['measure_co2', CarbonDioxideCluster],
      ['measure_temperature', CLUSTER.TEMPERATURE_MEASUREMENT],
      ['measure_humidity', CLUSTER.RELATIVE_HUMIDITY_MEASUREMENT],
      ['measure_pressure', CLUSTER.PRESSURE_MEASUREMENT],
    ];

    for (const [capability, cluster] of mappings) {
      this.registerCapability(capability, cluster, {
        endpoint: 1,
        get: 'measuredValue',
        report: 'measuredValue',
        reportParser: zigbeeParsers[capability],
      });
    }
  }
};
