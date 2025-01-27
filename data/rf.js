function submitRF() {
    sendToServer({
        rf_baudrate: document.getElementById("rf_baudrate").value,
        rf_rx_gpio: document.getElementById("rf_rx_gpio").value,
        rf_tx_gpio: document.getElementById("rf_tx_gpio").value,
        rf_pd_gpio: document.getElementById("rf_pd_gpio").value,
        rf_pwr_gpio: document.getElementById("rf_pwr_gpio").value,
        rf_ptt_gpio: document.getElementById("rf_ptt_gpio").value,
        rf_sql_gpio: document.getElementById("rf_sql_gpio").value,
        rf_adc_atten: document.getElementById("rf_adc_atten").value,
        rf_adc_dc_offset: document.getElementById("rf_adc_dc_offset").value,
        rf_pd_active: document.querySelector('input[name="rf_pd_active"]:checked').value,
        rf_pwr_active: document.querySelector('input[name="rf_pwr_active"]:checked').value,
        rf_ptt_active: document.querySelector('input[name="rf_ptt_active"]:checked').value,
        rf_sql_active: document.querySelector('input[name="rf_sql_active"]:checked').value,
    });
}
