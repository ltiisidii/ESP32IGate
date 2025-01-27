function submitMODBUS() {
    sendToServer({
        modbus_enable: document.getElementById("modbus_enable").checked,
        modbus_channel: document.getElementById("modbus_channel").value,
        modbus_address: document.getElementById("modbus_address").value,
        modbus_de_gpio: document.getElementById("modbus_de_gpio").value,
    });
}
