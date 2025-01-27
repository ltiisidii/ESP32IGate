function submitUART0() {
    sendToServer({
        uart0_enable: document.getElementById("uart0_enable").checked,
        uart0_rx_gpio: document.getElementById("uart0_rx_gpio").value,
        uart0_tx_gpio: document.getElementById("uart0_tx_gpio").value,
        uart0_rts_gpio: document.getElementById("uart0_rts_gpio").value,
        uart0_baudrate: document.getElementById("uart0_baudrate").value,
    });
}

function submitUART1() {
    sendToServer({
        uart1_enable: document.getElementById("uart1_enable").checked,
        uart1_rx_gpio: document.getElementById("uart1_rx_gpio").value,
        uart1_tx_gpio: document.getElementById("uart1_tx_gpio").value,
        uart1_rts_gpio: document.getElementById("uart1_rts_gpio").value,
        uart1_baudrate: document.getElementById("uart1_baudrate").value,
    });
}

function submitUART2() {
    sendToServer({
        uart2_enable: document.getElementById("uart2_enable").checked,
        uart2_rx_gpio: document.getElementById("uart2_rx_gpio").value,
        uart2_tx_gpio: document.getElementById("uart2_tx_gpio").value,
        uart2_rts_gpio: document.getElementById("uart2_rts_gpio").value,
        uart2_baudrate: document.getElementById("uart2_baudrate").value,
    });
}
