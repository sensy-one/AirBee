'use strict';

const { Cluster, ZCLDataTypes } = require('zigbee-clusters');

class CarbonDioxideCluster extends Cluster {
  static get ID() { return 0x040D; }
  static get NAME() { return 'carbonDioxideMeasurement'; }
  static get ATTRIBUTES() {
    return {
      measuredValue: { id: 0x0000, type: ZCLDataTypes.single },
      minMeasuredValue: { id: 0x0001, type: ZCLDataTypes.single },
      maxMeasuredValue: { id: 0x0002, type: ZCLDataTypes.single },
      tolerance: { id: 0x0003, type: ZCLDataTypes.single },
    };
  }
}

Cluster.addCluster(CarbonDioxideCluster);
module.exports = CarbonDioxideCluster;
