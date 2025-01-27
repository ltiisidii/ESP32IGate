function sendToServer(data) {
    fetch("/mod", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: new URLSearchParams(data),
    })
        .then((response) => response.text())
        .then((message) => alert(message))
        .catch((error) => console.error("Error:", error));
}
