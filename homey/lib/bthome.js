'use strict';

const { inRange } = require('./measurements');

function isBTHomeService(uuid) {
  const normalized = String(uuid).toLowerCase().replaceAll('-', '');
  return normalized === 'fcd2' || normalized === '0000fcd200001000800000805f9b34fb';
}

function servicePayload(advertisement) {
  if (!Array.isArray(advertisement?.serviceData)) return null;
  const service = advertisement.serviceData.find(entry => isBTHomeService(entry.uuid));
  return service && Buffer.isBuffer(service.data) ? service.data : null;
}

function decode(payload) {
  if (!Buffer.isBuffer(payload) || payload.length < 1) return null;
  if ((payload[0] & 0xE0) !== 0x40 || (payload[0] & 0x1B) !== 0) return null;
  const values = {};
  const seen = new Set();
  let packetId;
  let offset = 1;
  while (offset < payload.length) {
    const id = payload[offset++];
    const size = { 0x00: 1, 0x02: 2, 0x03: 2, 0x04: 3, 0x12: 2 }[id];
    if (!size || seen.has(id) || offset + size > payload.length) return null;
    seen.add(id);
    switch (id) {
      case 0x00:
        packetId = payload[offset];
        break;
      case 0x02: {
        const temperature = payload.readInt16LE(offset) / 100;
        if (inRange(temperature, -40, 125)) values.measure_temperature = temperature;
        break;
      }
      case 0x03: {
        const humidity = payload.readUInt16LE(offset) / 100;
        if (inRange(humidity, 0, 100)) values.measure_humidity = humidity;
        break;
      }
      case 0x04: {
        const pressure = payload.readUIntLE(offset, 3) / 100;
        if (inRange(pressure, 300, 1250)) values.measure_pressure = pressure;
        break;
      }
      case 0x12: {
        const co2 = payload.readUInt16LE(offset);
        if (inRange(co2, 1, 10000)) values.measure_co2 = co2;
        break;
      }
      default: break;
    }
    offset += size;
  }
  return { packetId, values };
}

function identify(advertisement) {
  if (advertisement?.localName !== 'AirBee' || !advertisement.uuid) return null;
  const payload = servicePayload(advertisement);
  return decode(payload) ? { id: advertisement.uuid, payload } : null;
}

module.exports = { decode, identify, servicePayload };
