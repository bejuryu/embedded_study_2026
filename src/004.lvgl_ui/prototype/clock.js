/* ═══════════════════════════════════════════════════════
   Digital Desk Clock – Live Clock Logic
   ═══════════════════════════════════════════════════════ */

(function () {
  'use strict';

  // ── DOM Elements ──
  const hoursEl   = document.getElementById('clock-hours');
  const minutesEl = document.getElementById('clock-minutes');
  const secondsEl = document.getElementById('clock-seconds');
  const dateEl    = document.getElementById('date-display');

  // ── Day names in Korean ──
  const DAY_NAMES_KR = ['일요일', '월요일', '화요일', '수요일', '목요일', '금요일', '토요일'];

  // ── Pad number to 2 digits ──
  function pad(n) {
    return n.toString().padStart(2, '0');
  }

  // ── Update clock display ──
  function updateClock() {
    const now = new Date();

    // Time
    hoursEl.textContent   = pad(now.getHours());
    minutesEl.textContent = pad(now.getMinutes());
    secondsEl.textContent = pad(now.getSeconds());

    // Date: YYYY.MM.DD
    const year  = now.getFullYear();
    const month = pad(now.getMonth() + 1);
    const day   = pad(now.getDate());
    dateEl.textContent = `${year}.${month}.${day}`;

    // Day of week (Korean)
    const dayOfWeek = document.querySelector('.weather-day');
    if (dayOfWeek) {
      dayOfWeek.textContent = DAY_NAMES_KR[now.getDay()];
    }
  }

  // ── Initialize ──
  updateClock();
  setInterval(updateClock, 1000);

  // ── Simulate battery / WiFi status (for prototype) ──
  function simulateStatus() {
    const voltages = [3.7, 3.8, 3.9, 4.0, 4.1, 4.2];
    const batteryEl = document.querySelector('#battery-voltage .status-text');
    const chargeEl = document.getElementById('charge-status');

    if (batteryEl) {
      const v = voltages[Math.floor(Math.random() * voltages.length)];
      batteryEl.textContent = v.toFixed(1) + 'V';

      // Update battery fill based on voltage
      const fill = document.querySelector('.battery-fill');
      if (fill) {
        const pct = ((v - 3.3) / (4.2 - 3.3)) * 14; // max width = 14
        fill.setAttribute('width', Math.max(2, pct).toString());
      }
    }

    if (chargeEl) {
      // Toggle charging animation
      chargeEl.classList.toggle('charging', Math.random() > 0.3);
    }
  }

  // Run status simulation every 10s
  simulateStatus();
  setInterval(simulateStatus, 10000);

})();
