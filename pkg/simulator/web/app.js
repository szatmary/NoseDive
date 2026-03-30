(function() {
  'use strict';

  let ws = null;
  let focusedField = null; // Don't overwrite fields the user is editing

  const $ = (id) => document.getElementById(id);

  // --- WebSocket ---

  function connect() {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(proto + '//' + location.host + '/ws');

    ws.onopen = () => {
      $('connStatus').textContent = 'Connected';
      $('connStatus').className = 'status connected';
    };

    ws.onclose = () => {
      $('connStatus').textContent = 'Disconnected';
      $('connStatus').className = 'status disconnected';
      setTimeout(connect, 1000);
    };

    ws.onerror = () => ws.close();

    ws.onmessage = (e) => {
      try {
        const state = JSON.parse(e.data);
        updateDisplay(state);
      } catch (err) {}
    };
  }

  function send(field, value) {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ field, value }));
    }
  }

  // --- State Display ---

  function updateDisplay(s) {
    // Status badges
    const rsBadge = $('dispRunState');
    rsBadge.textContent = s.runState;
    rsBadge.className = 'badge ' + s.runState.toLowerCase();

    $('dispMode').textContent = s.mode;
    updateDimBadge('dispSAT', s.sat);
    updateDimBadge('dispStop', s.stopCondition);

    // Metrics
    $('dispSpeed').textContent = (s.speed * 3.6).toFixed(1);
    $('dispERPM').textContent = Math.round(s.erpm);
    $('dispDuty').textContent = (s.dutyCycle * 100).toFixed(1);
    $('dispMotorCurrent').textContent = s.motorCurrent.toFixed(1);
    $('dispBattCurrent').textContent = s.battCurrent.toFixed(1);
    $('dispSetpoint').textContent = s.setpoint.toFixed(1);

    // ADC
    $('dispADC1').textContent = s.adc1.toFixed(2);
    $('dispADC2').textContent = s.adc2.toFixed(2);

    // Counters
    $('dispAH').textContent = s.ampHours.toFixed(3);
    $('dispWH').textContent = s.wattHours.toFixed(3);

    // Update inputs only if user isn't editing them
    if (focusedField !== 'pitch') {
      $('pitch').value = s.pitch;
      $('pitchVal').innerHTML = s.pitch.toFixed(1) + '&deg;';
    }
    if (focusedField !== 'roll') {
      $('roll').value = s.roll;
      $('rollVal').innerHTML = s.roll.toFixed(1) + '&deg;';
    }
    if (focusedField !== 'voltage') {
      $('voltage').value = s.voltage;
      $('voltageVal').textContent = s.voltage.toFixed(1) + ' V';
    }
    if (focusedField !== 'mosfetTemp') {
      $('mosfetTemp').value = s.mosfetTemp;
      $('mosfetTempVal').innerHTML = s.mosfetTemp.toFixed(1) + ' &deg;C';
    }
    if (focusedField !== 'motorTemp') {
      $('motorTemp').value = s.motorTemp;
      $('motorTempVal').innerHTML = s.motorTemp.toFixed(1) + ' &deg;C';
    }
    if (focusedField !== 'runState') {
      $('runState').value = s.runState;
    }
    if (focusedField !== 'mode') {
      $('mode').value = s.mode;
    }
    if (focusedField !== 'fault') {
      $('fault').value = s.fault;
    }
    if (focusedField !== 'charging') {
      $('charging').checked = s.charging;
    }

    // Footpad buttons
    document.querySelectorAll('.fp-btn').forEach(btn => {
      btn.classList.toggle('active', btn.dataset.fp === s.footpad);
    });

    // Footpad pads visualization
    const fpLeft = s.footpad === 'LEFT' || s.footpad === 'BOTH';
    const fpRight = s.footpad === 'RIGHT' || s.footpad === 'BOTH';
    $('padLeft').classList.toggle('active', fpLeft);
    $('padRight').classList.toggle('active', fpRight);

    // Board side view - pitch rotation
    $('boardSide').style.transform = 'rotate(' + (-s.pitch) + 'deg)';
    // Board top view - roll rotation
    $('boardTop').style.transform = 'rotate(' + s.roll + 'deg)';
  }

  function updateDimBadge(id, value) {
    const el = $(id);
    el.textContent = value;
    el.className = value === 'NONE' ? 'badge dim' : 'badge fault';
  }

  // --- Input Handlers ---

  function setupSlider(id, field, suffix, fmt) {
    const slider = $(id);
    const valEl = $(id + 'Val');

    slider.addEventListener('input', () => {
      const v = parseFloat(slider.value);
      valEl.innerHTML = fmt(v) + suffix;
      send(field, v);
    });

    slider.addEventListener('focus', () => { focusedField = field; });
    slider.addEventListener('blur', () => { focusedField = null; });

    // Also handle mousedown/touchstart for continuous tracking
    slider.addEventListener('mousedown', () => { focusedField = field; });
    slider.addEventListener('touchstart', () => { focusedField = field; });
    document.addEventListener('mouseup', () => {
      if (focusedField === field) {
        // Delay clearing so the last value lands
        setTimeout(() => { focusedField = null; }, 200);
      }
    });
    document.addEventListener('touchend', () => {
      if (focusedField === field) {
        setTimeout(() => { focusedField = null; }, 200);
      }
    });
  }

  function setupSelect(id, field) {
    const sel = $(id);
    sel.addEventListener('change', () => {
      send(field, sel.value);
    });
    sel.addEventListener('focus', () => { focusedField = field; });
    sel.addEventListener('blur', () => { focusedField = null; });
  }

  function setupCheckbox(id, field) {
    const cb = $(id);
    cb.addEventListener('change', () => {
      send(field, cb.checked);
    });
  }

  function setupFootpadButtons() {
    document.querySelectorAll('.fp-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        send('footpad', btn.dataset.fp);
      });
    });
  }

  // --- Init ---

  function init() {
    setupSlider('pitch', 'pitch', '&deg;', v => v.toFixed(1));
    setupSlider('roll', 'roll', '&deg;', v => v.toFixed(1));
    setupSlider('voltage', 'voltage', ' V', v => v.toFixed(1));
    setupSlider('mosfetTemp', 'mosfetTemp', ' &deg;C', v => v.toFixed(1));
    setupSlider('motorTemp', 'motorTemp', ' &deg;C', v => v.toFixed(1));

    setupSelect('runState', 'runState');
    setupSelect('mode', 'mode');
    setupSelect('fault', 'fault');
    setupCheckbox('charging', 'charging');
    setupFootpadButtons();

    connect();
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
