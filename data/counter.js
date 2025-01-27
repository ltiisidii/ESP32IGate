function submitCounter0() {
    sendToServer({
        counter0_enable: document.getElementById("counter0_enable").checked,
        counter0_gpio: document.getElementById("counter0_gpio").value,
        counter0_active: document.querySelector('input[name="counter0_active"]:checked').value,
    });
}

function submitCounter1() {
    sendToServer({
        counter1_enable: document.getElementById("counter1_enable").checked,
        counter1_gpio: document.getElementById("counter1_gpio").value,
        counter1_active: document.querySelector('input[name="counter1_active"]:checked').value,
    });
}
