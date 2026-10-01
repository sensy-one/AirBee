'use strict';

const Homey = require('homey');

module.exports = class SensyOneApp extends Homey.App {
  async onInit() {
    this.log('SENSY-ONE AirBee started');
  }
};
