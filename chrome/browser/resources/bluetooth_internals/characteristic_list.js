// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Javascript for CharacteristicList and CharacteristicListItem, served from
 *     chrome://bluetooth-internals/.
 */

cr.define('characteristic_list', function() {
  /** @const */ var ArrayDataModel = cr.ui.ArrayDataModel;
  /** @const */ var ExpandableList = expandable_list.ExpandableList;
  /** @const */ var ExpandableListItem = expandable_list.ExpandableListItem;
  /** @const */ var Snackbar = snackbar.Snackbar;
  /** @const */ var SnackbarType = snackbar.SnackbarType;

  /** Property names for the CharacteristicInfo fieldset */
  var INFO_PROPERTY_NAMES = {
    id: 'ID',
    'uuid.uuid': 'UUID',
  };

  /** Property names for the Properties fieldset. */
  var PROPERTIES_PROPERTY_NAMES = {
    broadcast: 'Broadcast',
    read: 'Read',
    write_without_response: 'Write Without Response',
    write: 'Write',
    notify: 'Notify',
    indicate: 'Indicate',
    authenticated_signed_writes: 'Authenticated Signed Writes',
    extended_properties: 'Extended Properties',
    reliable_write: 'Reliable Write',
    writable_auxiliaries: 'Writable Auxiliaries',
    read_encrypted: 'Read Encrypted',
    write_encrypted: 'Write Encrypted',
    read_encrypted_authenticated: 'Read Encrypted Authenticated',
    write_encrypted_authenticated: 'Write Encrypted Authenticated',
  };

  /**
   * A list item that displays the properties of a CharacteristicInfo object.
   * Two fieldsets are created within the element: one for the primitive
   * properties, 'id' and 'uuid', and one for the 'properties' bitfield in the
   * CharacteristicInfo object.
   * @constructor
   * @param {!interfaces.BluetoothDevice.CharacteristicInfo} characteristicInfo
   */
  function CharacteristicListItem(characteristicInfo) {
    var listItem = new ExpandableListItem();
    listItem.__proto__ = CharacteristicListItem.prototype;

    listItem.info = characteristicInfo;
    listItem.decorate();

    return listItem;
  }

  CharacteristicListItem.prototype = {
    __proto__: ExpandableListItem.prototype,

    /**
     * Decorates the element as a characteristic list item. Creates and caches
     * two fieldsets for displaying property values.
     * @override
     */
    decorate: function() {
      this.classList.add('characteristic-list-item');

      /** @private {!object_fieldset.ObjectFieldSet} */
      this.characteristicFieldSet_ = object_fieldset.ObjectFieldSet();
      this.characteristicFieldSet_.setPropertyDisplayNames(INFO_PROPERTY_NAMES);
      this.characteristicFieldSet_.setObject({
        id: this.info.id,
        'uuid.uuid': this.info.uuid.uuid,
      });

      /** @private {!object_fieldset.ObjectFieldSet} */
      this.propertiesFieldSet_ = new object_fieldset.ObjectFieldSet();
      this.propertiesFieldSet_.setPropertyDisplayNames(
          PROPERTIES_PROPERTY_NAMES);
      var Property = interfaces.BluetoothDevice.Property;
      this.propertiesFieldSet_.setObject({
        broadcast: (this.info.properties & Property.BROADCAST) > 0,
        read: (this.info.properties & Property.READ) > 0,
        write_without_response: (this.info.properties &
            Property.WRITE_WITHOUT_RESPONSE) > 0,
        write: (this.info.properties & Property.WRITE) > 0,
        notify: (this.info.properties & Property.NOTIFY) > 0,
        indicate: (this.info.properties & Property.INDICATE) > 0,
        authenticated_signed_writes: (this.info.properties &
            Property.AUTHENTICATED_SIGNED_WRITES) > 0,
        extended_properties: (this.info.properties &
            Property.EXTENDED_PROPERTIES) > 0,
        reliable_write: (this.info.properties & Property.RELIABLE_WRITE) > 0,
        writable_auxiliaries: (this.info.properties &
            Property.WRITABLE_AUXILIARIES) > 0,
        read_encrypted: (this.info.properties & Property.READ_ENCRYPTED)  > 0,
        write_encrypted: (this.info.properties & Property.WRITE_ENCRYPTED)  > 0,
        read_encrypted_authenticated: (this.info.properties &
            Property.READ_ENCRYPTED_AUTHENTICATED) > 0,
        write_encrypted_authenticated: (this.info.properties &
            Property.WRITE_ENCRYPTED_AUTHENTICATED) > 0,
      });

      // Create content for display in brief content container.
      var characteristicHeaderText = document.createElement('div');
      characteristicHeaderText.textContent = 'Characteristic:';

      var characteristicHeaderValue = document.createElement('div');
      characteristicHeaderValue.textContent = this.info.uuid.uuid;

      var characteristicHeader = document.createElement('div');
      characteristicHeader.appendChild(characteristicHeaderText);
      characteristicHeader.appendChild(characteristicHeaderValue);
      this.briefContent_.appendChild(characteristicHeader);

      // Create content for display in expanded content container.
      var characteristicInfoHeader = document.createElement('h4');
      characteristicInfoHeader.textContent = 'Characteristic Info';

      var characteristicDiv = document.createElement('div');
      characteristicDiv.classList.add('flex');
      characteristicDiv.appendChild(this.characteristicFieldSet_);

      var propertiesHeader = document.createElement('h4');
      propertiesHeader.textContent = 'Properties';

      var propertiesDiv = document.createElement('div');
      propertiesDiv.classList.add('flex');
      propertiesDiv.appendChild(this.propertiesFieldSet_);

      var infoDiv = document.createElement('div');
      infoDiv.classList.add('info-container');
      infoDiv.appendChild(characteristicInfoHeader);
      infoDiv.appendChild(characteristicDiv);
      infoDiv.appendChild(propertiesHeader);
      infoDiv.appendChild(propertiesDiv);

      this.expandedContent_.appendChild(infoDiv);
    },
  };

  /**
   * A list that displays CharacteristicListItems.
   * @constructor
   */
  var CharacteristicList = cr.ui.define('list');

  CharacteristicList.prototype = {
    __proto__: ExpandableList.prototype,

    /** @override */
    decorate: function() {
      ExpandableList.prototype.decorate.call(this);

      /** @private {boolean} */
      this.characteristicsRequested_ = false;

      this.classList.add('characteristic-list');
      this.setEmptyMessage('No Characteristics Found');
    },

    /** @override */
    createItem: function(data) {
      return new CharacteristicListItem(data);
    },

    /**
     * Loads the characteristic list with an array of CharacteristicInfo from
     * the device with |deviceAddress| and service with |serviceId|. If no
     * active connection to the device exists, one is created.
     * @param {string} deviceAddress
     * @param {string} serviceId
     */
    load: function(deviceAddress, serviceId) {
      if (this.characteristicsRequested_ || !this.isLoading())
        return;

      this.characteristicsRequested_ = true;

      device_broker.connectToDevice(deviceAddress).then(function(device) {
        return device.getCharacteristics(serviceId);
      }.bind(this)).then(function(response) {
        this.setData(new ArrayDataModel(response.characteristics));
        this.setLoading(false);
        this.characteristicsRequested_ = false;
      }.bind(this)).catch(function(error) {
        this.characteristicsRequested_ = false;
        Snackbar.show(
            deviceAddress + ': ' + error.message, SnackbarType.ERROR, 'Retry',
            function() {
              this.load(deviceAddress, serviceId);
            }.bind(this));
      }.bind(this));
    },
  };

  return {
    CharacteristicList: CharacteristicList,
    CharacteristicListItem: CharacteristicListItem,
  };
});