{
  "fileFormat": 2,
  "featureLevel": 99,
  "creator": "zap",
  "keyValuePairs": [
    { "key": "commandDiscovery", "value": "1" },
    { "key": "defaultResponsePolicy", "value": "always" },
    { "key": "manufacturerCodes", "value": "0x1049" }
  ],
  "package": [
    {
      "pathRelativity": "relativeToZap",
      "path": "../../../../../../../../../app/zcl/zcl-zap.json",
      "type": "zcl-properties",
      "category": "zigbee",
      "version": 1,
      "description": "Zigbee Silabs ZCL data"
    },
    {
      "pathRelativity": "relativeToZap",
      "path": "../../../../../gen-template/gen-templates.json",
      "type": "gen-templates-json",
      "category": "zigbee",
      "version": "zigbee-v0"
    }
  ],
  "endpointTypes": [
    {
      "id": 1,
      "name": "AirBee environmental sensor",
      "deviceTypeRef": {
        "code": 770,
        "profileId": 260,
        "label": "HA Temperature Sensor",
        "name": "HA-tempsensor"
      },
      "deviceTypes": [
        {
          "code": 770,
          "profileId": 260,
          "label": "HA Temperature Sensor",
          "name": "HA-tempsensor"
        }
      ],
      "deviceVersions": [1],
      "deviceIdentifiers": [770],
      "deviceTypeName": "HA Temperature Sensor",
      "deviceTypeCode": 770,
      "deviceTypeProfileId": 260,
      "clusters": [
        {
          "name": "Basic",
          "code": 0,
          "mfgCode": null,
          "define": "BASIC_CLUSTER",
          "side": "server",
          "enabled": 1,
          "attributes": [
            { "name": "ZCL version", "code": 0, "mfgCode": null, "side": "server", "type": "int8u", "included": 1, "storageOption": "RAM", "singleton": 1, "bounded": 0, "defaultValue": "0x08", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 },
            { "name": "application version", "code": 1, "mfgCode": null, "side": "server", "type": "int8u", "included": 1, "storageOption": "RAM", "singleton": 1, "bounded": 0, "defaultValue": "0x01", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 },
            { "name": "sw build id", "code": 16384, "mfgCode": null, "side": "server", "type": "char_string", "included": 1, "storageOption": "RAM", "singleton": 1, "bounded": 0, "defaultValue": "1.0.0", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 },
            { "name": "hardware version", "code": 3, "mfgCode": null, "side": "server", "type": "int8u", "included": 1, "storageOption": "RAM", "singleton": 1, "bounded": 0, "defaultValue": "0x01", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 },
            { "name": "manufacturer name", "code": 4, "mfgCode": null, "side": "server", "type": "char_string", "included": 1, "storageOption": "RAM", "singleton": 1, "bounded": 0, "defaultValue": "SENSY-ONE", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 },
            { "name": "model identifier", "code": 5, "mfgCode": null, "side": "server", "type": "char_string", "included": 1, "storageOption": "RAM", "singleton": 1, "bounded": 0, "defaultValue": "AirBee", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 },
            { "name": "power source", "code": 7, "mfgCode": null, "side": "server", "type": "enum8", "included": 1, "storageOption": "RAM", "singleton": 1, "bounded": 0, "defaultValue": "0x03", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 },
            { "name": "cluster revision", "code": 65533, "mfgCode": null, "side": "server", "type": "int16u", "included": 1, "storageOption": "RAM", "singleton": 1, "bounded": 0, "defaultValue": "3", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 }
          ]
        },
        {
          "name": "Identify",
          "code": 3,
          "mfgCode": null,
          "define": "IDENTIFY_CLUSTER",
          "side": "client",
          "enabled": 1,
          "commands": [
            { "name": "Identify", "code": 0, "mfgCode": null, "source": "client", "isIncoming": 0, "isEnabled": 1 },
            { "name": "IdentifyQueryResponse", "code": 0, "mfgCode": null, "source": "server", "isIncoming": 1, "isEnabled": 1 },
            { "name": "IdentifyQuery", "code": 1, "mfgCode": null, "source": "client", "isIncoming": 0, "isEnabled": 1 },
            { "name": "TriggerEffect", "code": 64, "mfgCode": null, "source": "client", "isIncoming": 0, "isEnabled": 1 }
          ],
          "attributes": [
            { "name": "cluster revision", "code": 65533, "mfgCode": null, "side": "client", "type": "int16u", "included": 1, "storageOption": "RAM", "singleton": 0, "bounded": 0, "defaultValue": "2", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 }
          ]
        },
        {
          "name": "Identify",
          "code": 3,
          "mfgCode": null,
          "define": "IDENTIFY_CLUSTER",
          "side": "server",
          "enabled": 1,
          "commands": [
            { "name": "Identify", "code": 0, "mfgCode": null, "source": "client", "isIncoming": 1, "isEnabled": 1 },
            { "name": "IdentifyQueryResponse", "code": 0, "mfgCode": null, "source": "server", "isIncoming": 0, "isEnabled": 1 },
            { "name": "IdentifyQuery", "code": 1, "mfgCode": null, "source": "client", "isIncoming": 1, "isEnabled": 1 },
            { "name": "TriggerEffect", "code": 64, "mfgCode": null, "source": "client", "isIncoming": 1, "isEnabled": 1 }
          ],
          "attributes": [
            { "name": "identify time", "code": 0, "mfgCode": null, "side": "server", "type": "int16u", "included": 1, "storageOption": "RAM", "singleton": 0, "bounded": 0, "defaultValue": "0x0000", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 },
            { "name": "cluster revision", "code": 65533, "mfgCode": null, "side": "server", "type": "int16u", "included": 1, "storageOption": "RAM", "singleton": 0, "bounded": 0, "defaultValue": "2", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 }
          ]
        },
        {
          "name": "Temperature Measurement",
          "code": 1026,
          "mfgCode": null,
          "define": "TEMP_MEASUREMENT_CLUSTER",
          "side": "server",
          "enabled": 1,
          "attributes": [
            { "name": "measured value", "code": 0, "mfgCode": null, "side": "server", "type": "int16s", "included": 1, "storageOption": "RAM", "singleton": 0, "bounded": 0, "defaultValue": "0x8000", "reportable": 1, "minInterval": 300, "maxInterval": 300, "reportableChange": 1 },
            { "name": "min measured value", "code": 1, "mfgCode": null, "side": "server", "type": "int16s", "included": 1, "storageOption": "RAM", "singleton": 0, "bounded": 0, "defaultValue": "-4500", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 },
            { "name": "max measured value", "code": 2, "mfgCode": null, "side": "server", "type": "int16s", "included": 1, "storageOption": "RAM", "singleton": 0, "bounded": 0, "defaultValue": "13000", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 },
            { "name": "cluster revision", "code": 65533, "mfgCode": null, "side": "server", "type": "int16u", "included": 1, "storageOption": "RAM", "singleton": 1, "bounded": 0, "defaultValue": "3", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 }
          ]
        },
        {
          "name": "Pressure Measurement",
          "code": 1027,
          "mfgCode": null,
          "define": "PRESSURE_MEASUREMENT_CLUSTER",
          "side": "server",
          "enabled": 1,
          "attributes": [
            { "name": "measured value", "code": 0, "mfgCode": null, "side": "server", "type": "int16s", "included": 1, "storageOption": "RAM", "singleton": 0, "bounded": 0, "defaultValue": "0x8000", "reportable": 1, "minInterval": 300, "maxInterval": 300, "reportableChange": 1 },
            { "name": "min measured value", "code": 1, "mfgCode": null, "side": "server", "type": "int16s", "included": 1, "storageOption": "RAM", "singleton": 0, "bounded": 0, "defaultValue": "300", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 },
            { "name": "max measured value", "code": 2, "mfgCode": null, "side": "server", "type": "int16s", "included": 1, "storageOption": "RAM", "singleton": 0, "bounded": 0, "defaultValue": "1250", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 },
            { "name": "cluster revision", "code": 65533, "mfgCode": null, "side": "server", "type": "int16u", "included": 1, "storageOption": "RAM", "singleton": 1, "bounded": 0, "defaultValue": "2", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 }
          ]
        },
        {
          "name": "Relative Humidity Measurement",
          "code": 1029,
          "mfgCode": null,
          "define": "RELATIVE_HUMIDITY_MEASUREMENT_CLUSTER",
          "side": "server",
          "enabled": 1,
          "attributes": [
            { "name": "measured value", "code": 0, "mfgCode": null, "side": "server", "type": "int16u", "included": 1, "storageOption": "RAM", "singleton": 0, "bounded": 0, "defaultValue": "0xFFFF", "reportable": 1, "minInterval": 300, "maxInterval": 300, "reportableChange": 1 },
            { "name": "min measured value", "code": 1, "mfgCode": null, "side": "server", "type": "int16u", "included": 1, "storageOption": "RAM", "singleton": 0, "bounded": 0, "defaultValue": "0", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 },
            { "name": "max measured value", "code": 2, "mfgCode": null, "side": "server", "type": "int16u", "included": 1, "storageOption": "RAM", "singleton": 0, "bounded": 0, "defaultValue": "10000", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 },
            { "name": "cluster revision", "code": 65533, "mfgCode": null, "side": "server", "type": "int16u", "included": 1, "storageOption": "RAM", "singleton": 1, "bounded": 0, "defaultValue": "2", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 }
          ]
        },
        {
          "name": "Carbon Dioxide Concentration Measurement",
          "code": 1037,
          "mfgCode": null,
          "define": "CARBON_DIOXIDE_CONCENTRATION_MEASUREMENT_CLUSTER",
          "side": "server",
          "enabled": 1,
          "attributes": [
            { "name": "measured value", "code": 0, "mfgCode": null, "side": "server", "type": "float_single", "included": 1, "storageOption": "RAM", "singleton": 0, "bounded": 0, "defaultValue": "", "reportable": 1, "minInterval": 300, "maxInterval": 300, "reportableChange": 0 },
            { "name": "min measured value", "code": 1, "mfgCode": null, "side": "server", "type": "float_single", "included": 1, "storageOption": "RAM", "singleton": 0, "bounded": 0, "defaultValue": "", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 },
            { "name": "max measured value", "code": 2, "mfgCode": null, "side": "server", "type": "float_single", "included": 1, "storageOption": "RAM", "singleton": 0, "bounded": 0, "defaultValue": "", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 },
            { "name": "cluster revision", "code": 65533, "mfgCode": null, "side": "server", "type": "int16u", "included": 1, "storageOption": "RAM", "singleton": 1, "bounded": 0, "defaultValue": "2", "reportable": 0, "minInterval": 1, "maxInterval": 65534, "reportableChange": 0 }
          ]
        }
      ]
    }
  ],
  "endpoints": [
    {
      "endpointTypeName": "AirBee environmental sensor",
      "endpointTypeIndex": 0,
      "profileId": 260,
      "endpointId": 1,
      "networkId": 0
    }
  ]
}
