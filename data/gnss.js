function submitGNSS() {
    sendToServer({
        gnss_enable: document.getElementById("gnss_enable").checked,
        gnss_channel: document.getElementById("gnss_channel").value,
        gnss_at_command: document.getElementById("gnss_at_command").value,
        gnss_tcp_host: document.getElementById("gnss_tcp_host").value,
        gnss_tcp_port: document.getElementById("gnss_tcp_port").value,
    });
}
