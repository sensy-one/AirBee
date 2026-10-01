'use strict';

function inRange(value, minimum, maximum) {
  return Number.isFinite(value) && value >= minimum && value <= maximum;
}

const zigbeeParsers = {
  measure_co2: value => inRange(value, 0.000001, 0.01)
    ? Math.round(value * 1000000) : null,
  measure_temperature: value => inRange(value, -4000, 12500) ? value / 100 : null,
  measure_humidity: value => inRange(value, 0, 10000) ? value / 100 : null,
  measure_pressure: value => inRange(value, 300, 1250) ? value : null,
};

module.exports = { inRange, zigbeeParsers };
